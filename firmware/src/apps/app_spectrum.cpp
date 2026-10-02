// 頻譜 Spectrum — live audio spectrum from the onboard microphones (ES7210).
// (Waveshare example 05_Spec_Analyzer.) PWR button = freeze / resume.
#include "app.h"
#include "../theme.h"
#include "../hal/board_caps.h"
#include "../hal/hal_extras.h"

#include <Arduino.h>
#include <math.h>

LV_FONT_DECLARE(font_cjk_28);
LV_FONT_DECLARE(font_styrene_16);

extern const App APP_SPECTRUM;

#define FFT_N   1024
#define BANDS   28

static float re[FFT_N], im[FFT_N], window_[FFT_N];
static int16_t samples[FFT_N];

static lv_obj_t *bars[BANDS], *peaks[BANDS], *lbl_none, *lbl_hz[3], *lbl_state;
static float level[BANDS], peak[BANDS];
static int  band_lo[BANDS], band_hi[BANDS];
static int  sample_rate = 44100;
static bool mic_ok = false, frozen = false;
static int  W, H, area_x, area_y, area_w, area_h, bar_w;

// In-place iterative radix-2 FFT.
static void fft(float* xr, float* xi, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = xr[i]; xr[i] = xr[j]; xr[j] = t; t = xi[i]; xi[i] = xi[j]; xi[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = a + len / 2;
                float tr = xr[b] * cr - xi[b] * ci, ti = xr[b] * ci + xi[b] * cr;
                xr[b] = xr[a] - tr; xi[b] = xi[a] - ti;
                xr[a] += tr;        xi[a] += ti;
                float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

static void compute_bands(void) {
    // Log-spaced bands from 60 Hz to 16 kHz.
    const float f0 = 60, f1 = 16000;
    const float bin_hz = (float)sample_rate / FFT_N;
    int prev = 1;
    for (int b = 0; b < BANDS; b++) {
        float hi = f0 * powf(f1 / f0, (b + 1) / (float)BANDS);
        int hb = (int)(hi / bin_hz);
        if (hb <= prev) hb = prev + 1;
        if (hb > FFT_N / 2 - 1) hb = FFT_N / 2 - 1;
        band_lo[b] = prev; band_hi[b] = hb;
        prev = hb;
    }
}

static void spectrum_create(lv_obj_t* root) {
    W = board_caps().width; H = board_caps().height;
    app_make_title(root, app_label(&APP_SPECTRUM));

    area_x = 30; area_w = W - 60;
    area_y = H >= 460 ? 110 : 80;
    area_h = H - area_y - 80;
    bar_w = area_w / BANDS - 4;

    for (int i = 0; i < FFT_N; i++) window_[i] = 0.5f - 0.5f * cosf(2 * (float)M_PI * i / (FFT_N - 1));

    for (int b = 0; b < BANDS; b++) {
        int x = area_x + b * (area_w / BANDS) + 2;
        bars[b] = lv_obj_create(root);
        lv_obj_remove_style_all(bars[b]);
        lv_obj_set_style_bg_opa(bars[b], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bars[b], 3, 0);
        // Hue sweeps from accent (low) to blue (high)
        lv_color_t c = lv_color_mix(lv_color_hex(0x6ab0de), THEME_ACCENT, (uint8_t)(255 * b / (BANDS - 1)));
        lv_obj_set_style_bg_color(bars[b], c, 0);
        lv_obj_set_pos(bars[b], x, area_y + area_h - 4);
        lv_obj_set_size(bars[b], bar_w, 4);

        peaks[b] = lv_obj_create(root);
        lv_obj_remove_style_all(peaks[b]);
        lv_obj_set_style_bg_opa(peaks[b], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(peaks[b], THEME_TEXT, 0);
        lv_obj_set_size(peaks[b], bar_w, 3);
        lv_obj_set_pos(peaks[b], x, area_y + area_h - 7);
    }

    static const char* const FREQ_LABELS[] = {"60 Hz", "1 kHz", "16 kHz"};
    for (int i = 0; i < 3; i++) {
        lbl_hz[i] = lv_label_create(root);
        lv_obj_set_style_text_font(lbl_hz[i], &font_styrene_16, 0);
        lv_obj_set_style_text_color(lbl_hz[i], THEME_DIM, 0);
        lv_label_set_text(lbl_hz[i], FREQ_LABELS[i]);
    }
    lv_obj_set_pos(lbl_hz[0], area_x, area_y + area_h + 8);
    lv_obj_align(lbl_hz[1], LV_ALIGN_TOP_MID, 0, area_y + area_h + 8);
    lv_obj_align(lbl_hz[2], LV_ALIGN_TOP_RIGHT, -area_x, area_y + area_h + 8);

    lbl_state = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_state, &font_cjk_28, 0);
    lv_obj_set_style_text_color(lbl_state, THEME_DIM, 0);
    lv_label_set_text(lbl_state, "");
    lv_obj_align(lbl_state, LV_ALIGN_TOP_RIGHT, -30, 34);

    lbl_none = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_none, &font_cjk_28, 0);
    lv_obj_set_style_text_color(lbl_none, THEME_DIM, 0);
    lv_label_set_text(lbl_none, "這塊板子沒有麥克風");
    lv_obj_center(lbl_none);
    lv_obj_add_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);
}

static void spectrum_enter(void) {
    mic_ok = sound_hal_mic_start(&sample_rate);
    compute_bands();
    frozen = false;
    lv_label_set_text(lbl_state, "");
    for (int b = 0; b < BANDS; b++) { level[b] = 0; peak[b] = 0; }
    if (mic_ok) lv_obj_add_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_clear_flag(lbl_none, LV_OBJ_FLAG_HIDDEN);
}

static void spectrum_leave(void) { sound_hal_mic_stop(); }

static void spectrum_tick(void) {
    static uint32_t last = 0;
    if (!mic_ok || frozen || millis() - last < 40) return;   // ~25 fps
    last = millis();
    if (sound_hal_mic_latest(samples, FFT_N) != FFT_N) return;

    float mean = 0;
    for (int i = 0; i < FFT_N; i++) mean += samples[i];
    mean /= FFT_N;
    for (int i = 0; i < FFT_N; i++) { re[i] = (samples[i] - mean) * window_[i]; im[i] = 0; }
    fft(re, im, FFT_N);

    for (int b = 0; b < BANDS; b++) {
        float m = 0;
        for (int k = band_lo[b]; k < band_hi[b]; k++) {
            float p = re[k] * re[k] + im[k] * im[k];
            if (p > m) m = p;
        }
        // dB relative to a loud full-scale-ish tone; map -60..0 dB → 0..1
        float db = 10.0f * log10f(m + 1e-3f) - 10.0f * log10f(1e14f);
        float v = (db + 60.0f) / 60.0f;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        level[b] = v > level[b] ? v : level[b] * 0.82f + v * 0.18f;   // fast attack, slow fall
        peak[b]  = level[b] > peak[b] ? level[b] : peak[b] - 0.012f;
        if (peak[b] < 0) peak[b] = 0;

        int h = 4 + (int)(level[b] * (area_h - 8));
        int x = lv_obj_get_x(bars[b]);
        lv_obj_set_pos(bars[b], x, area_y + area_h - h);
        lv_obj_set_size(bars[b], bar_w, h);
        lv_obj_set_y(peaks[b], area_y + area_h - 7 - (int)(peak[b] * (area_h - 8)));
    }
}

static bool spectrum_on_pwr(void) {
    frozen = !frozen;
    lv_label_set_text(lbl_state, frozen ? "暫停" : "");
    return true;
}

extern const App APP_SPECTRUM = {
    /*name*/       "Spectrum",
    /*icon_glyph*/ ICON_WAVE,
    /*icon_img*/   nullptr,
    /*color*/      0xc46686,
    /*create*/     spectrum_create,
    /*enter*/      spectrum_enter,
    /*leave*/      spectrum_leave,
    /*tick*/       spectrum_tick,
    /*on_pwr*/     spectrum_on_pwr,
    /*label*/      "頻譜",
};
