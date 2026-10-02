// LVGL memory allocator (LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM).
//
// On PSRAM boards (S3) LVGL widgets, styles and label strings go to PSRAM so
// internal SRAM stays free for BLE / Wi-Fi. Boards without PSRAM (C6) and the
// desktop sim use malloc. Every block carries an 8-byte size header so the
// Settings app can show how much LVGL actually uses (lv_mem_psram_used()).
#include "lvgl.h"
#include "lv_mem_psram.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#define LV_HEAP_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif

#define HDR 8
static size_t used_bytes = 0;

static void * raw_alloc(size_t n)
{
#ifdef LV_HEAP_CAPS
    void * p = heap_caps_malloc(n, LV_HEAP_CAPS);
    return p ? p : malloc(n);
#else
    return malloc(n);
#endif
}

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes) { LV_UNUSED(mem); LV_UNUSED(bytes); return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void * lv_malloc_core(size_t size)
{
    uint8_t * b = (uint8_t *)raw_alloc(size + HDR);
    if(!b) return NULL;
    *(size_t *)b = size;
    used_bytes += size;
    return b + HDR;
}

void lv_free_core(void * p)
{
    if(!p) return;
    uint8_t * b = (uint8_t *)p - HDR;
    used_bytes -= *(size_t *)b;
    free(b);   // ESP-IDF free() handles both internal and PSRAM blocks
}

void * lv_realloc_core(void * p, size_t new_size)
{
    if(!p) return lv_malloc_core(new_size);
    size_t old = *(size_t *)((uint8_t *)p - HDR);
    void * q = lv_malloc_core(new_size);
    if(!q) return NULL;
    memcpy(q, p, old < new_size ? old : new_size);
    lv_free_core(p);
    return q;
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p) { LV_UNUSED(mon_p); }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

size_t lv_mem_psram_used(void) { return used_bytes; }

#else
size_t lv_mem_psram_used(void) { return 0; }
#endif
