// 設定 Settings — device information and a few controls.
//   儲存空間  firmware / flash / PSRAM / RAM / settings storage (NVS)
//   電池      percent, charging state, battery / USB / system voltage, PMU temp
//   藍牙      connection state, device name, MAC
//   顯示      brightness (4 levels)
//   聲音      play the test chime
//   關於      chip, CPU, chip temperature, uptime, firmware build
#include "app.h"
#include "sysinfo.h"
#include "../ble.h"
#include "../theme.h"
#include "../brightness.h"
#include "../hal/board_caps.h"
#include "../hal/hal_extras.h"
#include "../hal/sound_hal.h"

#include <Arduino.h>

LV_FONT_DECLARE(font_cjk_28);
LV_FONT_DECLARE(font_styrene_20);

extern const App APP_SETTINGS;

static lv_obj_t* list;   // scrolling column of cards

// Storage
static lv_obj_t *v_fw, *b_fw, *v_flash, *v_psram, *b_psram, *v_ram, *b_ram, *v_nvs, *v_lvgl, *v_largest;
// Battery
static lv_obj_t *v_bpct, *b_bpct, *v_bstate, *v_bmv, *v_vbus, *v_sys, *v_ptemp;
static lv_obj_t* card_batt;
// Bluetooth
static lv_obj_t *v_ble, *v_name, *v_mac;
// Display
static lv_obj_t* brt_btns[8];
static int       brt_count = 0;
// About
static lv_obj_t *v_chip, *v_cpu, *v_ctemp, *v_up, *v_build;

static const int ROW_H = 50;

// ---- builders ----
static lv_obj_t* card(const char* title) {
    lv_obj_t* hdr = lv_label_create(list);
    lv_obj_set_style_text_font(hdr, &font_cjk_28, 0);
    lv_obj_set_style_text_color(hdr, THEME_ACCENT, 0);
    lv_label_set_text(hdr, title);
    lv_obj_set_style_pad_left(hdr, 12, 0);

    lv_obj_t* c = lv_obj_create(list);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_hor(c, 18, 0);
    lv_obj_set_style_pad_ver(c, 6, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_EVENT_BUBBLE);
    return c;
}

static lv_obj_t* text(lv_obj_t* parent, lv_color_t col, const char* t) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &font_cjk_28, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, t);
    return l;
}

// One "label ........ value" row; returns the value label.
static lv_obj_t* row(lv_obj_t* c, const char* label) {
    lv_obj_t* r = lv_obj_create(c);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t* l = text(r, THEME_DIM, label);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* v = text(r, THEME_TEXT, "-");
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    return v;
}

static lv_obj_t* bar(lv_obj_t* c) {
    lv_obj_t* b = lv_bar_create(c);
    lv_obj_set_size(b, LV_PCT(100), 10);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_bg_color(b, THEME_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, THEME_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 5, LV_PART_INDICATOR);
    lv_obj_set_style_margin_bottom(b, 12, 0);
    return b;
}

static void set_bar(lv_obj_t* b, uint32_t used, uint32_t total) {
    if (!total) { lv_bar_set_value(b, 0, LV_ANIM_OFF); return; }
    uint32_t v = (uint32_t)((uint64_t)used * 1000 / total);
    lv_bar_set_value(b, v, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, v >= 900 ? THEME_RED : v >= 750 ? THEME_AMBER : THEME_GREEN,
                              LV_PART_INDICATOR);
}

static void fmt_bytes(char* out, size_t n, uint32_t b) {
    if (b >= (1u << 20))      snprintf(out, n, "%.1f MB", b / 1048576.0);
    else if (b >= (1u << 10)) snprintf(out, n, "%u KB", (unsigned)(b / 1024));
    else                      snprintf(out, n, "%u B", (unsigned)b);
}

static void fmt_used(lv_obj_t* lbl, uint32_t used, uint32_t total) {
    char a[16], b[16];
    fmt_bytes(a, sizeof(a), used);
    fmt_bytes(b, sizeof(b), total);
    lv_label_set_text_fmt(lbl, "%s / %s", a, b);
}

// ---- controls ----
static void brt_cb(lv_event_t* e) {
    brightness_set_index((int)(intptr_t)lv_event_get_user_data(e));
    for (int i = 0; i < brt_count; i++) {
        bool on = i == brightness_get_index();
        lv_obj_set_style_bg_color(brt_btns[i], on ? THEME_ACCENT : THEME_BAR_BG, 0);
    }
}

static void sound_cb(lv_event_t*) { sound_hal_play_reset(); }

static bool perf_on = false;
static void perf_cb(lv_event_t* e) {
#if LV_USE_PERF_MONITOR
    perf_on = !perf_on;
    if (perf_on) lv_sysmon_show_performance(NULL);
    else         lv_sysmon_hide_performance(NULL);
    lv_obj_set_style_bg_color((lv_obj_t*)lv_event_get_target(e), perf_on ? THEME_ACCENT : THEME_BAR_BG, 0);
#else
    (void)e;
#endif
}

static lv_obj_t* pill_button(lv_obj_t* parent, const char* label, lv_event_cb_t cb, void* ud) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_set_height(b, 56);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, THEME_BAR_BG, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, THEME_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t* l = text(b, THEME_TEXT, label);
    lv_obj_center(l);
    return b;
}

// ---- refresh ----
static void refresh(void) {
    char buf[48];
    SysInfo si;
    sysinfo_read(&si);

    // Storage
    fmt_used(v_fw, si.app_used, si.app_part);       set_bar(b_fw, si.app_used, si.app_part);
    fmt_bytes(buf, sizeof(buf), si.flash_total);    lv_label_set_text(v_flash, buf);
    if (si.psram_total) {
        fmt_used(v_psram, si.psram_total - si.psram_free, si.psram_total);
        set_bar(b_psram, si.psram_total - si.psram_free, si.psram_total);
    } else {
        lv_label_set_text(v_psram, "無");
        lv_bar_set_value(b_psram, 0, LV_ANIM_OFF);
    }
    fmt_used(v_ram, si.heap_total - si.heap_free, si.heap_total);
    set_bar(b_ram, si.heap_total - si.heap_free, si.heap_total);
    fmt_bytes(buf, sizeof(buf), si.heap_largest);  lv_label_set_text(v_largest, buf);
    fmt_bytes(buf, sizeof(buf), si.lvgl_used);     lv_label_set_text(v_lvgl, buf);
    if (si.nvs_total) lv_label_set_text_fmt(v_nvs, "%u / %u 筆", (unsigned)si.nvs_used, (unsigned)si.nvs_total);

    // Battery
    PowerDetails pd;
    if (power_hal_details(&pd)) {
        if (pd.batt_present && pd.pct >= 0) {
            lv_label_set_text_fmt(v_bpct, "%d%%", pd.pct);
            lv_bar_set_value(b_bpct, pd.pct * 10, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(b_bpct, pd.pct <= 15 ? THEME_RED : pd.pct <= 35 ? THEME_AMBER : THEME_GREEN,
                                      LV_PART_INDICATOR);
        } else {
            lv_label_set_text(v_bpct, "沒有電池");
            lv_bar_set_value(b_bpct, 0, LV_ANIM_OFF);
        }
        lv_label_set_text(v_bstate, pd.charging ? "充電中" : pd.vbus_in ? "USB 供電" : "使用電池");
        if (pd.batt_mv) { char _b[64]; snprintf(_b, sizeof(_b), "%.2f V", pd.batt_mv / 1000.0); lv_label_set_text(v_bmv, _b); }
        else            lv_label_set_text(v_bmv, "-");
        if (pd.vbus_mv) { char _b[64]; snprintf(_b, sizeof(_b), "%.2f V", pd.vbus_mv / 1000.0); lv_label_set_text(v_vbus, _b); }
        else            lv_label_set_text(v_vbus, "未接");
        if (pd.sys_mv)  { char _b[64]; snprintf(_b, sizeof(_b), "%.2f V", pd.sys_mv / 1000.0); lv_label_set_text(v_sys, _b); }
        if (pd.pmu_temp_c > -100) { char _b[64]; snprintf(_b, sizeof(_b), "%.1f °C", pd.pmu_temp_c); lv_label_set_text(v_ptemp, _b); }
    } else if (card_batt) {
        lv_label_set_text(v_bpct, "不支援");
    }

    // Bluetooth
    ble_state_t bs = ble_get_state();
    lv_label_set_text(v_ble, bs == BLE_STATE_CONNECTED ? "已連線" :
                             bs == BLE_STATE_ADVERTISING ? "等待連線" : "未連線");
    lv_obj_set_style_text_color(v_ble, bs == BLE_STATE_CONNECTED ? THEME_GREEN : THEME_TEXT, 0);
    lv_label_set_text(v_name, ble_get_device_name());
    lv_label_set_text(v_mac, ble_get_mac_address());

    // About
    lv_label_set_text_fmt(v_chip, "%s rev %d", si.chip, si.chip_rev);
    lv_label_set_text_fmt(v_cpu, "%d 核 %d MHz", si.cores, si.cpu_mhz);
    if (si.chip_temp_c > -100) { char _b[64]; snprintf(_b, sizeof(_b), "%.1f °C", si.chip_temp_c); lv_label_set_text(v_ctemp, _b); }
    uint32_t u = si.uptime_s;
    if (u >= 86400) lv_label_set_text_fmt(v_up, "%u 天 %u 小時", (unsigned)(u / 86400), (unsigned)(u % 86400 / 3600));
    else            lv_label_set_text_fmt(v_up, "%02u:%02u:%02u", (unsigned)(u / 3600), (unsigned)(u / 60 % 60), (unsigned)(u % 60));
}

// ---- App callbacks ----
static void settings_create(lv_obj_t* root) {
    const int W = board_caps().width, H = board_caps().height;
    const int M = H >= 460 ? 24 : 12;

    app_make_title(root, app_label(&APP_SETTINGS));

    list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, W, H - 80);
    lv_obj_set_pos(list, 0, 80);
    lv_obj_set_style_pad_hor(list, M, 0);
    lv_obj_set_style_pad_bottom(list, 40, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* c = card("儲存空間");
    v_fw = row(c, "韌體");        b_fw = bar(c);
    v_psram = row(c, "PSRAM");     b_psram = bar(c);
    v_ram = row(c, "記憶體");      b_ram = bar(c);
    v_largest = row(c, "最大可用區塊");
    v_lvgl = row(c, "畫面使用");
    v_flash = row(c, "Flash 容量");
    v_nvs = row(c, "設定儲存");

    card_batt = c = card("電池");
    v_bpct = row(c, "電量");       b_bpct = bar(c);
    v_bstate = row(c, "狀態");
    v_bmv = row(c, "電池電壓");
    v_vbus = row(c, "USB 電壓");
    v_sys = row(c, "系統電壓");
    v_ptemp = row(c, "電源晶片溫度");

    c = card("藍牙");
    v_ble = row(c, "狀態");
    v_name = row(c, "名稱");
    v_mac = row(c, "MAC");

    c = card("顯示亮度");
    lv_obj_t* brow = lv_obj_create(c);
    lv_obj_remove_style_all(brow);
    lv_obj_set_size(brow, LV_PCT(100), 76);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brow, 8, 0);
    lv_obj_add_flag(brow, LV_OBJ_FLAG_EVENT_BUBBLE);
    brt_count = brightness_level_count();
    if (brt_count > 8) brt_count = 8;
    static const char* const NAMES[] = {"暗", "中", "亮", "最亮", "5", "6", "7", "8"};
    for (int i = 0; i < brt_count; i++) {
        brt_btns[i] = pill_button(brow, NAMES[i], brt_cb, (void*)(intptr_t)i);
        lv_obj_set_width(brt_btns[i], 1);          // let flex-grow share the row evenly
        lv_obj_set_flex_grow(brt_btns[i], 1);
    }

    lv_obj_t* pb = pill_button(c, "顯示 FPS", perf_cb, nullptr);
    lv_obj_set_width(pb, LV_PCT(100));
    lv_obj_set_style_margin_bottom(pb, 10, 0);

    c = card("聲音");
    lv_obj_t* sb = pill_button(c, "播放測試音", sound_cb, nullptr);
    lv_obj_set_width(sb, LV_PCT(100));
    lv_obj_set_style_margin_ver(sb, 10, 0);

    c = card("關於");
    v_chip = row(c, "晶片");
    v_cpu = row(c, "處理器");
    v_ctemp = row(c, "晶片溫度");
    v_up = row(c, "開機時間");
    v_build = row(c, "韌體版本");
    lv_label_set_text(v_build, __DATE__);
    lv_obj_t* bn = row(c, "板子");
    lv_label_set_text(bn, board_caps().name);
    lv_obj_set_style_text_font(bn, &font_styrene_20, 0);

    if (!board_caps().has_battery) {
        // Hide the battery card (header is the sibling just before it).
        lv_obj_add_flag(card_batt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lv_obj_get_sibling(card_batt, -1), LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_enter(void) {
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    for (int i = 0; i < brt_count; i++)
        lv_obj_set_style_bg_color(brt_btns[i], i == brightness_get_index() ? THEME_ACCENT : THEME_BAR_BG, 0);
    refresh();
}

static void settings_tick(void) {
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();
    refresh();
}

extern const App APP_SETTINGS = {
    /*name*/       "Settings",
    /*icon_glyph*/ ICON_GEAR,
    /*icon_img*/   nullptr,
    /*color*/      0xb0aea5,
    /*create*/     settings_create,
    /*enter*/      settings_enter,
    /*leave*/      nullptr,
    /*tick*/       settings_tick,
    /*on_pwr*/     nullptr,
    /*label*/      "設定",
};
