#include "psram.h"

#if defined(ESP32) && !defined(EPOXY_DUINO)

#include <esp_heap_caps.h>

bool psram_available()
{
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

#if !defined(CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC)
#include <mbedtls/platform.h>

// The prebuilt core is built with MBEDTLS_INTERNAL_MEM_ALLOC, which pins every
// mbedTLS allocation (SSL contexts, the 16K/4K record buffers, parsed x509
// chains) to internal DRAM. MBEDTLS_PLATFORM_MEMORY is also on, so the
// allocator can be swapped at runtime: prefer PSRAM, fall back to any heap.
// Builds whose sdkconfig sets CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC already do
// this in the SDK and compile it out.
static void *psram_tls_calloc(size_t n, size_t size)
{
  return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
}

static void psram_tls_free(void *p)
{
  heap_caps_free(p);
}
#endif

void psram_setup()
{
#if !defined(CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC)
  if(psram_available()) {
    mbedtls_platform_set_calloc_free(psram_tls_calloc, psram_tls_free);
  }
#endif
}

#else

bool psram_available() { return false; }
void psram_setup() {}

#endif
