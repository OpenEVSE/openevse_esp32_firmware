/**
 * @file tsdb_internal.h
 * @brief Internal structures and functions for ESP TSDB
 */

#ifndef TSDB_INTERNAL_H
#define TSDB_INTERNAL_H

#include "esp_tsdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>

// ============================================================================
// FILE FORMAT STRUCTURES (internal only)
// ============================================================================
// Note: tsdb_header_t and tsdb_block_t are now in esp_tsdb.h (public API)

/**
 * @brief Sparse time index entry (internal only)
 */
typedef struct {
    uint32_t timestamp;             // First timestamp in block range
    uint32_t block_number;          // Physical block number
} __attribute__((packed)) tsdb_index_entry_t;

// ============================================================================
// BUFFER POOL MANAGEMENT
// ============================================================================

#define TSDB_MAX_PAGES 128          // Max pages for paged allocation

/**
 * @brief Buffer pool (paged or contiguous)
 */
typedef struct {
    void *pages[TSDB_MAX_PAGES];    // Page pointers
    uint8_t num_pages;              // Number of allocated pages
    size_t page_size;               // Size of each page
    size_t total_size;              // Total allocated size
    bool is_paged;                  // true = paged, false = contiguous
} tsdb_buffer_pool_t;

// Buffer pool functions (implemented in tsdb_buffer.c)
esp_err_t tsdb_alloc_buffer_pool(tsdb_buffer_pool_t *pool,
                                 size_t total_size,
                                 bool use_paged,
                                 size_t page_size,
                                 tsdb_alloc_strategy_t strategy);
void tsdb_free_buffer_pool(tsdb_buffer_pool_t *pool);
void* tsdb_get_buffer_ptr(tsdb_buffer_pool_t *pool, size_t offset, size_t size);
void tsdb_buffer_read(tsdb_buffer_pool_t *pool, size_t offset, void *dest, size_t size);
void tsdb_buffer_write(tsdb_buffer_pool_t *pool, size_t offset, const void *src, size_t size);

// ============================================================================
// PER-INSTANCE STATE
// ============================================================================

/**
 * @brief Database handle internals.
 *
 * Forward-declared as `tsdb_t` (opaque) in esp_tsdb.h so multiple instances
 * can be opened simultaneously. Each handle carries its own file descriptor,
 * header cache, buffer pool, overflow state, and FreeRTOS mutex.
 *
 * v2 single-DB callers see a static `g_default_handle` of this type wrapped
 * by the legacy global API (tsdb_init/tsdb_write/etc).
 */
struct tsdb_s {
    FILE *file;
    tsdb_header_t header;
    char filepath[128];
    bool is_open;

    // Buffer pool
    tsdb_buffer_pool_t pool;

    // Logical buffer regions (offsets into pool)
    size_t read_buffer_offset;      // Offset for block read buffer
    size_t write_cache_offset;      // Offset for block write cache
    size_t query_buffer_offset;     // Offset for query iterator
    size_t stream_buffer_offset;    // Offset for streaming/temp data
    size_t stream_buffer_size;      // Size of stream buffer

    // Write cache state
    uint32_t cached_block_num;
    bool cache_dirty;

    // Overflow state
    uint8_t  extra_param_count;
    uint32_t overflow_data_offset;      // overflow_offset + TSDB_OVERFLOW_HEADER_SIZE
    uint32_t first_overflow_record_idx;
    uint16_t overflow_record_size;

    // Per-handle serialization. Acquired by every public _h-suffixed call so
    // concurrent writers/queriers on the same handle are safe; different
    // handles are fully independent.
    SemaphoreHandle_t mutex;
};

// Legacy single-DB handle backing the v2 global API. Allocated lazily on the
// first tsdb_init() call; freed by tsdb_close().
extern tsdb_t *g_default_handle;

// ============================================================================
// INTERNAL FUNCTIONS
// ============================================================================
// Note: struct tsdb_query_s is now in esp_tsdb.h (public API)

// Core operations (tsdb_core.c)
esp_err_t tsdb_read_header(FILE *file, tsdb_header_t *header);
esp_err_t tsdb_write_header(FILE *file, const tsdb_header_t *header);

// Block operations (tsdb_write.c, tsdb_query.c)
esp_err_t tsdb_read_block(tsdb_t *db, uint32_t block_num, tsdb_block_t *block);
esp_err_t tsdb_write_block(tsdb_t *db, uint32_t block_num, const tsdb_block_t *block);
uint32_t tsdb_calc_block_offset(const tsdb_header_t *header, uint32_t block_num);

// Index operations (tsdb_index.c)
esp_err_t tsdb_find_block_for_timestamp(FILE *file,
                                        const tsdb_header_t *header,
                                        uint32_t timestamp,
                                        uint32_t *block_num);

#endif // TSDB_INTERNAL_H
