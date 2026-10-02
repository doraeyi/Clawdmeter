// Now Playing — shows what the computer is playing (YouTube / YouTube Music in
// the browser, or anything else that reports to Windows' media controls) and
// controls it with BLE HID media keys.
//
// Data: the host daemon writes a JSON payload to the now-playing
// characteristic whenever the track/state changes (+ a heartbeat):
//   {"np":1,"ti":"Title","ar":"Artist","app":"Chrome","st":"playing",
//    "pos":83,"dur":245}
//   st = playing | paused | stopped | none       pos/dur in seconds (0 = unknown)
//
// Controls (work even without the daemon — they're plain media keys):
//   ⏮ / ⏯ / ⏭ buttons on screen, PWR button = play/pause.
#include "app.h"
#include "now_playing.h"
#include "../ble.h"
#include "../theme.h"
#include "../hal/board_caps.h"

#include <Arduino.h>
#include <ArduinoJson.h>

LV_FONT_DECLARE(font_cjk_28);
LV_FONT_DECLARE(font_icons_64);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);

#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR < 3
#define NP_LONG_DOTS LV_LABEL_LONG_DOT
#else
#define NP_LONG_DOTS LV_LABEL_LONG_MODE_DOTS
#endif

extern const App APP_NOW_PLAYING;

// ---- State ----
static char     np_title[160]  = "";
static char     np_artist[120] = "";
static char     np_app[32]     = "";
static bool     np_playing     = false;
static bool     np_has_track   = false;
static int      np_pos = 0, np_dur = 0;   // seconds at np_pos_ms
static uint32_t np_pos_ms = 0;
static uint32_t np_last_rx_ms = 0;
static bool     np_dirty = true;

static const uint32_t NP_STALE_MS = 90000;   // daemon heartbeats every ~15 s

// ---- Widgets ----
static lv_obj_t* grp_track;     // everything shown while a track is known
static lv_obj_t* grp_empty;     // "Nothing playing" hint
static lv_obj_t* lbl_empty2;
static lv_obj_t* lbl_source;
static lv_obj_t* lbl_title;
static lv_obj_t* lbl_artist;
static lv_obj_t* bar_prog;
static lv_obj_t* lbl_pos;
static lv_obj_t* lbl_dur;
static lv_obj_t* lbl_play;      // glyph inside the round play button

static lv_obj_t* make_group(lv_obj_t* root) {
    lv_obj_t* g = lv_obj_create(root);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    return g;
}

static lv_obj_t* make_text(lv_obj_t* parent, const lv_font_t* f, lv_color_t c, const char* t) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, t);
    return l;
}

static void fmt_time(char* buf, size_t n, int s) {
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(buf, n, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else           snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static int current_pos(void) {
    int p = np_pos;
    if (np_playing) p += (int)((millis() - np_pos_ms) / 1000);
    if (np_dur > 0 && p > np_dur) p = np_dur;
    return p;
}

// ---- Buttons ----
static void send_key(uint16_t usage) {
    ble_media_key(usage);
    Serial.printf("now playing: media key 0x%04X\n", usage);
}

static void toggle_play(void) {
    send_key(MEDIA_PLAY_PAUSE);
    // Optimistic flip so the icon reacts instantly; the daemon's next payload
    // (≈1 s) confirms or corrects it.
    np_pos = current_pos();
    np_pos_ms = millis();
    np_playing = !np_playing;
    np_dirty = true;
}

static void btn_cb(lv_event_t* e) {
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: send_key(MEDIA_PREV); break;
    case 1: toggle_play();        break;
    case 2: send_key(MEDIA_NEXT); break;
    }
}

static lv_obj_t* make_button(lv_obj_t* parent, const char* glyph, int size, bool filled, int id) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, filled ? THEME_ACCENT : THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(b, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, filled ? lv_color_hex(0xb8603f) : THEME_PANEL, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)id);

    lv_obj_t* g = make_text(b, &font_icons_64, filled ? THEME_BG : THEME_TEXT, glyph);
    lv_obj_center(g);
    return g;
}

// ---- App callbacks ----
static void np_create(lv_obj_t* root) {
    const int W = board_caps().width, H = board_caps().height;
    const int M = H >= 460 ? 40 : 24;
    const int content_w = W - 2 * M;

    // Empty state
    grp_empty = make_group(root);
    lv_obj_t* ic = make_text(grp_empty, &font_icons_64, lv_color_hex(APP_NOW_PLAYING.color), ICON_MUSIC);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -60);
    lv_obj_t* e1 = make_text(grp_empty, &font_cjk_28, THEME_TEXT, "Nothing playing");
    lv_obj_align(e1, LV_ALIGN_CENTER, 0, 20);
    lbl_empty2 = make_text(grp_empty, &font_styrene_20, THEME_DIM, "");
    lv_obj_set_width(lbl_empty2, content_w);
    lv_obj_set_style_text_align(lbl_empty2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty2, LV_ALIGN_CENTER, 0, 70);

    // Track view
    grp_track = make_group(root);

    lbl_source = make_text(grp_track, &font_styrene_16, THEME_DIM, "");
    lv_obj_align(lbl_source, LV_ALIGN_TOP_MID, 0, M);

    lbl_title = make_text(grp_track, &font_cjk_28, THEME_TEXT, "");
    lv_obj_set_width(lbl_title, content_w);
    lv_obj_set_height(lbl_title, 2 * 40);                 // two lines, then "…"
    lv_obj_set_style_text_line_space(lbl_title, -14, 0);  // CJK font has tall metrics
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_title, NP_LONG_DOTS);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, M + 40);

    lbl_artist = make_text(grp_track, &font_cjk_28, THEME_DIM, "");
    lv_obj_set_width(lbl_artist, content_w);
    lv_obj_set_style_text_align(lbl_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_artist, NP_LONG_DOTS);
    lv_obj_align(lbl_artist, LV_ALIGN_TOP_MID, 0, M + 132);

    bar_prog = lv_bar_create(grp_track);
    lv_obj_set_size(bar_prog, content_w, 8);
    lv_obj_align(bar_prog, LV_ALIGN_TOP_MID, 0, M + 196);
    lv_bar_set_range(bar_prog, 0, 1000);
    lv_obj_set_style_bg_color(bar_prog, THEME_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_prog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_prog, THEME_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_prog, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_prog, 4, LV_PART_INDICATOR);

    // Fixed-width time labels so they stay put as the text changes.
    lbl_pos = make_text(grp_track, &font_styrene_16, THEME_DIM, "");
    lv_obj_set_width(lbl_pos, 100);
    lv_obj_align_to(lbl_pos, bar_prog, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lbl_dur = make_text(grp_track, &font_styrene_16, THEME_DIM, "");
    lv_obj_set_width(lbl_dur, 100);
    lv_obj_set_style_text_align(lbl_dur, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align_to(lbl_dur, bar_prog, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 8);

    // Controls row
    const int big = H >= 460 ? 120 : 96, small = H >= 460 ? 96 : 76;
    const int row_y = H >= 460 ? 140 : 110;   // offset of the row center below screen center
    lv_obj_t* prev = make_button(grp_track, ICON_PREV, small, false, 0);
    lbl_play       = make_button(grp_track, ICON_PLAY, big,   true,  1);
    lv_obj_t* next = make_button(grp_track, ICON_NEXT, small, false, 2);
    lv_obj_align(lv_obj_get_parent(lbl_play), LV_ALIGN_CENTER, 0, row_y);
    lv_obj_align(lv_obj_get_parent(prev), LV_ALIGN_CENTER, -(big / 2 + small / 2 + 24), row_y);
    lv_obj_align(lv_obj_get_parent(next), LV_ALIGN_CENTER,  (big / 2 + small / 2 + 24), row_y);
    // Optical centering: the play triangle sits a touch right of the circle center.
    lv_obj_align(lbl_play, LV_ALIGN_CENTER, 4, 0);

    lv_obj_add_flag(grp_track, LV_OBJ_FLAG_HIDDEN);
    np_dirty = true;
}

static void np_refresh(void) {
    const bool fresh = np_last_rx_ms && (millis() - np_last_rx_ms) < NP_STALE_MS;
    const bool show_track = np_has_track && fresh;

    if (show_track) {
        lv_obj_add_flag(grp_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(grp_track, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(grp_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(grp_empty, LV_OBJ_FLAG_HIDDEN);
        if (ble_get_state() != BLE_STATE_CONNECTED)
            lv_label_set_text(lbl_empty2, "Not connected to the computer");
        else if (!fresh)
            lv_label_set_text(lbl_empty2, "Waiting for the computer app\n(buttons still work)");
        else
            lv_label_set_text(lbl_empty2, "Play something on YouTube");
        return;
    }

    if (np_dirty) {
        np_dirty = false;
        lv_label_set_text(lbl_title, np_title);
        lv_label_set_text(lbl_artist, np_artist);
        lv_label_set_text(lbl_source, np_app);
        lv_label_set_text(lbl_play, np_playing ? ICON_PAUSE : ICON_PLAY);
        lv_obj_align(lbl_play, LV_ALIGN_CENTER, np_playing ? 0 : 4, 0);
    }

    if (np_dur > 0) {
        const int p = current_pos();
        lv_obj_clear_flag(bar_prog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_pos, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_dur, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(bar_prog, (int32_t)((int64_t)p * 1000 / np_dur), LV_ANIM_OFF);
        char b[16];
        fmt_time(b, sizeof(b), p);      lv_label_set_text(lbl_pos, b);
        fmt_time(b, sizeof(b), np_dur); lv_label_set_text(lbl_dur, b);
    } else {
        lv_obj_add_flag(bar_prog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_pos, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_dur, LV_OBJ_FLAG_HIDDEN);
    }
}

static void np_enter(void) { np_dirty = true; np_refresh(); }

static void np_tick(void) {
    static uint32_t last = 0;
    if (!np_dirty && millis() - last < 500) return;   // progress ticks twice a second
    last = millis();
    np_refresh();
}

static bool np_on_pwr(void) { toggle_play(); return true; }

void now_playing_on_payload(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("now playing: bad JSON");
        return;
    }
    const char* st = doc["st"] | "none";
    strlcpy(np_title,  doc["ti"]  | "", sizeof(np_title));
    strlcpy(np_artist, doc["ar"]  | "", sizeof(np_artist));
    strlcpy(np_app,    doc["app"] | "", sizeof(np_app));
    np_playing   = strcmp(st, "playing") == 0;
    np_has_track = np_title[0] && strcmp(st, "none") != 0 && strcmp(st, "stopped") != 0;
    np_pos       = doc["pos"] | 0;
    np_dur       = doc["dur"] | 0;
    np_pos_ms    = millis();
    np_last_rx_ms = millis();
    if (np_last_rx_ms == 0) np_last_rx_ms = 1;
    np_dirty = true;
}

extern const App APP_NOW_PLAYING = {
    /*name*/       "Now Playing",
    /*icon_glyph*/ ICON_MUSIC,
    /*icon_img*/   nullptr,
    /*color*/      0xc46686,
    /*create*/     np_create,
    /*enter*/      np_enter,
    /*leave*/      nullptr,
    /*tick*/       np_tick,
    /*on_pwr*/     np_on_pwr,
};
