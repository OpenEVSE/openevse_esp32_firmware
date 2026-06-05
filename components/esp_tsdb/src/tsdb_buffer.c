/**
 * @file tsdb_buffer.c
 * @brief Buffer pool management with paged and contiguous allocation
 */

#include "tsdb_internal.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "TSDB_BUFFER";

/**
 * @brief Allocate buffer pool
 */
esp_err_t tsdb_alloc_buffer_pool(tsdb_buffer_pool_t *pool,
                                 size_t total_size,
                                 bool use_paged,
                                 size_t page_size,
                                 tsdb_alloc_strategy_t strategy) {
    if (pool == NULL || total_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(pool, 0, sizeof(tsdb_buffer_pool_t));
    pool->page_size = page_size > 0 ? page_size : 2048;  // Default 2KB pages
    pool->total_size = total_size;

    // Determine allocation caps based on strategy
    uint32_t caps = MALLOC_CAP_8BIT;
    if (strategy == TSDB_ALLOC_PSRAM) {
        caps = MALLOC_CAP_SPIRAM;
        ESP_LOGI(TAG, "Using PSRAM allocation strategy");
    } else if (strategy == TSDB_ALLOC_INTERNAL_RAM) {
        caps = MALLOC_CAP_INTERNAL;
        ESP_LOGI(TAG, "Using internal RAM allocation strategy");
    } else {
        // Auto-detect: prefer PSRAM if available
        if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > total_size) {
            caps = MALLOC_CAP_SPIRAM;
            ESP_LOGI(TAG, "Auto-detected: using PSRAM");
        } else {
            caps = MALLOC_CAP_INTERNAL;
            ESP_LOGI(TAG, "Auto-detected: using internal RAM");
        }
    }

    if (use_paged) {
        // Paged allocation mode
        pool->is_paged = true;
        size_t pages_needed = (total_size + pool->page_size - 1) / pool->page_size;

        if (pages_needed > TSDB_MAX_PAGES) {
            ESP_LOGE(TAG, "Too many pages needed: %d (max %d)", pages_needed, TSDB_MAX_PAGES);
            return ESP_ERR_INVALID_ARG;
        }

        ESP_LOGI(TAG, "Allocating %d pages of %d bytes each (%d KB total)",
                 pages_needed, pool->page_size, (pages_needed * pool->page_size) / 1024);

        // Allocate pages
        for (int i = 0; i < pages_needed; i++) {
            pool->pages[i] = heap_caps_malloc(pool->page_size, caps);
            if (pool->pages[i] == NULL) {
                ESP_LOGE(TAG, "Failed to allocate page %d of %d", i + 1, pages_needed);
                ESP_LOGE(TAG, "Free heap: %d, largest block: %d",
                         heap_caps_get_free_size(caps),
                         heap_caps_get_largest_free_block(caps));

                // Cleanup already allocated pages
                for (int j = 0; j < i; j++) {
                    free(pool->pages[j]);
                    pool->pages[j] = NULL;
                }
                return ESP_ERR_NO_MEM;
            }
            pool->num_pages++;
            ESP_LOGD(TAG, "Page %d allocated at %p", i, pool->pages[i]);
        }

        ESP_LOGI(TAG, "Successfully allocated %d pages (%d KB) from fragmented heap",
                 pool->num_pages, (pool->num_pages * pool->page_size) / 1024);

    } else {
        // Contiguous allocation mode
        pool->is_paged = false;
        pool->pages[0] = heap_caps_malloc(total_size, caps);

        if (pool->pages[0] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %d KB contiguous buffer", total_size / 1024);
            ESP_LOGE(TAG, "Free heap: %d, largest block: %d",
                     heap_caps_get_free_size(caps),
                     heap_caps_get_largest_free_block(caps));
            return ESP_ERR_NO_MEM;
        }

        pool->num_pages = 1;
        ESP_LOGI(TAG, "Successfully allocated %d KB contiguous buffer at %p",
                 total_size / 1024, pool->pages[0]);
    }

    return ESP_OK;
}

/**
 * @brief Free buffer pool
 */
void tsdb_free_buffer_pool(tsdb_buffer_pool_t *pool) {
    if (pool == NULL) {
        return;
    }

    for (int i = 0; i < pool->num_pages; i++) {
        if (pool->pages[i]) {
            free(pool->pages[i]);
            pool->pages[i] = NULL;
        }
    }

    ESP_LOGI(TAG, "Buffer pool freed (%d pages)", pool->num_pages);
    memset(pool, 0, sizeof(tsdb_buffer_pool_t));
}

/**
 * @brief Get direct pointer to buffer region (zero-copy if possible)
 *
 * @return Pointer if region is contiguous, NULL if spans pages
 */
void* tsdb_get_buffer_ptr(tsdb_buffer_pool_t *pool, size_t offset, size_t size) {
    if (pool == NULL || pool->num_pages == 0) {
        return NULL;
    }

    // Check bounds
    if (offset + size > pool->total_size) {
        ESP_LOGE(TAG, "Buffer access out of bounds: offset=%d, size=%d, total=%d",
                 offset, size, pool->total_size);
        return NULL;
    }

    if (!pool->is_paged) {
        // Contiguous mode: always return direct pointer
        return (uint8_t*)pool->pages[0] + offset;
    }

    // Paged mode: check if region fits in single page
    size_t page_idx = offset / pool->page_size;
    size_t page_offset = offset % pool->page_size;

    // If region spans multiple pages, return NULL (caller must use copy functions)
    if (page_offset + size > pool->page_size) {
        return NULL;
    }

    // Fits in single page, return direct pointer
    return (uint8_t*)pool->pages[page_idx] + page_offset;
}

/**
 * @brief Read from buffer pool (handles paging automatically)
 */
void tsdb_buffer_read(tsdb_buffer_pool_t *pool, size_t offset, void *dest, size_t size) {
    if (pool == NULL || dest == NULL || size == 0) {
        return;
    }

    // Check bounds
    if (offset + size > pool->total_size) {
        ESP_LOGE(TAG, "Buffer read out of bounds");
        return;
    }

    if (!pool->is_paged) {
        // Contiguous: simple memcpy
        memcpy(dest, (uint8_t*)pool->pages[0] + offset, size);
        return;
    }

    // Paged: copy across page boundaries
    size_t remaining = size;
    size_t current_offset = offset;
    uint8_t *dest_ptr = (uint8_t*)dest;

    while (remaining > 0) {
        size_t page_idx = current_offset / pool->page_size;
        size_t page_offset = current_offset % pool->page_size;
        size_t chunk_size = pool->page_size - page_offset;

        if (chunk_size > remaining) {
            chunk_size = remaining;
        }

        memcpy(dest_ptr, (uint8_t*)pool->pages[page_idx] + page_offset, chunk_size);

        dest_ptr += chunk_size;
        current_offset += chunk_size;
        remaining -= chunk_size;
    }
}

/**
 * @brief Write to buffer pool (handles paging automatically)
 */
void tsdb_buffer_write(tsdb_buffer_pool_t *pool, size_t offset, const void *src, size_t size) {
    if (pool == NULL || src == NULL || size == 0) {
        return;
    }

    // Check bounds
    if (offset + size > pool->total_size) {
        ESP_LOGE(TAG, "Buffer write out of bounds");
        return;
    }

    if (!pool->is_paged) {
        // Contiguous: simple memcpy
        memcpy((uint8_t*)pool->pages[0] + offset, src, size);
        return;
    }

    // Paged: copy across page boundaries
    size_t remaining = size;
    size_t current_offset = offset;
    const uint8_t *src_ptr = (const uint8_t*)src;

    while (remaining > 0) {
        size_t page_idx = current_offset / pool->page_size;
        size_t page_offset = current_offset % pool->page_size;
        size_t chunk_size = pool->page_size - page_offset;

        if (chunk_size > remaining) {
            chunk_size = remaining;
        }

        memcpy((uint8_t*)pool->pages[page_idx] + page_offset, src_ptr, chunk_size);

        src_ptr += chunk_size;
        current_offset += chunk_size;
        remaining -= chunk_size;
    }
}
