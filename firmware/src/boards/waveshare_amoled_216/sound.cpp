#include "../../hal/sound_hal.h"
#include "board.h"

#if BOARD_HAS_SOUND

#include <Arduino.h>
#include "../../chime.h"

// AMOLED-2.16: ES8311 codec + speaker. The power amp is a plain GPIO. All the
// codec/I2S/playback work lives in the shared chime engine (../../chime.cpp);
// this file only supplies the board's pins and the amp-enable hook.

static void amp_enable(bool on) {
    digitalWrite(SND_PA_PIN, on ? HIGH : LOW);
}

void sound_hal_init(void) {
    pinMode(SND_PA_PIN, OUTPUT);
    const ChimeConfig cfg = {
        SND_I2S_MCLK, SND_I2S_BCLK, SND_I2S_WS, SND_I2S_DOUT, SND_I2S_DIN,
        SND_SAMPLE_RATE, SND_ES8311_ADDR, 65, amp_enable
    };
    chime_init(cfg);
}

void sound_hal_play_reset(void) { chime_play(); }
void sound_hal_tick(void)       { chime_tick(); }

// ---- Microphone (ES7210 on the shared I2S bus) ----------------------------
// The ES7210 ADC sits on the same MCLK/BCLK/LRCK as the ES8311 and returns
// MIC1/MIC2 on the I2S data-in line (GPIO 10), so the chime engine's
// full-duplex I2S already carries it. A small task drains I2S into a ring
// buffer; apps read the newest samples without blocking the UI loop.
#include "../../hal/hal_extras.h"
#include "es7210.h"

#define MIC_RING   4096                 // mono samples (~93 ms at 44.1 kHz)
#define MIC_CHUNK  256                  // stereo frames per I2S read

static int16_t          mic_ring[MIC_RING];
static volatile uint32_t mic_head = 0;  // total samples written
static volatile bool    mic_run = false;
static TaskHandle_t     mic_task_h = nullptr;
static bool             es7210_ok = false;

static bool es7210_setup(void) {
    audio_hal_codec_config_t cfg = {};
    cfg.adc_input  = AUDIO_HAL_ADC_INPUT_ALL;
    cfg.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
    cfg.i2s_iface.mode    = AUDIO_HAL_MODE_SLAVE;
    cfg.i2s_iface.fmt     = AUDIO_HAL_I2S_NORMAL;
    cfg.i2s_iface.samples = AUDIO_HAL_44K_SAMPLES;
    cfg.i2s_iface.bits    = AUDIO_HAL_BIT_LENGTH_16BITS;
    esp_err_t r = ESP_OK;
    r |= es7210_adc_init(&Wire, &cfg);
    r |= es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface);
    r |= es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2),
                             (es7210_gain_value_t)GAIN_30DB);
    r |= es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START);
    return r == ESP_OK;
}

static void mic_task(void*) {
    static int16_t frames[MIC_CHUNK * 2];
    while (mic_run) {
        size_t got = chime_i2s_read(frames, sizeof(frames));
        int n = (int)(got / 4);                 // stereo frames
        uint32_t h = mic_head;
        for (int i = 0; i < n; i++) {
            // Average MIC1 (L) and MIC2 (R).
            mic_ring[h % MIC_RING] = (int16_t)(((int32_t)frames[2 * i] + frames[2 * i + 1]) / 2);
            h++;
        }
        mic_head = h;
        if (n == 0) vTaskDelay(pdMS_TO_TICKS(10));
    }
    mic_task_h = nullptr;
    vTaskDelete(nullptr);
}

bool sound_hal_mic_start(int* sample_rate) {
    if (!chime_is_ready()) return false;
    if (!es7210_ok) {
        es7210_ok = es7210_setup();
        Serial.printf("ES7210 mic init %s\n", es7210_ok ? "OK" : "FAILED");
        if (!es7210_ok) return false;
    }
    if (sample_rate) *sample_rate = SND_SAMPLE_RATE;
    if (!mic_run) {
        mic_run = true;
        if (xTaskCreatePinnedToCore(mic_task, "mic", 4096, nullptr, 1, &mic_task_h, 0) != pdPASS) {
            mic_run = false;
            return false;
        }
    }
    return true;
}

int sound_hal_mic_latest(int16_t* out, int n) {
    uint32_t h = mic_head;
    if (h < (uint32_t)n || n > MIC_RING) return 0;
    for (int i = 0; i < n; i++) out[i] = mic_ring[(h - n + i) % MIC_RING];
    return n;
}

void sound_hal_mic_stop(void) { mic_run = false; }

#endif  // BOARD_HAS_SOUND
