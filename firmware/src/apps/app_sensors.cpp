// 感測器 Sensors — QMI8658 accelerometer: bubble level + live X/Y/Z and tilt.
// (Waveshare example 04_LVGL_QMI8658_ui.)
#include "app.h"
#include "../theme.h"
#include "../hal/board_caps.h"
#include "../hal/hal_extras.h"

#include <Arduino.h>
#include <math.h>

LV_FONT_DECLARE(font_cjk_28);
LV_FONT_DECLARE(font_styrene_24);

extern const App APP_SENSORS;

static lv_obj_t *dish, *bubble, *lbl_xyz, *lbl_tilt, *lbl_none;
static int dish_r, bubble_r;
static float fx = 0, fy = 0;   // smoothed bubble offset

static void sensors_create(lv_obj_t* root) {
    const int H = board_caps().height;
    app_make_title(root, app_label(&APP_SENSORS));

    dish_r = H >= 460 ? 120 : 90;
    bubble_r = dish_r / 5;

    dish = lv_obj_create(root);
    lv_obj_set_size(dish, dish_r * 2, dish_r * 2);
    lv_obj_align(dish, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(dish, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dish, THEME_PANEL, 0);
    lv_obj_set_style_border_color(dish, THEME_BAR_BG, 0);
    lv_obj_set_style_border_width(dish, 3, 0);
    lv_obj_set_style_pad_all(dish, 0, 0);
    lv_obj_clear_flag(dish, LV_OBJ_FLAG_SCROLLABLE);

    // Crosshair + center ring
    for (int i = 0; i < 2; i++) {
        lv_obj_t* l = lv_obj_create(dish);
        lv_obj_remove_style_all(l);
        lv_obj_set_style_bg_color(l, THEME_BAR_BG, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_size(l, i ? 2 : dish_r * 2, i ? dish_r * 2 : 2);
        lv_obj_center(l);
    }
    lv_obj_t* ring = lv_obj_create(dish);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, bubble_r * 2 + 12, bubble_r * 2 + 12);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(ring, THEME_DIM, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_center(ring);

    bubble = lv_obj_create(dish);
    lv_obj_remove_style_all(bubble);
    lv_obj_set_size(bubble, bubble_r * 2, bubble_r * 2);
    lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bubble, THEME_GREEN, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_center(bubble);

    lbl_tilt = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_tilt, &font_cjk_28, 0);
    lv_obj_set_style_text_color(lbl_tilt, THEME_TEXT, 0);
    lv_label_set_text(lbl_tilt, "");
    lv_obj_align_to(lbl_tilt, dish, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);

    lbl_xyz = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_xyz, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_xyz, THEME_DIM, 0);
    lv_label_set_text(lbl_xyz, "");
    lv_obj_align(lbl_xyz, LV_ALIGN_BOTTOM_MID, 0, -30);

    lbl_none = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_none, &font_cjk_28, 0);
    lv_obj_set_style_text_color(lbl_none, THEME_DIM, 0);
    lv_label_set_text(lbl_none, "這塊板子沒有加速度計");
    lv_obj_center(lbl_none);
    lv_obj_add_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);
}

static void sensors_enter(void) {
    imu_hal_app_mode(true);
    fx = fy = 0;
}

static void sensors_leave(void) { imu_hal_app_mode(false); }

static void sensors_tick(void) {
    static uint32_t last = 0;
    if (millis() - last < 33) return;
    last = millis();

    float ax, ay, az;
    if (!imu_hal_accel(&ax, &ay, &az)) {
        lv_obj_add_flag(dish, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(dish, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);

    // Bubble moves opposite to gravity's in-plane component (like a real level).
    const float lim = dish_r - bubble_r - 4;
    float tx = -ay * lim * 1.6f, ty = -ax * lim * 1.6f;
    float d = sqrtf(tx * tx + ty * ty);
    if (d > lim) { tx *= lim / d; ty *= lim / d; }
    fx += (tx - fx) * 0.35f;
    fy += (ty - fy) * 0.35f;
    lv_obj_align(bubble, LV_ALIGN_CENTER, (int)fx, (int)fy);

    float pitch = atan2f(ax, sqrtf(ay * ay + az * az)) * 57.2958f;
    float roll  = atan2f(ay, sqrtf(ax * ax + az * az)) * 57.2958f;
    bool level = fabsf(pitch) < 1.5f && fabsf(roll) < 1.5f;
    lv_obj_set_style_bg_color(bubble, level ? THEME_GREEN : THEME_ACCENT, 0);

    { char _b[64]; snprintf(_b, sizeof(_b), level ? "水平" : "傾斜 %.0f° / %.0f°", pitch, roll); lv_label_set_text(lbl_tilt, _b); }
    lv_obj_align_to(lbl_tilt, dish, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);
    { char _b[64]; snprintf(_b, sizeof(_b), "X %+.2fg   Y %+.2fg   Z %+.2fg", ax, ay, az); lv_label_set_text(lbl_xyz, _b); }
}

extern const App APP_SENSORS = {
    /*name*/       "Sensors",
    /*icon_glyph*/ ICON_COMPASS,
    /*icon_img*/   nullptr,
    /*color*/      0x6ab0de,
    /*create*/     sensors_create,
    /*enter*/      sensors_enter,
    /*leave*/      sensors_leave,
    /*tick*/       sensors_tick,
    /*on_pwr*/     nullptr,
    /*label*/      "感測器",
};
