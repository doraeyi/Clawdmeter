#include "../idle.h"
#include "app_manager.h"
#include "app_registry.h"
#include "../theme.h"
#include "../brightness.h"
#include "../hal/board_caps.h"
#include "../hal/power_hal.h"
#include "../ble.h"

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

LV_FONT_DECLARE(font_clock_120);
LV_FONT_DECLARE(font_icons_64);
LV_FONT_DECLARE(font_cjk_28);
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);

// ---------------------------------------------------------------------------
// Layout (derived from the panel size, same breakpoints as ui.cpp)
// ---------------------------------------------------------------------------
struct LauncherLayout {
    int16_t w, h;
    int16_t margin;
    const lv_font_t* clock_font;
    const lv_font_t* date_font;
    const lv_font_t* hint_font;
    const lv_font_t* title_font;
    const lv_font_t* tile_font;
    int16_t title_y;
    int16_t grid_y;
    int16_t tile_gap;
    int16_t bar_w, bar_h, bar_bottom;   // the "home bar" pill
    int16_t swipe_px;                   // min travel for a swipe
    int16_t bottom_zone;                // height of the swipe-up start zone
};
static LauncherLayout LL;

static void compute_launcher_layout(void) {
    const BoardCaps& c = board_caps();
    LL.w = c.width;
    LL.h = c.height;
    if (c.height >= 300) {
        LL.margin      = c.height >= 460 ? 36 : 24;
        LL.clock_font  = c.width >= 360 ? &font_clock_120 : &font_tiempos_56;
        LL.date_font   = &font_styrene_28;
        LL.hint_font   = &font_styrene_16;
        LL.title_font  = c.height >= 460 ? &font_tiempos_56 : &font_tiempos_34;
        LL.tile_font   = c.height >= 460 ? &font_styrene_24 : &font_styrene_20;
        LL.title_y     = c.height >= 460 ? 30 : 24;
        LL.grid_y      = c.height >= 460 ? 112 : 84;
        LL.tile_gap    = 16;
        LL.bar_w = 120; LL.bar_h = 6; LL.bar_bottom = 4;
        LL.bottom_zone = 40;   // only the strip around the home bar; content can scroll above it
    } else {
        LL.margin      = 10;
        LL.clock_font  = &font_styrene_48;
        LL.date_font   = &font_styrene_16;
        LL.hint_font   = &font_styrene_14;
        LL.title_font  = &font_tiempos_34;
        LL.tile_font   = &font_styrene_14;
        LL.title_y     = 4;
        LL.grid_y      = 48;
        LL.tile_gap    = 8;
        LL.bar_w = 70; LL.bar_h = 4; LL.bar_bottom = 3;
        LL.bottom_zone = 28;
    }
    LL.swipe_px = c.width / 6;   // 80 px on 480
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static app_view_t view = VIEW_HOME;
static int        cur_app = -1;

static lv_obj_t* home_root;
static lv_obj_t* lbl_time;
static lv_obj_t* lbl_date;
static lv_obj_t* lbl_batt;
static lv_obj_t* launcher_root;
static lv_obj_t* home_bar;
static lv_obj_t* app_roots[16];

// Wall clock fed by the daemon payload ("t" = local epoch). Advanced locally
// between payloads. 0 = unknown (daemon clock option off / not connected yet).
static long     clock_epoch = 0;
static uint32_t clock_base_ms = 0;
static int      clock_fmt = 24;
static int      clock_last_min = -2;
static bool     got_payload = false;   // any daemon payload since boot
static int      hint_state = -1;

// ---------------------------------------------------------------------------
// Small widget helpers
// ---------------------------------------------------------------------------
static lv_obj_t* make_fullscreen(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, LL.w, LL.h);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_color(o, THEME_BG, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* f, lv_color_t col, const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, txt);
    return l;
}

static bool is_ascii(const char* s) {
    for (; *s; s++) if ((unsigned char)*s >= 0x80) return false;
    return true;
}

lv_obj_t* app_make_title(lv_obj_t* root, const char* text) {
    // The serif title font is Latin-only; CJK titles use the CJK font.
    lv_obj_t* t = make_label(root, is_ascii(text) ? LL.title_font : &font_cjk_28, THEME_TEXT, text);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, LL.title_y);
    return t;
}

void app_make_placeholder(lv_obj_t* root, const App* app, const char* line) {
    app_make_title(root, app_label(app));
    if (app->icon_glyph) {
        lv_obj_t* ic = make_label(root, &font_icons_64, lv_color_hex(app->color), app->icon_glyph);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, -10);
    }
    lv_obj_t* l = make_label(root, LL.date_font, THEME_DIM, line);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 70);
}

// ---------------------------------------------------------------------------
// Home (clock)
// ---------------------------------------------------------------------------
static const char* const WDAY[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* const MON[]  = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};

static void build_home(lv_obj_t* scr) {
    home_root = make_fullscreen(scr);

    lbl_time = make_label(home_root, LL.clock_font, THEME_TEXT, "--:--");
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -40);

    lbl_date = make_label(home_root, LL.date_font, THEME_DIM, "Waiting for time sync");
    lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    lbl_batt = make_label(home_root, LL.hint_font, THEME_DIM, "");
    lv_obj_align(lbl_batt, LV_ALIGN_TOP_RIGHT, -LL.margin, LL.margin / 2 + 8);
    if (!board_caps().has_battery) lv_obj_add_flag(lbl_batt, LV_OBJ_FLAG_HIDDEN);

    // Page dots: home ● ○ apps
    lv_obj_t* dots = lv_obj_create(home_root);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, 40, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -LL.margin);
    for (int i = 0; i < 2; i++) {
        lv_obj_t* d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_pos(d, i * 24 + 3, 1);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, i == 0 ? THEME_TEXT : THEME_BAR_BG, 0);
    }

    lv_obj_t* hint = make_label(home_root, LL.hint_font, THEME_DIM, "Swipe for apps / double-tap to sleep");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -LL.margin - 22);
}

static void update_clock(bool force) {
    if (clock_epoch <= 0) {
        // Say why there's no time: no BLE link, no daemon data yet, or the
        // daemon is running with its clock option off.
        int st = ble_get_state() != BLE_STATE_CONNECTED ? 0 : !got_payload ? 1 : 2;
        if (force || clock_last_min != -1 || st != hint_state) {
            clock_last_min = -1;
            hint_state = st;
            lv_label_set_text(lbl_time, "--:--");
            lv_label_set_text(lbl_date, st == 0 ? "Bluetooth not connected"
                                      : st == 1 ? "Waiting for the computer app"
                                                : "Clock is off in the computer app");
            lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
        }
        return;
    }
    time_t cur = (time_t)(clock_epoch + (lv_tick_get() - clock_base_ms) / 1000);
    struct tm tmv;
    gmtime_r(&cur, &tmv);   // epoch is already local wall-clock time
    if (!force && tmv.tm_min == clock_last_min) return;
    clock_last_min = tmv.tm_min;

    char buf[24];
    if (clock_fmt == 12) {
        int h12 = tmv.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        snprintf(buf, sizeof(buf), "%d:%02d", h12, tmv.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    }
    lv_label_set_text(lbl_time, buf);

    if (clock_fmt == 12) {
        snprintf(buf, sizeof(buf), "%s, %s %d  %s", WDAY[tmv.tm_wday], MON[tmv.tm_mon],
                 tmv.tm_mday, tmv.tm_hour < 12 ? "AM" : "PM");
    } else {
        snprintf(buf, sizeof(buf), "%s, %s %d", WDAY[tmv.tm_wday], MON[tmv.tm_mon], tmv.tm_mday);
    }
    lv_label_set_text(lbl_date, buf);
    lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
}

static void update_battery_label(void) {
    if (!board_caps().has_battery) return;
    static int last = -100;
    static bool last_chg = false;
    int pct = power_hal_battery_pct();
    bool chg = power_hal_is_charging();
    if (pct == last && chg == last_chg) return;
    last = pct; last_chg = chg;
    if (pct < 0) lv_label_set_text(lbl_batt, "");
    else         lv_label_set_text_fmt(lbl_batt, "%s%d%%", chg ? "+" : "", pct);
}

// ---------------------------------------------------------------------------
// Launcher (app grid)
// ---------------------------------------------------------------------------
static void tile_click_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    app_manager_open_app(idx);
}

static void build_launcher(lv_obj_t* scr) {
    launcher_root = make_fullscreen(scr);

    lv_obj_t* t = make_label(launcher_root, LL.title_font, THEME_TEXT, "Apps");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, LL.title_y);

    // Scrollable grid: as many apps as you like, scroll vertically.
    const bool small = LL.w < 300;
    const int cols = small ? 2 : 3;
    const int gap = LL.tile_gap;
    const int grid_w = LL.w - 2 * LL.margin;
    const int tile_w = (grid_w - (cols - 1) * gap) / cols;
    const int tile_h = tile_w + (small ? 0 : 12);
    const int grid_h = LL.h - LL.grid_y - LL.bar_bottom - LL.bar_h - 6;

    lv_obj_t* grid = lv_obj_create(launcher_root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LL.w, grid_h);
    lv_obj_set_pos(grid, 0, LL.grid_y);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(grid, gap, 0);

    for (int i = 0; i < APP_COUNT && i < 16; i++) {
        const App* app = APPS[i];
        const int r = i / cols, col = i % cols;

        lv_obj_t* tile = lv_obj_create(grid);
        lv_obj_set_size(tile, tile_w, tile_h);
        lv_obj_set_pos(tile, LL.margin + col * (tile_w + gap), r * (tile_h + gap));
        lv_obj_set_style_bg_color(tile, THEME_PANEL, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, THEME_BAR_BG, LV_STATE_PRESSED);
        lv_obj_set_style_radius(tile, 20, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(tile, tile_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* icon;
        if (app->icon_glyph) {
            icon = make_label(tile, &font_icons_64, lv_color_hex(app->color), app->icon_glyph);
        } else {
            icon = lv_image_create(tile);
            if (app->icon_img) lv_image_set_src(icon, app->icon_img);
        }
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, small ? -12 : -18);

        lv_obj_t* name = make_label(tile, small ? LL.tile_font : &font_cjk_28, THEME_TEXT, app_label(app));
        lv_obj_set_style_text_line_space(name, -14, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, small ? -4 : 0);
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
static void show_only(lv_obj_t* keep) {
    lv_obj_add_flag(home_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(launcher_root, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < APP_COUNT && i < 16; i++)
        if (app_roots[i]) lv_obj_add_flag(app_roots[i], LV_OBJ_FLAG_HIDDEN);
    if (keep) {
        lv_obj_clear_flag(keep, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(keep);
    }
}

static void leave_current_app(void) {
    if (view == VIEW_APP && cur_app >= 0) {
        const App* a = APPS[cur_app];
        if (a->leave) a->leave();
    }
    cur_app = -1;
}

static void set_home_bar(bool visible) {
    if (visible) lv_obj_clear_flag(home_bar, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(home_bar, LV_OBJ_FLAG_HIDDEN);
}

void app_manager_go_home(void) {
    leave_current_app();
    show_only(home_root);
    view = VIEW_HOME;
    set_home_bar(false);
    update_clock(true);
}

void app_manager_open_launcher(void) {
    leave_current_app();
    show_only(launcher_root);
    view = VIEW_LAUNCHER;
    set_home_bar(true);
}

void app_manager_open_app(int index) {
    if (index < 0 || index >= APP_COUNT) return;
    leave_current_app();
    show_only(app_roots[index]);   // NULL root (Claude) → everything of ours hidden
    view = VIEW_APP;
    cur_app = index;
    set_home_bar(true);
    const App* a = APPS[index];
    Serial.printf("app: open %s\n", a->name);
    if (a->enter) a->enter();
}

app_view_t app_manager_view(void) { return view; }

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------
static bool     g_down = false;
static bool     g_fired = false;
static int      g_sx, g_sy;

// Double-tap on the home clock → screen off until the PWR button is pressed.
static uint32_t tap_down_ms = 0, last_tap_ms = 0;
static int last_x = 0, last_y = 0, last_tap_x = 0, last_tap_y = 0;

static void note_tap_release(void) {
    const uint32_t now = lv_tick_get();
    const int mdx = last_x - g_sx, mdy = last_y - g_sy;
    const bool is_tap = !g_fired && now - tap_down_ms < 300 && mdx * mdx + mdy * mdy < 20 * 20;
    if (!is_tap || view != VIEW_HOME) { last_tap_ms = 0; return; }
    const int ddx = g_sx - last_tap_x, ddy = g_sy - last_tap_y;
    if (last_tap_ms && now - last_tap_ms < 400 && ddx * ddx + ddy * ddy < 60 * 60) {
        last_tap_ms = 0;
        idle_sleep_now();
        return;
    }
    last_tap_ms = now;
    last_tap_x = g_sx; last_tap_y = g_sy;
}

bool app_manager_touch(bool pressed, int x, int y) {
    if (pressed) { last_x = x; last_y = y; }
    if (!pressed) {
        if (g_down) note_tap_release();
        g_down = false;
        g_fired = false;
        return false;
    }
    if (!g_down) {           // press edge
        g_down = true;
        tap_down_ms = lv_tick_get();
        g_fired = false;
        g_sx = x; g_sy = y;
        return false;
    }
    if (g_fired) return true;

    const int dx = x - g_sx, dy = y - g_sy;
    const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const int S = LL.swipe_px;

    // Swipe up from the bottom bar → home (from anywhere but home).
    if (view != VIEW_HOME && g_sy >= LL.h - LL.bottom_zone && -dy >= S && ady > adx) {
        g_fired = true;
        app_manager_go_home();
        return true;
    }
    // Home ↔ launcher with a horizontal swipe.
    if ((view == VIEW_HOME || view == VIEW_LAUNCHER) && adx >= S && adx > 2 * ady) {
        g_fired = true;
        if (view == VIEW_HOME) app_manager_open_launcher();
        else                   app_manager_go_home();
        return true;
    }
    // Inside an app: swipe right from the left edge → back to the launcher.
    if (view == VIEW_APP && g_sx <= 30 && dx >= S && adx > 2 * ady) {
        g_fired = true;
        app_manager_open_launcher();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Buttons / data
// ---------------------------------------------------------------------------
void app_manager_on_pwr(void) {
    if (view == VIEW_APP && cur_app >= 0) {
        const App* a = APPS[cur_app];
        if (a->on_pwr && a->on_pwr()) return;
    }
    brightness_cycle();
}

void app_manager_on_usage(const UsageData* d) {
    if (!d->valid) return;
    got_payload = true;
    if (!d->ok) return;
    if (d->clock_epoch > 0) {
        clock_epoch   = d->clock_epoch;
        clock_base_ms = lv_tick_get();
        clock_fmt     = d->clock_fmt;
        update_clock(true);
    }
}

// ---------------------------------------------------------------------------
// Init / tick
// ---------------------------------------------------------------------------
void app_manager_init(void) {
    compute_launcher_layout();
    lv_obj_t* scr = lv_screen_active();

    build_home(scr);
    build_launcher(scr);

    for (int i = 0; i < APP_COUNT && i < 16; i++) {
        const App* a = APPS[i];
        app_roots[i] = nullptr;
        if (a->create) {
            // Apps that pass through to an existing screen (Claude) skip the
            // root by leaving create() NULL.
            app_roots[i] = make_fullscreen(scr);
            a->create(app_roots[i]);
        }
    }

    // Home bar lives on the top layer so it floats above every app.
    home_bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(home_bar);
    lv_obj_set_size(home_bar, LL.bar_w, LL.bar_h);
    lv_obj_align(home_bar, LV_ALIGN_BOTTOM_MID, 0, -LL.bar_bottom);
    lv_obj_set_style_radius(home_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(home_bar, THEME_DIM, 0);
    lv_obj_set_style_bg_opa(home_bar, LV_OPA_70, 0);
    lv_obj_clear_flag(home_bar, LV_OBJ_FLAG_CLICKABLE);

    update_battery_label();
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(NULL);   // off by default; Settings → 顯示 FPS
#endif
    app_manager_go_home();
}

void app_manager_tick(void) {
    if (view == VIEW_HOME) {
        update_clock(false);
        update_battery_label();
    } else if (view == VIEW_APP && cur_app >= 0) {
        const App* a = APPS[cur_app];
        if (a->tick) a->tick();
    }
}
