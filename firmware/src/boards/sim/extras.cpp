// Simulator versions of the optional board extras (hal_extras.h) so the
// Settings / Sensors / Tilt / Spectrum apps have something to show.
#include "../../hal/hal_extras.h"
#include "sim_platform.h"
#include <Arduino.h>
#include <math.h>

bool power_hal_details(PowerDetails* d) {
    d->pct = sim_battery_pct();
    d->charging = sim_charging();
    d->vbus_in = true;
    d->batt_present = true;
    d->batt_mv = 3700 + d->pct * 5;
    d->vbus_mv = 5040;
    d->sys_mv = 3900;
    d->pmu_temp_c = 36.5f;
    return true;
}

// Slow wobble so the level / tilt game move on their own.
bool imu_hal_accel(float* x, float* y, float* z) {
    float t = millis() / 1000.0f;
    *x = 0.35f * sinf(t * 0.9f);
    *y = 0.25f * cosf(t * 0.6f);
    *z = sqrtf(fmaxf(0.0f, 1.0f - (*x) * (*x) - (*y) * (*y)));
    return true;
}
void imu_hal_app_mode(bool) {}

// Synthetic "music": a few tones + noise.
static bool mic_on = false;
bool sound_hal_mic_start(int* rate) { if (rate) *rate = 44100; mic_on = true; return true; }
int sound_hal_mic_latest(int16_t* out, int n) {
    if (!mic_on) return 0;
    float t0 = millis() / 1000.0f;
    for (int i = 0; i < n; i++) {
        float t = t0 + i / 44100.0f;
        float v = 6000 * sinf(2 * M_PI * 110 * t) + 4000 * sinf(2 * M_PI * 440 * t)
                + 2500 * sinf(2 * M_PI * (1500 + 800 * sinf(t0)) * t) + 1500 * sinf(2 * M_PI * 6000 * t)
                + (rand() % 1000 - 500);
        out[i] = (int16_t)v;
    }
    return n;
}
void sound_hal_mic_stop(void) { mic_on = false; }
