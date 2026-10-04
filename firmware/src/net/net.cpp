// On-demand Wi-Fi: clock sync at boot + direct Tapo plug control (see net.h).
#include "net.h"
#include "tapo_client.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
#define NET_HAVE_WIFI 1
#else
#define NET_HAVE_WIFI 0
#endif
#if NET_HAVE_WIFI && defined(TAPO_USERNAME) && defined(TAPO_PASSWORD) && defined(TAPO_PLUG_IPS)
#define NET_HAVE_TAPO 1
static const char* const PLUG_IPS[] = TAPO_PLUG_IPS;
#ifdef TAPO_PLUG_NAMES
static const char* const PLUG_NAMES[] = TAPO_PLUG_NAMES;
#define PLUG_NAME_COUNT ((int)(sizeof(PLUG_NAMES) / sizeof(PLUG_NAMES[0])))
#else
static const char* const* PLUG_NAMES = nullptr;
#define PLUG_NAME_COUNT 0
#endif
#define PLUG_COUNT (min((int)(sizeof(PLUG_IPS) / sizeof(PLUG_IPS[0])), NET_MAX_PLUGS))
#else
#define NET_HAVE_TAPO 0
#define PLUG_COUNT 0
#endif
#ifndef TIME_ZONE
#define TIME_ZONE "CST-8"
#endif
#ifndef TIME_FORMAT
#define TIME_FORMAT 24
#endif

#define WIFI_JOIN_MS      12000
#define PLUG_GRACE_MS     60000     // keep Wi-Fi up this long after the app closes
#define TIME_RETRY_MS     (10UL * 60UL * 1000UL)
#define TIME_MAX_TRIES    3

enum JobType : uint8_t { J_TIME, J_REFRESH, J_SET };
struct Job { JobType type; int8_t idx; uint8_t on; };

static QueueHandle_t      q_;
static SemaphoreHandle_t  mtx_;
static NetPlug            plugs_[NET_MAX_PLUGS];
static int                plug_n_ = 0;
static NetState           state_ = NET_OFF;
static volatile uint32_t  gen_ = 0;
static volatile int       users_ = 0;
static volatile uint32_t  hold_until_ = 0;
static volatile bool      time_new_ = false;
static long               time_epoch_ = 0;
static bool               time_synced_ = false;
static int                time_tries_ = 0;
static uint32_t           time_next_try_ = 0;
static bool               wifi_on_ = false;

#if NET_HAVE_TAPO
static TapoAuth   auth_;
static TapoClient clients_[NET_MAX_PLUGS];
#endif

bool net_configured(void)      { return NET_HAVE_TAPO && PLUG_COUNT > 0; }
bool net_time_configured(void) { return NET_HAVE_WIFI; }

static void lock()   { xSemaphoreTake(mtx_, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx_); }
static void bump()   { gen_ = gen_ + 1; }

static void set_state(NetState s) {
    lock();
    if (state_ != s) { state_ = s; bump(); }
    unlock();
}

static void post(JobType t, int idx = -1, bool on = false) {
    if (!q_) return;
    Job j{t, (int8_t)idx, (uint8_t)on};
    xQueueSend(q_, &j, 0);
}

// ---------------------------------------------------------------- Wi-Fi
static bool wifi_up(void) {
#if NET_HAVE_WIFI
    if (wifi_on_ && WiFi.status() == WL_CONNECTED) return true;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);                 // modem sleep — required alongside BLE
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifi_on_ = true;
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_MS) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[net] Wi-Fi up (%s) in %lu ms\n", WiFi.localIP().toString().c_str(), millis() - t0);
        return true;
    }
    Serial.println("[net] Wi-Fi join failed");
#endif
    return false;
}

static void wifi_down(void) {
    if (!wifi_on_) return;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_on_ = false;
#if NET_HAVE_TAPO
    for (int i = 0; i < PLUG_COUNT; i++) clients_[i].reset();
#endif
    Serial.println("[net] Wi-Fi off");
}

// ---------------------------------------------------------------- clock
static long days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void do_time_sync(void) {
    configTzTime(TIME_ZONE, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    uint32_t t0 = millis();
    while (time(nullptr) < 1700000000 && millis() - t0 < 10000) delay(100);
    time_t now = time(nullptr);
    if (now < 1700000000) {
        Serial.println("[net] NTP failed");
        return;
    }
    struct tm lt;
    localtime_r(&now, &lt);
    long local = days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400L
               + lt.tm_hour * 3600L + lt.tm_min * 60L + lt.tm_sec;
    lock();
    time_epoch_ = local;
    time_new_ = true;
    unlock();
    time_synced_ = true;
    Serial.printf("[net] clock set %02d:%02d\n", lt.tm_hour, lt.tm_min);
}

// ---------------------------------------------------------------- plugs
#if NET_HAVE_TAPO
static void read_plug(int i, bool* auth_err) {
    bool on = false;
    char nick[48] = "";
    int rc = clients_[i].get_info(&on, PLUG_NAME_COUNT > i ? nullptr : nick, sizeof(nick));
    if (rc == TAPO_ERR_AUTH) *auth_err = true;
    lock();
    NetPlug& p = plugs_[i];
    if (PLUG_NAME_COUNT > i) snprintf(p.name, sizeof(p.name), "%s", PLUG_NAMES[i]);
    else if (nick[0])        snprintf(p.name, sizeof(p.name), "%s", nick);
    else if (!p.name[0])     snprintf(p.name, sizeof(p.name), "插座 %d", i + 1);
    p.ok = rc == TAPO_OK;
    if (rc == TAPO_OK) p.on = on;
    bump();
    unlock();
    if (rc != TAPO_OK) Serial.printf("[net] plug %s error %d\n", PLUG_IPS[i], rc);
}

static void read_all(void) {
    bool auth_err = false;
    for (int i = 0; i < PLUG_COUNT; i++) read_plug(i, &auth_err);
    lock();
    plug_n_ = PLUG_COUNT;
    unlock();
    set_state(auth_err ? NET_ERR_AUTH : NET_READY);
}

static void set_plug(int i, bool on) {
    if (i < 0 || i >= PLUG_COUNT) return;
    int rc = clients_[i].set_on(on);
    Serial.printf("[net] plug %s -> %s (%d)\n", PLUG_IPS[i], on ? "on" : "off", rc);
    bool auth_err = false;
    read_plug(i, &auth_err);
    if (auth_err) set_state(NET_ERR_AUTH);
}
#endif

// ---------------------------------------------------------------- worker
static void worker(void*) {
    for (;;) {
        Job j;
        if (xQueueReceive(q_, &j, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!wifi_up()) {
                if (j.type != J_TIME) set_state(NET_ERR_WIFI);
                else time_next_try_ = millis() + TIME_RETRY_MS;
                wifi_down();
                continue;
            }
            if (!time_synced_) do_time_sync();       // any time Wi-Fi is up
#if NET_HAVE_TAPO
            if (j.type == J_REFRESH) read_all();
            else if (j.type == J_SET) set_plug(j.idx, j.on);
#endif
            if (j.type == J_TIME && !time_synced_) time_next_try_ = millis() + TIME_RETRY_MS;
        }
        // Retry a failed boot-time clock sync a couple of times.
        if (!time_synced_ && time_tries_ > 0 && time_tries_ < TIME_MAX_TRIES &&
            (int32_t)(millis() - time_next_try_) > 0 && uxQueueMessagesWaiting(q_) == 0) {
            time_tries_++;
            post(J_TIME);
        }
        if (wifi_on_ && users_ == 0 && uxQueueMessagesWaiting(q_) == 0 &&
            (int32_t)(millis() - hold_until_) > 0) {
            wifi_down();
            set_state(NET_OFF);
        }
    }
}

void net_init(void) {
    if (!NET_HAVE_WIFI) return;
    mtx_ = xSemaphoreCreateMutex();
    q_ = xQueueCreate(8, sizeof(Job));
#if NET_HAVE_TAPO
    auth_.set(TAPO_USERNAME, TAPO_PASSWORD);
    for (int i = 0; i < PLUG_COUNT; i++) {
        clients_[i].init(PLUG_IPS[i], &auth_);
        plugs_[i].name[0] = 0;
        if (PLUG_NAME_COUNT > i) snprintf(plugs_[i].name, sizeof(plugs_[i].name), "%s", PLUG_NAMES[i]);
    }
#endif
    WiFi.mode(WIFI_OFF);
    xTaskCreate(worker, "net", 8192, nullptr, 1, nullptr);
    hold_until_ = millis();
    time_tries_ = 1;
    post(J_TIME);
}

bool net_take_time(long* local_epoch, int* fmt) {
    if (!mtx_ || !time_new_) return false;
    lock();
    *local_epoch = time_epoch_;
    time_new_ = false;
    unlock();
    *fmt = TIME_FORMAT;
    return true;
}

void net_plugs_open(void) {
    if (!net_configured() || !q_) return;
    users_ = users_ + 1;
    lock();
    if (state_ != NET_READY) { state_ = NET_CONNECTING; bump(); }
    unlock();
    post(J_REFRESH);
}

void net_plugs_close(void) {
    if (!net_configured() || !q_ || users_ == 0) return;
    hold_until_ = millis() + PLUG_GRACE_MS;
    users_ = users_ - 1;
}

void net_plugs_refresh(void) {
    if (net_configured() && q_ && uxQueueMessagesWaiting(q_) == 0) post(J_REFRESH);
}

void net_plug_set(int i, bool on) {
    if (net_configured() && q_) post(J_SET, i, on);
}

int net_plugs_snapshot(NetPlug* out, int max, NetState* state, uint32_t* gen) {
    if (!mtx_) { if (state) *state = NET_OFF; if (gen) *gen = 0; return 0; }
    lock();
    int n = plug_n_ < max ? plug_n_ : max;
    memcpy(out, plugs_, sizeof(NetPlug) * n);
    if (state) *state = state_;
    if (gen) *gen = gen_;
    unlock();
    return n;
}
