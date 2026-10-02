// 重力球 Tilt ball — tilt the board to roll the ball into the targets.
// (Inspired by Waveshare example 04_Immersive_block.)
// PWR button = restart.
#include "app.h"
#include "../theme.h"
#include "../hal/board_caps.h"
#include "../hal/hal_extras.h"
#include "../hal/imu_hal.h"

#include <Arduino.h>
#include <math.h>

LV_FONT_DECLARE(font_cjk_28);
LV_FONT_DECLARE(font_styrene_48);

extern const App APP_TILT;

// Sensor axes → screen axes at rotation 0. If the ball rolls the wrong way
// left/right on your board, flip TILT_FLIP_X; up/down → TILT_FLIP_Y.
#define TILT_FLIP_X 0
#define TILT_FLIP_Y 0

static lv_obj_t *field, *ball, *target, *lbl_score, *lbl_best, *lbl_none;
static int W, H, ball_r, target_r, field_top;
static float bx, by, vx, vy;
static int score = 0, best = 0;
static uint32_t last_ms = 0;

static uint32_t rng = 2463534242u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void place_target(void) {
    const int m = target_r + 10;
    int tx, ty;
    for (int tries = 0; tries < 20; tries++) {
        tx = m + (int)(rnd() % (uint32_t)(W - 2 * m));
        ty = field_top + m + (int)(rnd() % (uint32_t)(H - field_top - 2 * m - 20));
        float dx = tx - bx, dy = ty - by;
        if (dx * dx + dy * dy > 120 * 120) break;   // not right under the ball
    }
    lv_obj_set_pos(target, tx - target_r, ty - target_r);
}

static void restart(void) {
    bx = W / 2.0f; by = (H + field_top) / 2.0f;
    vx = vy = 0;
    score = 0;
    lv_label_set_text(lbl_score, "0");
    place_target();
}

static lv_obj_t* dot(lv_obj_t* parent, int r, lv_color_t c) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void tilt_create(lv_obj_t* root) {
    W = board_caps().width; H = board_caps().height;
    ball_r = W / 18;  target_r = W / 24;
    field_top = H >= 460 ? 90 : 60;
    field = root;

    lbl_score = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_score, &font_styrene_48, 0);
    lv_obj_set_style_text_color(lbl_score, THEME_TEXT, 0);
    lv_obj_align(lbl_score, LV_ALIGN_TOP_MID, 0, 20);

    lbl_best = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_best, &font_cjk_28, 0);
    lv_obj_set_style_text_color(lbl_best, THEME_DIM, 0);
    lv_label_set_text(lbl_best, "");
    lv_obj_align(lbl_best, LV_ALIGN_TOP_RIGHT, -40, 34);

    target = dot(root, target_r, THEME_GREEN);
    ball = dot(root, ball_r, THEME_ACCENT);

    lbl_none = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_none, &font_cjk_28, 0);
    lv_obj_set_style_text_color(lbl_none, THEME_DIM, 0);
    lv_label_set_text(lbl_none, "這塊板子沒有加速度計");
    lv_obj_center(lbl_none);
    lv_obj_add_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);

    restart();
}

static void tilt_enter(void) {
    rng ^= millis() * 2654435761u;
    imu_hal_app_mode(true);    // freeze screen rotation while playing
    last_ms = millis();
}

static void tilt_leave(void) { imu_hal_app_mode(false); }

static void tilt_tick(void) {
    uint32_t now = millis();
    if (now - last_ms < 16) return;
    float dt = (now - last_ms) / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;
    last_ms = now;

    float ax, ay, az;
    if (!imu_hal_accel(&ax, &ay, &az)) {
        lv_obj_clear_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Gravity in screen coordinates (see TILT_FLIP_*), then follow the
    // display's rotation quadrant (frozen while the game runs).
    float gx = TILT_FLIP_X ? ay : -ay;
    float gy = TILT_FLIP_Y ? -ax : ax;
    for (uint8_t q = imu_hal_rotation_quadrant() & 3; q; q--) {
        float t = gx; gx = -gy; gy = t;
    }

    const float ACC = 2600.0f, DRAG = 0.985f, BOUNCE = 0.55f;
    vx = (vx + gx * ACC * dt) * DRAG;
    vy = (vy + gy * ACC * dt) * DRAG;
    bx += vx * dt; by += vy * dt;

    const float minx = ball_r, maxx = W - ball_r;
    const float miny = field_top + ball_r, maxy = H - ball_r - 14;
    if (bx < minx) { bx = minx; vx = -vx * BOUNCE; }
    if (bx > maxx) { bx = maxx; vx = -vx * BOUNCE; }
    if (by < miny) { by = miny; vy = -vy * BOUNCE; }
    if (by > maxy) { by = maxy; vy = -vy * BOUNCE; }
    lv_obj_set_pos(ball, (int)bx - ball_r, (int)by - ball_r);

    float tx = lv_obj_get_x(target) + target_r, ty = lv_obj_get_y(target) + target_r;
    float dx = tx - bx, dy = ty - by, rr = ball_r + target_r;
    if (dx * dx + dy * dy < rr * rr) {
        score++;
        if (score > best) best = score;
        lv_label_set_text_fmt(lbl_score, "%d", score);
        lv_label_set_text_fmt(lbl_best, "最高 %d", best);
        place_target();
    }
}

static bool tilt_on_pwr(void) { restart(); return true; }

extern const App APP_TILT = {
    /*name*/       "Tilt",
    /*icon_glyph*/ ICON_BALL,
    /*icon_img*/   nullptr,
    /*color*/      0xd97757,
    /*create*/     tilt_create,
    /*enter*/      tilt_enter,
    /*leave*/      tilt_leave,
    /*tick*/       tilt_tick,
    /*on_pwr*/     tilt_on_pwr,
    /*label*/      "重力球",
};
