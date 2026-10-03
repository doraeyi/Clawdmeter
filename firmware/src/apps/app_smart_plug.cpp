// 智慧插座 Smart Plug — on/off buttons for TP-Link Tapo plugs (P100/P105/P110…).
//
// The board never talks to the plugs itself. The Windows daemon controls them
// over the home network (python `tapo` library) and talks to the board over
// BLE characteristic …0006:
//   daemon → board (write):  {"pl":[{"n":"檯燈","on":1,"ok":1}, …], "err":"…"}
//   board → daemon (notify): {"cmd":"set","i":<index>,"on":<0|1>}
// Tapping a card flips it immediately (optimistic) and the next state payload
// from the daemon confirms or corrects it.
#include "app.h"
#include "smart_plug.h"
#include "../theme.h"
#include "../ble.h"
#include "../hal/board_caps.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

LV_FONT_DECLARE(font_cjk_28);
LV_FONT_DECLARE(font_icons_64);

extern const App APP_SMART_PLUG;

#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR < 3
#define PLUG_LONG_DOTS LV_LABEL_LONG_DOT
#define PLUG_LONG_WRAP LV_LABEL_LONG_WRAP
#else
#define PLUG_LONG_DOTS LV_LABEL_LONG_MODE_DOTS
#define PLUG_LONG_WRAP LV_LABEL_LONG_MODE_WRAP
#endif

#define MAX_PLUGS      4
#define STALE_MS       45000   // daemon sends a heartbeat every ~15 s
#define PENDING_MS     8000    // give up waiting for confirmation after this

struct Plug {
    char     name[48];
    bool     on;
    bool     ok;        // reachable
    uint32_t pending;   // millis() of the last tap, 0 = none
};

static Plug     plugs[MAX_PLUGS];
static int      plug_count = 0;
static char     err_msg[96] = "";
static uint32_t last_rx_ms = 0;
static bool     dirty = true;

static lv_obj_t *cards[MAX_PLUGS], *lbl_name[MAX_PLUGS], *lbl_state[MAX_PLUGS], *knob[MAX_PLUGS], *track[MAX_PLUGS];
static lv_obj_t *lbl_msg, *icon_big;

static const lv_color_t COL_ON  = lv_color_hex(0x788c5d);

static void card_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= plug_count || !plugs[i].ok) return;
    bool target = !plugs[i].on;
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"set\",\"i\":%d,\"on\":%d}", i, target ? 1 : 0);
    if (!ble_plug_command(cmd)) return;
    plugs[i].on = target;
    plugs[i].pending = millis() | 1;
    dirty = true;
}

static void plug_create(lv_obj_t* root) {
    const int W = board_caps().width, H = board_caps().height;
    app_make_title(root, app_label(&APP_SMART_PLUG));

    const int top = H >= 460 ? 96 : 76;
    const int bottom = H - 56;                 // keep clear of the swipe-home bar
    const int gap = 14;
    const int ch = (bottom - top - gap * 2) / 3;
    const int cw = W - 64;

    for (int i = 0; i < MAX_PLUGS; i++) {
        lv_obj_t* c = lv_obj_create(root);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, cw, ch);
        lv_obj_set_pos(c, 32, top + i * (ch + gap));
        lv_obj_set_style_radius(c, 24, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, THEME_PANEL, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(0x33332f), LV_STATE_PRESSED);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        cards[i] = c;

        lbl_name[i] = lv_label_create(c);
        lv_obj_set_style_text_font(lbl_name[i], &font_cjk_28, 0);
        lv_obj_set_style_text_color(lbl_name[i], THEME_TEXT, 0);
        lv_label_set_long_mode(lbl_name[i], PLUG_LONG_DOTS);
        lv_obj_set_width(lbl_name[i], cw - 160);
        lv_obj_align(lbl_name[i], LV_ALIGN_LEFT_MID, 24, -16);

        lbl_state[i] = lv_label_create(c);
        lv_obj_set_style_text_font(lbl_state[i], &font_cjk_28, 0);
        lv_obj_set_style_text_color(lbl_state[i], THEME_DIM, 0);
        lv_obj_align(lbl_state[i], LV_ALIGN_LEFT_MID, 24, 20);

        // Toggle switch drawn with two plain objects (cheap to redraw).
        track[i] = lv_obj_create(c);
        lv_obj_remove_style_all(track[i]);
        lv_obj_set_size(track[i], 96, 52);
        lv_obj_align(track[i], LV_ALIGN_RIGHT_MID, -22, 0);
        lv_obj_set_style_radius(track[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(track[i], LV_OPA_COVER, 0);
        lv_obj_remove_flag(track[i], LV_OBJ_FLAG_CLICKABLE);

        knob[i] = lv_obj_create(track[i]);
        lv_obj_remove_style_all(knob[i]);
        lv_obj_set_size(knob[i], 42, 42);
        lv_obj_set_style_radius(knob[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(knob[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(knob[i], THEME_TEXT, 0);
        lv_obj_remove_flag(knob[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    }

    icon_big = lv_label_create(root);
    lv_obj_set_style_text_font(icon_big, &font_icons_64, 0);
    lv_obj_set_style_text_color(icon_big, lv_color_hex(APP_SMART_PLUG.color), 0);
    lv_label_set_text(icon_big, ICON_PLUG);
    lv_obj_align(icon_big, LV_ALIGN_CENTER, 0, -40);

    lbl_msg = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_msg, &font_cjk_28, 0);
    lv_obj_set_style_text_color(lbl_msg, THEME_DIM, 0);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_msg, PLUG_LONG_WRAP);
    lv_obj_set_width(lbl_msg, W - 80);
    lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, 50);
}

static void show_message(const char* msg) {
    for (int i = 0; i < MAX_PLUGS; i++) lv_obj_add_flag(cards[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(icon_big, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_msg, LV_OBJ_FLAG_HIDDEN);
    if (strcmp(lv_label_get_text(lbl_msg), msg) != 0) lv_label_set_text(lbl_msg, msg);
}

static void render(void) {
    const bool fresh = last_rx_ms && (millis() - last_rx_ms) < STALE_MS;
    if (ble_get_state() != BLE_STATE_CONNECTED) { show_message("藍牙未連線"); return; }
    if (!fresh)            { show_message("等待電腦端程式…"); return; }
    if (plug_count == 0)   { show_message(err_msg[0] ? err_msg : "電腦端還沒設定插座\n請看 docs/apps.md"); return; }

    lv_obj_add_flag(icon_big, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_msg, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < MAX_PLUGS; i++) {
        if (i >= plug_count || i >= 3) { lv_obj_add_flag(cards[i], LV_OBJ_FLAG_HIDDEN); continue; }
        const Plug& p = plugs[i];
        lv_obj_clear_flag(cards[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_name[i], p.name);
        const char* st = !p.ok ? "離線" : p.pending ? (p.on ? "開啟中…" : "關閉中…") : (p.on ? "開" : "關");
        lv_label_set_text(lbl_state[i], st);
        lv_obj_set_style_text_color(lbl_state[i], p.ok && p.on ? COL_ON : THEME_DIM, 0);
        lv_obj_set_style_bg_color(track[i], p.ok && p.on ? COL_ON : lv_color_hex(0x3a3a37), 0);
        lv_obj_set_style_bg_opa(knob[i], p.ok ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_align(knob[i], p.on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, p.on ? -5 : 5, 0);
    }
}

static void plug_enter(void) { dirty = true; render(); }

static void plug_tick(void) {
    static uint32_t last = 0;
    static int last_ble = -1;
    static bool last_fresh = false;
    // Revert taps the daemon never confirmed.
    for (int i = 0; i < plug_count; i++)
        if (plugs[i].pending && millis() - plugs[i].pending > PENDING_MS) {
            plugs[i].on = !plugs[i].on;
            plugs[i].pending = 0;
            dirty = true;
        }
    const bool fresh = last_rx_ms && (millis() - last_rx_ms) < STALE_MS;
    const int bs = (int)ble_get_state();
    if (bs != last_ble || fresh != last_fresh) { dirty = true; last_ble = bs; last_fresh = fresh; }
    if (!dirty || millis() - last < 100) return;
    last = millis();
    dirty = false;
    render();
}

void smart_plug_on_payload(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;
    last_rx_ms = millis();
    const char* err = doc["err"] | "";
    strncpy(err_msg, err, sizeof(err_msg) - 1);
    err_msg[sizeof(err_msg) - 1] = '\0';

    JsonArray arr = doc["pl"].as<JsonArray>();
    int n = 0;
    for (JsonObject o : arr) {
        if (n >= MAX_PLUGS) break;
        Plug& p = plugs[n];
        const char* nm = o["n"] | "";
        char fallback[16];
        if (!nm[0]) { snprintf(fallback, sizeof(fallback), "插座 %d", n + 1); nm = fallback; }
        strncpy(p.name, nm, sizeof(p.name) - 1);
        p.name[sizeof(p.name) - 1] = '\0';
        p.ok = (o["ok"] | 1) != 0;
        bool on = (o["on"] | 0) != 0;
        // A payload that agrees with a pending tap confirms it; one that
        // disagrees within the first second is probably from before the tap.
        if (p.pending && on != p.on && millis() - p.pending < 1500) on = p.on;
        else p.pending = 0;
        p.on = on;
        n++;
    }
    for (int i = n; i < MAX_PLUGS; i++) plugs[i].pending = 0;
    plug_count = n;
    dirty = true;
}

extern const App APP_SMART_PLUG = {
    /*name*/       "Smart Plug",
    /*icon_glyph*/ ICON_PLUG,
    /*icon_img*/   nullptr,
    /*color*/      0x788c5d,
    /*create*/     plug_create,
    /*enter*/      plug_enter,
    /*leave*/      nullptr,
    /*tick*/       plug_tick,
    /*on_pwr*/     nullptr,
    /*label*/      "智慧插座",
};
