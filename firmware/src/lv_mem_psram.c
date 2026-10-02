// LVGL memory allocator (LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM).
//
// On PSRAM boards (S3) every LVGL widget, style and label string goes to
// PSRAM. Plain malloc() keeps anything under 16 KB in internal SRAM, which
// filled ~200 of 257 KB with small LVGL objects and left nothing for Wi-Fi.
// Internal SRAM is kept for BLE/Wi-Fi and the DMA draw buffers instead.
// Boards without PSRAM (C6) and the desktop sim just use malloc.
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <stdlib.h>
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#define LV_HEAP_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes) { LV_UNUSED(mem); LV_UNUSED(bytes); return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void * lv_malloc_core(size_t size)
{
#ifdef LV_HEAP_CAPS
    void * p = heap_caps_malloc(size, LV_HEAP_CAPS);
    return p ? p : malloc(size);
#else
    return malloc(size);
#endif
}

void * lv_realloc_core(void * p, size_t new_size)
{
#ifdef LV_HEAP_CAPS
    void * q = heap_caps_realloc(p, new_size, LV_HEAP_CAPS);
    return q ? q : realloc(p, new_size);
#else
    return realloc(p, new_size);
#endif
}

void lv_free_core(void * p)
{
    free(p);   // ESP-IDF free() handles both internal and PSRAM blocks
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p) { LV_UNUSED(mon_p); }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

#endif
