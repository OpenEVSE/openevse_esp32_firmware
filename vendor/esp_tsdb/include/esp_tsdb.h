/**
 * @file esp_tsdb.h
 * @brief ESP Time-Series Database Library
 *
 * Lightweight time-series database for ESP32 with configurable memory allocation,
 * columnar storage, sparse indexing, and streaming queries.
 */

#ifndef ESP_TSDB_H
#define ESP_TSDB_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONSTANTS
// ============================================================================

#define TSDB_MAX_PARAMS 64          // Maximum parameters supported
#define TSDB_MAGIC 0x45545344       // "ETSD" (ESP Time-Series DB)
#define TSDB_VERSION 3              // File format version
#define TSDB_BLOCK_SIZE 1024        // Block size in bytes
#define TSDB_OVERFLOW_MAGIC 0x4F564652  // "OVFR"
#define TSDB_MAX_EXTRA_PARAMS 48
#define TSDB_OVERFLOW_HEADER_SIZE 1024

// ============================================================================
// OPAQUE DATABASE HANDLE
// ============================================================================

/**
 * @brief Opaque database handle. Created by tsdb_open(), released by
 *        tsdb_close_h(). Multiple handles may be open simultaneously, each
 *        backed by its own file, buffer pool, and per-instance mutex.
 *
 * The legacy global API (tsdb_init / tsdb_write / tsdb_query_init / ...) is
 * preserved for backward compatibility and operates on a single internal
 * default handle. New multi-instance code should use the _h-suffixed
 * variants below.
 */
typedef struct tsdb_s tsdb_t;

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * @brief Memory allocation strategy
 */
typedef enum {
    TSDB_ALLOC_INTERNAL_RAM,        // Use internal RAM (legacy/V1 units)
    TSDB_ALLOC_PSRAM,               // Use PSRAM (V2+ units with external RAM)
    TSDB_ALLOC_AUTO                 // Auto-detect best strategy
} tsdb_alloc_strategy_t;

/**
 * @brief Database configuration
 */
typedef struct {
    const char *filepath;           // Full path (e.g., "/littlefs/data.tsdb")

    // Data structure
    uint8_t num_params;             // Number of parameters to store (1-16)
    const char **param_names;       // Parameter names (optional, can be NULL)

    // Capacity
    uint32_t max_records;           // Maximum records before LRU eviction
    uint32_t index_stride;          // Records between index entries (default: 380)

    // Memory allocation
    size_t buffer_pool_size;        // Buffer pool size in bytes (e.g., 10KB or 256KB)
    tsdb_alloc_strategy_t alloc_strategy;  // Where to allocate buffers
    bool use_paged_allocation;      // true = use paged buffers (fragmented heap)
    size_t page_size;               // Page size if using paged allocation (default: 2048)
} tsdb_config_t;

/**
 * @brief Helper macro to calculate max records for a storage size
 *
 * @param storage_bytes Total file size budget
 * @param num_params Number of parameters per record
 */
#define TSDB_CALC_MAX_RECORDS(storage_bytes, num_params) \
    (((storage_bytes) - 2048) / (4 + ((num_params) * 2)))

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize or open existing time-series database
 *
 * @param config Configuration structure
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if config is invalid
 *         ESP_ERR_NO_MEM if buffer allocation fails
 *         ESP_FAIL on file I/O errors
 */
esp_err_t tsdb_init(const tsdb_config_t *config);

/**
 * @brief Close database and flush buffers
 *
 * @return ESP_OK on success
 */
esp_err_t tsdb_close(void);

/**
 * @brief Check if database is initialized
 *
 * @return true if initialized
 */
bool tsdb_is_initialized(void);

// ============================================================================
// WRITE OPERATIONS
// ============================================================================

/**
 * @brief Write a single record
 *
 * @param timestamp Unix timestamp (seconds)
 * @param values Array of parameter values (size must match num_params)
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if not initialized
 *         ESP_ERR_INVALID_ARG if values is NULL
 */
esp_err_t tsdb_write(uint32_t timestamp, const int16_t *values);

/**
 * @brief Write multiple records in batch (more efficient)
 *
 * @param timestamps Array of timestamps
 * @param values 2D array [count][num_params]
 * @param count Number of records
 * @return ESP_OK on success
 */
esp_err_t tsdb_write_batch(const uint32_t *timestamps,
                            const int16_t **values,
                            uint32_t count);

// ============================================================================
// INTERNAL STRUCTURES (exposed for stack allocation)
// ============================================================================

/**
 * @brief Header structure (512 bytes, stored at file offset 0)
 */
typedef struct {
    uint32_t magic;                 // Validation magic number (TSDB_MAGIC)
    uint32_t version;               // File format version

    // Parameter configuration
    uint8_t num_params;             // Number of parameters stored
    uint8_t param_size;             // Bytes per parameter (2 = int16_t)
    uint16_t record_size;           // Total bytes per record
    uint16_t records_per_block;     // Calculated from block size

    // Capacity and ring buffer state
    uint32_t max_records;           // Maximum records before LRU eviction
    uint32_t total_records;         // Total records ever written
    uint32_t oldest_record_idx;     // Index of oldest valid record
    uint32_t newest_record_idx;     // Index of newest record

    // Time bounds (for quick validation)
    uint32_t oldest_timestamp;      // Timestamp of oldest record
    uint32_t newest_timestamp;      // Timestamp of newest record

    // Index metadata
    uint32_t index_offset;          // Byte offset to sparse index
    uint32_t index_entries;         // Number of index entries
    uint32_t index_stride;          // Records between index entries

    // Statistics
    uint32_t total_writes;          // Lifetime write count
    uint32_t total_evictions;       // Lifetime LRU evictions

    // Parameter metadata (optional, for validation)
    char param_names[16][32];       // Human-readable parameter names

    uint8_t  base_params;               // Original num_params (drives block geometry)
    uint8_t  extra_param_count;          // Number of overflow params (0 = none)
    uint16_t overflow_record_size;       // extra_param_count * sizeof(int16_t)
    uint32_t overflow_offset;            // File offset of overflow region (0 = none)
    uint32_t first_overflow_record_idx;  // total_records when overflow was created
    uint8_t  reserved2[2];              // 2 bytes remaining (was 16 bytes reserved)
} __attribute__((packed)) tsdb_header_t;

/**
 * @brief Overflow header structure (1024 bytes, at overflow_offset)
 */
typedef struct {
    uint32_t magic;                    // 0x4F564652 "OVFR"
    uint8_t  param_count;             // Number of extra params
    uint8_t  reserved[3];
    uint32_t first_record_idx;        // Same as header.first_overflow_record_idx
    char     param_names[48][20];     // Up to 48 extra param names (960 bytes)
    uint8_t  padding[52];             // Pad to 1024 bytes total
} __attribute__((packed)) tsdb_overflow_header_t;

/**
 * @brief Data block (1024 bytes, columnar storage)
 */
typedef struct {
    uint32_t block_magic;           // 0x424C4B54 "BLKT"
    uint16_t record_count;          // Valid records in this block
    uint16_t flags;                 // Reserved flags

    // Columnar layout for selective reading
    uint32_t timestamps[38];        // Max records per block (for 10 params)

    // Parameters stored column-wise
    // Actual size determined by num_params at runtime
    int16_t params[16][38];

    // Note: Actual usable records_per_block calculated at init based on num_params
    // This structure is sized for worst case (max params)
} __attribute__((packed)) tsdb_block_t;

/**
 * @brief Runtime block access macros
 *
 * The tsdb_block_t struct is used as a memory buffer only.
 * These macros calculate correct byte offsets for the on-disk
 * columnar layout based on actual records_per_block.
 */
#define TSDB_BLOCK_HEADER_SIZE 8  // magic(4) + record_count(2) + flags(2)

// Access block fields from raw buffer
#define TSDB_BLOCK_MAGIC(buf)         (*(uint32_t*)(buf))
#define TSDB_BLOCK_COUNT(buf)         (*(uint16_t*)((uint8_t*)(buf) + 4))
#define TSDB_BLOCK_FLAGS(buf)         (*(uint16_t*)((uint8_t*)(buf) + 6))
#define TSDB_BLOCK_TS(buf, rec)       (*(uint32_t*)((uint8_t*)(buf) + 8 + (rec) * 4))
#define TSDB_BLOCK_PARAM(buf, rpb, param_idx, rec) \
    (*(int16_t*)((uint8_t*)(buf) + 8 + (rpb) * 4 + (param_idx) * (rpb) * 2 + (rec) * 2))

// ============================================================================
// QUERY OPERATIONS
// ============================================================================

/**
 * @brief Query handle for iterating through results
 *
 * Note: Callers should allocate this on the stack
 */
struct tsdb_query_s {
    tsdb_t *db;                     // Database handle this query iterates over
    FILE *file;
    tsdb_header_t header;

    // Query parameters
    uint32_t start_time;
    uint32_t end_time;
    uint8_t param_indices[TSDB_MAX_PARAMS];
    uint8_t num_params_to_fetch;

    // Iterator state
    uint32_t current_record_idx;
    uint32_t end_record_idx;
    uint32_t records_scanned;       // ring slots consumed so far this query
    uint32_t current_block_num;
    uint16_t offset_in_block;

    // Block buffer (points into global buffer pool or local storage)
    tsdb_block_t *block_buffer;
    bool block_loaded;
    bool owns_buffer;               // true if allocated separately
};

typedef struct tsdb_query_s tsdb_query_t;

/**
 * @brief Initialize a query for time range
 *
 * @param query Query handle (allocated by caller)
 * @param start_time Start timestamp (inclusive)
 * @param end_time End timestamp (inclusive)
 * @param param_indices Which parameters to fetch (NULL = all)
 * @param num_params_to_fetch Number of parameters to fetch
 * @return ESP_OK on success
 */
esp_err_t tsdb_query_init(tsdb_query_t *query,
                          uint32_t start_time,
                          uint32_t end_time,
                          const uint8_t *param_indices,
                          uint8_t num_params_to_fetch);

/**
 * @brief Fetch next record from query
 *
 * @param query Query handle
 * @param timestamp Output timestamp
 * @param values Output values (size must match num_params_to_fetch)
 * @return ESP_OK on success
 *         ESP_ERR_NOT_FOUND when no more records
 */
esp_err_t tsdb_query_next(tsdb_query_t *query,
                          uint32_t *timestamp,
                          int16_t *values);

/**
 * @brief Get count of records in time range without iterating
 *
 * @param start_time Start timestamp
 * @param end_time End timestamp
 * @param count Output count
 * @return ESP_OK on success
 */
esp_err_t tsdb_query_count(uint32_t start_time,
                           uint32_t end_time,
                           uint32_t *count);

/**
 * @brief Close query and free resources
 *
 * @param query Query handle
 */
void tsdb_query_close(tsdb_query_t *query);

// ============================================================================
// AGGREGATION OPERATIONS
// ============================================================================

/**
 * @brief Aggregation types
 */
typedef enum {
    TSDB_AGG_SUM,
    TSDB_AGG_AVG,
    TSDB_AGG_MIN,
    TSDB_AGG_MAX,
    TSDB_AGG_COUNT,
    TSDB_AGG_FIRST,
    TSDB_AGG_LAST
} tsdb_agg_type_t;

/**
 * @brief Perform aggregation over time range
 *
 * @param start_time Start timestamp
 * @param end_time End timestamp
 * @param param_index Which parameter to aggregate
 * @param agg_type Aggregation operation
 * @param result Output result (int32 for overflow safety)
 * @return ESP_OK on success
 */
esp_err_t tsdb_aggregate(uint32_t start_time,
                         uint32_t end_time,
                         uint8_t param_index,
                         tsdb_agg_type_t agg_type,
                         int32_t *result);

/**
 * @brief Result for one parameter in a multi-aggregate query
 */
typedef struct {
    uint8_t param_index;            // Which parameter
    tsdb_agg_type_t agg_type;       // Which aggregation
    int32_t result;                  // Computed result
} tsdb_agg_request_t;

/**
 * @brief Perform multiple aggregations in a single pass
 *
 * @param start_time Start timestamp
 * @param end_time End timestamp
 * @param requests Array of aggregation requests (param_index and agg_type filled in, result filled out)
 * @param num_requests Number of requests
 * @param record_count Output: number of records scanned (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t tsdb_aggregate_multi(uint32_t start_time,
                                uint32_t end_time,
                                tsdb_agg_request_t *requests,
                                uint8_t num_requests,
                                uint32_t *record_count);

// ============================================================================
// STATISTICS & MAINTENANCE
// ============================================================================

/**
 * @brief Database statistics
 */
typedef struct {
    uint32_t total_records;         // Current record count
    uint32_t max_records;           // Capacity
    uint32_t oldest_timestamp;      // Oldest record time
    uint32_t newest_timestamp;      // Newest record time
    uint32_t total_writes;          // Lifetime writes
    uint32_t total_evictions;       // Lifetime LRU evictions
    uint32_t storage_bytes;         // File size on disk
    uint8_t num_params;             // Parameters per record
    uint8_t extra_params;           // Extra overflow parameters
    size_t buffer_pool_size;        // Allocated buffer size
    bool using_paged_allocation;    // true if using paged buffers
} tsdb_stats_t;

/**
 * @brief Get database statistics
 *
 * @param stats Output statistics structure
 * @return ESP_OK on success
 */
esp_err_t tsdb_get_stats(tsdb_stats_t *stats);

/**
 * @brief Clear all data (keeps structure, resets to empty)
 *
 * @return ESP_OK on success
 */
esp_err_t tsdb_clear(void);

/**
 * @brief Delete database file completely
 *
 * @return ESP_OK on success
 */
esp_err_t tsdb_delete(void);

// ============================================================================
// OVERFLOW PARAMETER API
// ============================================================================

/**
 * @brief Add extra parameters via overflow region
 *
 * @param param_names Array of parameter name strings
 * @param count Number of extra parameters (1-48)
 * @return ESP_OK on success
 */
esp_err_t tsdb_add_extra_params(const char **param_names, uint8_t count);

/**
 * @brief Migrate overflow region to new parameter layout
 *
 * Handles adding, removing, and reordering overflow columns.
 * Matching columns (by name) are copied, new columns get zeros,
 * dropped columns are discarded. Crash-safe: new data written
 * first, header updated last.
 *
 * @param new_names Array of new param name strings (NULL if new_count==0)
 * @param new_count Number of new extra parameters (0 = remove overflow)
 * @return ESP_OK on success
 */
esp_err_t tsdb_migrate_overflow(const char **new_names, uint8_t new_count);

/**
 * @brief Get total parameter count (base + extra)
 */
uint8_t tsdb_get_total_params(void);

/**
 * @brief Get parameter name by index (base or extra)
 */
const char* tsdb_get_param_name(uint8_t index);

/**
 * @brief Check if overflow region is active
 */
bool tsdb_has_overflow(void);

// ============================================================================
// BENCHMARK
// ============================================================================

/**
 * @brief Benchmark results
 */
typedef struct {
    // Write performance
    uint32_t writes_count;
    uint32_t write_total_us;
    uint32_t write_min_us;
    uint32_t write_max_us;
    float    write_avg_us;

    // Full query (all params)
    uint32_t query_records;
    uint32_t query_total_us;
    float    query_per_record_us;

    // Single param query
    uint32_t single_param_records;
    uint32_t single_param_total_us;
    float    single_param_per_record_us;

    // Aggregation (single)
    uint32_t agg_records;
    uint32_t agg_total_us;

    // Multi-aggregation
    uint32_t multi_agg_records;
    uint32_t multi_agg_total_us;

    // Overflow write
    uint32_t overflow_writes_count;
    uint32_t overflow_write_total_us;
    float    overflow_write_avg_us;

    // Overflow query
    uint32_t overflow_query_records;
    uint32_t overflow_query_total_us;
    float    overflow_query_per_record_us;

    // Metadata
    uint32_t file_size_bytes;
    uint8_t  num_params;
    uint8_t  extra_params;
} tsdb_benchmark_results_t;

/**
 * @brief Run on-device benchmark
 *
 * Creates a temporary database, writes records, queries them, and measures
 * timing for all operations. Cleans up after itself.
 *
 * WARNING: Closes any currently open TSDB. Caller must re-init after.
 *
 * @param num_records Number of records to write (e.g., 500, 1000)
 * @param num_params Number of base params (1-16)
 * @param results Output benchmark results
 * @return ESP_OK on success
 */
esp_err_t tsdb_run_benchmark(uint32_t num_records, uint8_t num_params,
                              tsdb_benchmark_results_t *results);

// ============================================================================
// HANDLE-BASED API (multi-instance, v2.1+)
// ============================================================================
//
// Every legacy global function has an _h-suffixed counterpart that takes a
// tsdb_t handle as its first argument. Use these when you need more than
// one database open simultaneously (e.g., one for system metrics, one for
// per-panel telemetry). All functions are safe to call from multiple tasks
// against the same handle — each handle owns a FreeRTOS mutex internally.

/**
 * @brief Open a new database handle.
 *
 * Creates or opens the file at config->filepath, allocates the buffer pool,
 * and initialises the per-instance mutex. The returned handle is independent
 * of any other handle (including the default one used by the legacy API).
 *
 * @param config Configuration (same fields as tsdb_init).
 * @return Handle on success, NULL on failure (check the log for details).
 */
tsdb_t *tsdb_open(const tsdb_config_t *config);

/**
 * @brief Close a handle, flush buffers, free resources.
 *
 * Safe to call with NULL (no-op).
 *
 * @param db Handle returned from tsdb_open(). Must not be reused after.
 * @return ESP_OK on success.
 */
esp_err_t tsdb_close_h(tsdb_t *db);

/**
 * @brief Force a directory-entry commit by close+reopen of the underlying FILE*.
 *
 * On esp_littlefs (joltwallet/littlefs), files held open with fopen("r+b")
 * across many writes never trigger a directory commit even with fflush+fsync
 * after each write — only fclose actually publishes the dir entry to disk.
 * Without this, every reboot finds the file "missing" and tsdb_open recreates
 * it from scratch, losing all history.
 *
 * tsdb_sync_h takes the per-handle mutex, fcloses db->file, reopens it with
 * "r+b", and re-validates the on-disk header against the in-memory copy. The
 * handle and all internal state survive — callers may continue using db after
 * this returns successfully. Cost is one filesystem round-trip (~30-50 ms on
 * LittleFS); cheap enough to call after every tsdb_write_h on snapshot
 * cadences (5-min) or every N writes on faster cadences.
 *
 * Returns ESP_ERR_INVALID_STATE if db is null or not open, ESP_FAIL if the
 * close+reopen cycle leaves the file in an inconsistent state (rare; in that
 * case the handle's is_open is set false and callers should treat the DB as
 * closed).
 */
esp_err_t tsdb_sync_h(tsdb_t *db);

bool tsdb_is_initialized_h(const tsdb_t *db);

esp_err_t tsdb_write_h(tsdb_t *db, uint32_t timestamp, const int16_t *values);

esp_err_t tsdb_write_batch_h(tsdb_t *db,
                             const uint32_t *timestamps,
                             const int16_t **values,
                             uint32_t count);

esp_err_t tsdb_query_init_h(tsdb_t *db,
                            tsdb_query_t *query,
                            uint32_t start_time,
                            uint32_t end_time,
                            const uint8_t *param_indices,
                            uint8_t num_params_to_fetch);

// tsdb_query_next() and tsdb_query_close() use the handle stored inside the
// tsdb_query_t itself — no _h variant needed.

esp_err_t tsdb_query_count_h(tsdb_t *db,
                             uint32_t start_time,
                             uint32_t end_time,
                             uint32_t *count);

esp_err_t tsdb_aggregate_h(tsdb_t *db,
                           uint32_t start_time,
                           uint32_t end_time,
                           uint8_t param_index,
                           tsdb_agg_type_t agg_type,
                           int32_t *result);

esp_err_t tsdb_aggregate_multi_h(tsdb_t *db,
                                 uint32_t start_time,
                                 uint32_t end_time,
                                 tsdb_agg_request_t *requests,
                                 uint8_t num_requests,
                                 uint32_t *record_count);

esp_err_t tsdb_get_stats_h(tsdb_t *db, tsdb_stats_t *stats);

esp_err_t tsdb_clear_h(tsdb_t *db);

esp_err_t tsdb_delete_h(tsdb_t *db);

esp_err_t tsdb_add_extra_params_h(tsdb_t *db, const char **param_names, uint8_t count);

esp_err_t tsdb_migrate_overflow_h(tsdb_t *db, const char **new_names, uint8_t new_count);

uint8_t tsdb_get_total_params_h(const tsdb_t *db);

const char *tsdb_get_param_name_h(const tsdb_t *db, uint8_t index);

bool tsdb_has_overflow_h(const tsdb_t *db);

#ifdef __cplusplus
}
#endif

#endif // ESP_TSDB_H
