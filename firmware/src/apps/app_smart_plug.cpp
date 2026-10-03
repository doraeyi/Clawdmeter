// 智慧插座 Smart Plug — on/off buttons for TP-Link Tapo plugs (P100/P105/P110…).
//
// The board never talks to the plugs itself. The Windows daemon controls them
// over the home network (python `tapo` library) and talks to the board over
// BLE characteristic …0006:
//   daemon → board (write):  {"pl":[{"n":"檯燈","on":1,"ok":1}, …], "err":"…"}
//   board → daemon (notify): {"cmd":"set","i":<index>,"on":<0|1>}
// Tapping a tile flips it immediately (lit = on, grey = off) and the next
// state payload from the daemon confirms or corrects it.
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

// 2×2 grid. Tiles 0..plug_count-1 are plugs; the next free tile is "全部"
// (all off if anything is on, otherwise all on); any remaining tile is hidden.
#define TILES 4
static lv_obj_t *tiles[TILES], *t_icon[TILES], *t_name[TILES];
static lv_obj_t *lbl_msg, *icon_big;

static const lv_color_t COL_ON_BG   = lv_color_hex(0x4a3f22);
static const lv_color_t COL_ON_ICON = lv_color_hex(0xf5c451);
static const lv_color_t COL_OFF_BG  = lv_color_hex(0x1f1f1e);
static const lv_color_t COL_OFF_ICON= lv_color_hex(0x5c5b57);

static bool send_set(int i, bool on) {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"set\",\"i\":%d,\"on\":%d}", i, on ? 1 : 0);
    if (!ble_plug_command(cmd)) return false;
    plugs[i].on = on;
    plugs[i].pending = millis() ? millis() : 1;   // 0 means "none"
    return true;
}

static bool any_on(void) {
    for (int i = 0; i < plug_count; i++) if (plugs[i].ok && plugs[i].on) return true;
    return false;
}

static void tile_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < plug_count) {
        if (plugs[i].ok) send_set(i, !plugs[i].on);
    } else if (i == plug_count) {          // 全部
        bool target = !any_on();
        for (int k = 0; k < plug_count; k++)
            if (plugs[k].ok && plugs[k].on != target) send_set(k, target);
    }
    dirty = true;
}

static void plug_create(lv_obj_t* root) {
    const int W = board_caps().width, H = board_caps().height;
    app_make_title(root, app_label(&APP_SMART_PLUG));

    const int top = H >= 460 ? 86 : 70;
    const int bottom = H - 44;                 // keep clear of the swipe-home bar
    const int gap = 14, side = 32;
    const int tw = (W - side * 2 - gap) / 2;
    const int th = (bottom - top - gap) / 2;

    for (int i = 0; i < TILES; i++) {
        lv_obj_t* t = lv_obj_create(root);
        lv_obj_remove_style_all(t);
        lv_obj_set_size(t, tw, th);
        lv_obj_set_pos(t, side + (i % 2) * (tw + gap), top + (i / 2) * (th + gap));
        lv_obj_set_style_radius(t, 26, 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(t, COL_OFF_BG, 0);
        lv_obj_set_style_transform_scale(t, 240, LV_STATE_PRESSED);   // slight press feedback
        lv_obj_set_style_transform_pivot_x(t, tw / 2, 0);
        lv_obj_set_style_transform_pivot_y(t, th / 2, 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(t, tile_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        tiles[i] = t;

        t_icon[i] = lv_label_create(t);
        lv_obj_set_style_text_font(t_icon[i], &font_icons_64, 0);
        lv_label_set_text(t_icon[i], ICON_PLUG);
        lv_obj_align(t_icon[i], LV_ALIGN_CENTER, 0, -22);

        t_name[i] = lv_label_create(t);
        lv_obj_set_style_text_font(t_name[i], &font_cjk_28, 0);
        lv_obj_set_style_text_align(t_name[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(t_name[i], PLUG_LONG_DOTS);
        lv_obj_set_size(t_name[i], tw - 16, 40);
        lv_obj_align(t_name[i], LV_ALIGN_CENTER, 0, 44);
        lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
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
    for (int i = 0; i < TILES; i++) lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(icon_big, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_msg, LV_OBJ_FLAG_HIDDEN);
    if (strcmp(lv_label_get_text(lbl_msg), msg) != 0) lv_label_set_text(lbl_msg, msg);
}

// Lit (amber) = on, grey = off. Unreachable plugs are drawn at half opacity;
// a tap waiting for the daemon's confirmation at 75 %.
static void style_tile(int i, const char* icon, const char* name, bool lit, lv_opa_t opa) {
    lv_obj_clear_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(tiles[i], lit ? COL_ON_BG : COL_OFF_BG, 0);
    lv_label_set_text(t_icon[i], icon);
    lv_obj_set_style_text_color(t_icon[i], lit ? COL_ON_ICON : COL_OFF_ICON, 0);
    lv_label_set_text(t_name[i], name);
    lv_obj_set_style_text_color(t_name[i], lit ? THEME_TEXT : THEME_DIM, 0);
    lv_obj_set_style_opa(tiles[i], opa, 0);
}

static void render(void) {
    const bool fresh = last_rx_ms && (millis() - last_rx_ms) < STALE_MS;
    if (ble_get_state() != BLE_STATE_CONNECTED) { show_message("藍牙未連線"); return; }
    if (!fresh)            { show_message("等待電腦端程式…"); return; }
    if (plug_count == 0)   { show_message(err_msg[0] ? err_msg : "電腦端還沒設定插座\n請看 docs/apps.md"); return; }

    lv_obj_add_flag(icon_big, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_msg, LV_OBJ_FLAG_HIDDEN);
    bool any_ok = false;
    for (int i = 0; i < plug_count; i++) any_ok |= plugs[i].ok;
    for (int i = 0; i < TILES; i++) {
        if (i < plug_count) {
            const Plug& p = plugs[i];
            char nm[64];
            if (p.ok) snprintf(nm, sizeof(nm), "%s", p.name);
            else      snprintf(nm, sizeof(nm), "%s 離線", p.name);
            style_tile(i, ICON_PLUG, nm, p.ok && p.on, !p.ok ? LV_OPA_50 : p.pending ? LV_OPA_80 : LV_OPA_COVER);
        } else if (i == plug_count) {
            bool on = any_on();
            style_tile(i, ICON_POWER, on ? "全部關閉" : "全部開啟", false, any_ok ? LV_OPA_COVER : LV_OPA_50);
        } else {
            lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void plug_enter(void) { dirty = true; render(); }

static void plug_tick(void) {
    static uint32_t last = 0;
    static int last_ble = -1;
    static bool last_fresh = false;
    // Revert taps the daemon never confirmed.
    for (int i = 0; i < plug_count; i++)
        if (plugs[i].pending && (int32_t)(millis() - plugs[i].pending) > PENDING_MS) {
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
