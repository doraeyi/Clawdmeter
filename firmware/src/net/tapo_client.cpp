#include "tapo_client.h"
#include "http_post.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <Arduino.h>
static uint32_t now_ms() { return millis(); }
#else
#include <chrono>
static uint32_t now_ms() {
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif

#define TAPO_TIMEOUT_MS   4000
#define SESSION_MAX_MS    (20UL * 60UL * 1000UL)   // re-handshake well before the device's TIMEOUT
#define BUF_MAX           3072

static uint8_t g_req[512];
static uint8_t g_resp[BUF_MAX];
static char    g_plain[BUF_MAX];

void TapoClient::init(const char* host, const TapoAuth* auth) {
    snprintf(host_, sizeof(host_), "%s", host);
    auth_ = auth;
    ready_ = false;
}

int TapoClient::handshake() {
    ready_ = false;
    uint8_t local[16];
    klap_random(local, 16);

    HttpResult r = http_post(host_, 80, "/app/handshake1", nullptr, local, 16, g_resp, sizeof(g_resp), TAPO_TIMEOUT_MS);
    if (r.status < 0) return TAPO_ERR_NET;
    if (r.status != 200 || r.body_len != 48) return TAPO_ERR_DEVICE;
    snprintf(cookie_, sizeof(cookie_), "%s", r.set_cookie);

    uint8_t remote[16], server_hash[32], expect[32];
    memcpy(remote, g_resp, 16);
    memcpy(server_hash, g_resp + 16, 32);

    // Newer firmware uses the v2 hashes, older the v1 ones; accept either.
    const uint8_t* auth = nullptr;
    size_t auth_len = 0;
    klap_hs1_expected(local, remote, auth_->v2, 32, expect);
    if (memcmp(expect, server_hash, 32) == 0) { auth = auth_->v2; auth_len = 32; }
    else {
        klap_hs1_expected(local, remote, auth_->v1, 16, expect);
        if (memcmp(expect, server_hash, 32) == 0) { auth = auth_->v1; auth_len = 16; }
    }
    if (!auth) return TAPO_ERR_AUTH;

    uint8_t hs2[32];
    klap_hs2_payload(local, remote, auth, auth_len, hs2);
    r = http_post(host_, 80, "/app/handshake2", cookie_[0] ? cookie_ : nullptr, hs2, 32, g_resp, sizeof(g_resp), TAPO_TIMEOUT_MS);
    if (r.status < 0) return TAPO_ERR_NET;
    if (r.status != 200) return TAPO_ERR_AUTH;

    klap_session_init(&s_, local, remote, auth, auth_len);
    ready_ = true;
    hs_ms_ = now_ms();
    return TAPO_OK;
}

int TapoClient::request(const char* json, char* plain, size_t plain_max) {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!ready_ || now_ms() - hs_ms_ > SESSION_MAX_MS) {
            int h = handshake();
            if (h != TAPO_OK) return h;
        }
        const size_t len = strlen(json);
        if (klap_encrypted_size(len) > sizeof(g_req)) return TAPO_ERR_DEVICE;
        int32_t seq;
        size_t n = klap_encrypt(&s_, (const uint8_t*)json, len, g_req, &seq);
        char path[48];
        snprintf(path, sizeof(path), "/app/request?seq=%ld", (long)seq);
        HttpResult r = http_post(host_, 80, path, cookie_[0] ? cookie_ : nullptr, g_req, n, g_resp, sizeof(g_resp), TAPO_TIMEOUT_MS);
        if (r.status < 0) { ready_ = false; return TAPO_ERR_NET; }
        if (r.status != 200) { ready_ = false; continue; }    // session expired → handshake again
        if (r.body_len > plain_max) return TAPO_ERR_DEVICE;
        if (klap_decrypt(&s_, seq, g_resp, r.body_len, (uint8_t*)plain) < 0) { ready_ = false; continue; }
        return TAPO_OK;
    }
    return TAPO_ERR_DEVICE;
}

int TapoClient::get_info(bool* on, char* nickname, size_t nick_len) {
    int rc = request("{\"method\":\"get_device_info\",\"requestTimeMils\":0}", g_plain, sizeof(g_plain));
    if (rc != TAPO_OK) return rc;
    JsonDocument doc;
    if (deserializeJson(doc, g_plain)) return TAPO_ERR_DEVICE;
    if ((doc["error_code"] | -1) != 0) return TAPO_ERR_DEVICE;
    JsonObject res = doc["result"];
    if (on) *on = res["device_on"] | false;
    if (nickname && nick_len) {
        nickname[0] = 0;
        const char* b64 = res["nickname"] | "";
        if (tapo_b64_decode(b64, nickname, nick_len) < 0) nickname[0] = 0;
    }
    return TAPO_OK;
}

int TapoClient::set_on(bool on) {
    char json[96];
    snprintf(json, sizeof(json),
             "{\"method\":\"set_device_info\",\"params\":{\"device_on\":%s},\"requestTimeMils\":0}",
             on ? "true" : "false");
    int rc = request(json, g_plain, sizeof(g_plain));
    if (rc != TAPO_OK) return rc;
    JsonDocument doc;
    if (deserializeJson(doc, g_plain)) return TAPO_ERR_DEVICE;
    return (doc["error_code"] | -1) == 0 ? TAPO_OK : TAPO_ERR_DEVICE;
}

int tapo_b64_decode(const char* in, char* out, size_t out_max) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; *in && *in != '='; in++) {
        int v = val(*in);
        if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o + 1 >= out_max) return -1;
            out[o++] = (char)((acc >> bits) & 0xFF);
        }
    }
    out[o] = 0;
    return (int)o;
}
