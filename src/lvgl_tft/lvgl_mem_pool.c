#include "lvgl_mem_pool.h"

#if defined(LVGL_POOL_IN_PSRAM) && !defined(EPOXY_DUINO)

#include <esp_heap_caps.h>
#include <esp_log.h>

void *lvgl_mem_pool_alloc(size_t size)
{
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(p != NULL) {
    return p;
  }

  // PSRAM absent, not initialised, or exhausted. Internal DRAM is worse for
  // this pool but it is a working display; returning NULL is a panic loop.
  ESP_LOGW("lvgl", "PSRAM pool alloc of %u B failed, falling back to internal DRAM",
           (unsigned)size);
  return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

#endif
