#ifndef _LVGL_MEM_POOL_H
#define _LVGL_MEM_POOL_H

// Backing allocation for LVGL's widget/style/render pool on PSRAM boards.
//
// A function rather than a bare heap_caps_malloc() macro so the PSRAM request
// can fail softly. LVGL hands whatever comes back straight to
// lv_tlsf_create_with_pool(), which dereferences it without a NULL check, so an
// allocation failure here is not a degraded display -- it is a StoreProhibited
// panic in a boot loop before anything can report why.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *lvgl_mem_pool_alloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif // _LVGL_MEM_POOL_H
