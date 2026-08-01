/**
 * @file tsdb_query.c
 * @brief Query and aggregation operations with streaming
 */

#include "tsdb_internal.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "TSDB_QUERY";

// ============================================================================
// QUERY OPERATIONS
// ============================================================================

esp_err_t tsdb_query_init_h(tsdb_t *db,
                            tsdb_query_t *query,
                            uint32_t start_time,
                            uint32_t end_time,
                            const uint8_t *param_indices,
                            uint8_t num_params_to_fetch) {
    if (db == NULL || !db->is_open || query == NULL) {
        ESP_LOGE(TAG, "Invalid state or NULL query");
        return ESP_ERR_INVALID_STATE;
    }

    if (start_time > end_time) {
        ESP_LOGE(TAG, "Invalid time range: start > end");
        return ESP_ERR_INVALID_ARG;
    }

    memset(query, 0, sizeof(tsdb_query_t));

    // Bind handle to the query iterator. tsdb_query_next() / _close() use
    // this to access the underlying file + overflow state without a separate
    // handle parameter (preserves the v2 API shape).
    query->db = db;

    // Copy header
    memcpy(&query->header, &db->header, sizeof(tsdb_header_t));

    // Set query parameters
    query->start_time = start_time;
    query->end_time = end_time;
    query->file = db->file;

    // Setup parameter indices
    if (param_indices == NULL) {
        // Fetch all parameters (base + extra)
        uint8_t total = query->header.num_params + db->extra_param_count;
        query->num_params_to_fetch = total;
        for (uint8_t i = 0; i < total; i++) {
            query->param_indices[i] = i;
        }
    } else {
        query->num_params_to_fetch = num_params_to_fetch;
        if (num_params_to_fetch > TSDB_MAX_PARAMS) {
            ESP_LOGE(TAG, "Too many parameters requested: %d", num_params_to_fetch);
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(query->param_indices, param_indices, num_params_to_fetch);
    }

    // Try to use query buffer from pool
    query->block_buffer = (tsdb_block_t*)tsdb_get_buffer_ptr(&db->pool,
                                                              db->query_buffer_offset,
                                                              sizeof(tsdb_block_t));

    if (query->block_buffer == NULL) {
        // Paged mode and spans pages - allocate separately
        query->block_buffer = heap_caps_malloc(sizeof(tsdb_block_t), MALLOC_CAP_8BIT);
        if (query->block_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate query block buffer");
            return ESP_ERR_NO_MEM;
        }
        query->owns_buffer = true;
        ESP_LOGD(TAG, "Allocated separate query buffer at %p", query->block_buffer);
    } else {
        query->owns_buffer = false;
        ESP_LOGD(TAG, "Using query buffer from pool at %p", query->block_buffer);
    }

    // Walk the ring in logical (time-ascending) order. Records are written at
    // slot = total_records % max_records, and on eviction oldest_record_idx
    // advances, so reading slots oldest_record_idx, +1, +2, ... (mod
    // max_records) yields strictly ascending timestamps.
    //
    // We do NOT use the sparse index to seek to start_time: once the ring has
    // wrapped, index entries are ordered by physical slot (the slot they were
    // last written at), not by timestamp, so a binary search on them is
    // invalid and lands in the wrong place. A full logical scan with an
    // early-exit once ts > end_time is correct for every range and is bounded
    // by `available` record reads (one pass over the ring).
    bool unlimited = (query->header.max_records == 0);
    uint32_t available_records = unlimited ? query->header.total_records :
                                 ((query->header.total_records < query->header.max_records) ?
                                  query->header.total_records : query->header.max_records);

    uint16_t rpb = query->header.records_per_block;
    uint32_t first_slot = unlimited ? 0 : query->header.oldest_record_idx;

    query->current_record_idx = first_slot;       // current absolute ring slot
    query->end_record_idx = available_records;     // total records to scan
    query->records_scanned = 0;                    // ring slots consumed so far
    query->current_block_num = rpb ? (first_slot / rpb) : 0;
    query->offset_in_block = rpb ? (first_slot % rpb) : 0;
    query->block_loaded = false;

    ESP_LOGD(TAG, "Query init: time=[%lu, %lu], params=%d, scan %lu records from slot %lu",
             (unsigned long)start_time, (unsigned long)end_time,
             query->num_params_to_fetch,
             (unsigned long)available_records, (unsigned long)first_slot);

    return ESP_OK;
}

esp_err_t tsdb_query_next(tsdb_query_t *query,
                          uint32_t *timestamp,
                          int16_t *values) {
    if (query == NULL || query->db == NULL || timestamp == NULL || values == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    tsdb_t *db = query->db;

    bool unlimited = (query->header.max_records == 0);
    uint32_t available = query->end_record_idx;   // total records to scan (fixed)
    uint16_t qrpb = query->header.records_per_block;

    // Walk the ring one slot at a time in logical (time-ascending) order,
    // wrapping the physical slot/block modulo max_records. Stop once we've
    // visited every retained record or passed end_time.
    while (query->records_scanned < available) {
        if (!query->block_loaded) {
            if (tsdb_read_block(db, query->current_block_num,
                                query->block_buffer) != ESP_OK) {
                // Every slot inside the ring has a backing block; a read
                // failure here means truncation/corruption — stop cleanly.
                ESP_LOGD(TAG, "Block %lu unreadable mid-ring",
                         (unsigned long)query->current_block_num);
                return ESP_ERR_NOT_FOUND;
            }
            query->block_loaded = true;
        }

        uint8_t *qraw = (uint8_t *)query->block_buffer;
        uint16_t cur_off = query->offset_in_block;
        uint32_t ts = TSDB_BLOCK_TS(qraw, cur_off);

        // Absolute record index (in total_records space) of the slot we are
        // about to read; the k-th oldest retained record maps to
        // (total_records - available) + k. Needed for overflow lookups.
        uint32_t abs_record_idx = (query->header.total_records - available) +
                                  query->records_scanned;

        // Advance the ring cursor to the next slot (wrap modulo max_records).
        uint32_t next_slot = unlimited ? (query->current_record_idx + 1) :
                             ((query->current_record_idx + 1) % query->header.max_records);
        uint32_t next_block = qrpb ? (next_slot / qrpb) : 0;
        if (next_block != query->current_block_num) {
            query->current_block_num = next_block;
            query->block_loaded = false;
        }
        query->offset_in_block = qrpb ? (next_slot % qrpb) : 0;
        query->current_record_idx = next_slot;
        query->records_scanned++;

        if (ts == 0)
            continue;                       // uninitialized slot
        if (ts > query->end_time)
            return ESP_ERR_NOT_FOUND;       // ascending order -> no later matches
        if (ts < query->start_time)
            continue;                       // before the window

        // In range -> emit this record.
        *timestamp = ts;
        for (uint8_t i = 0; i < query->num_params_to_fetch; i++) {
            uint8_t param_idx = query->param_indices[i];
            if (param_idx < query->header.num_params) {
                values[i] = TSDB_BLOCK_PARAM(qraw, qrpb, param_idx, cur_off);
            } else if (db->extra_param_count > 0 &&
                       param_idx < (query->header.num_params + db->extra_param_count) &&
                       abs_record_idx >= db->first_overflow_record_idx) {
                uint32_t overflow_idx = abs_record_idx - db->first_overflow_record_idx;
                uint8_t extra_idx = param_idx - query->header.num_params;
                uint32_t ovf_offset = db->overflow_data_offset +
                                     (overflow_idx * db->overflow_record_size) +
                                     (extra_idx * sizeof(int16_t));

                long saved_pos = ftell(query->file);
                fseek(query->file, ovf_offset, SEEK_SET);
                int16_t extra_val = 0;
                if (fread(&extra_val, sizeof(int16_t), 1, query->file) == 1) {
                    values[i] = extra_val;
                } else {
                    values[i] = 0;
                }
                fseek(query->file, saved_pos, SEEK_SET);
            } else {
                values[i] = 0;  // Pre-overflow or invalid index
            }
        }

        ESP_LOGD(TAG, "Found record: ts=%lu", (unsigned long)ts);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

void tsdb_query_close(tsdb_query_t *query) {
    if (query == NULL) {
        return;
    }

    // Free buffer if we allocated it separately
    if (query->owns_buffer && query->block_buffer != NULL) {
        free(query->block_buffer);
        query->block_buffer = NULL;
        ESP_LOGD(TAG, "Freed separate query buffer");
    }

    memset(query, 0, sizeof(tsdb_query_t));
}

esp_err_t tsdb_query_count_h(tsdb_t *db,
                             uint32_t start_time,
                             uint32_t end_time,
                             uint32_t *count) {
    if (db == NULL || !db->is_open || count == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    *count = 0;

    // Initialize query
    tsdb_query_t query;
    esp_err_t ret = tsdb_query_init_h(db, &query, start_time, end_time, NULL, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    // Count records
    uint32_t ts;
    int16_t values[TSDB_MAX_PARAMS];

    while (tsdb_query_next(&query, &ts, values) == ESP_OK) {
        (*count)++;
    }

    tsdb_query_close(&query);

    ESP_LOGI(TAG, "Counted %lu records in range [%lu, %lu]",
             (unsigned long)*count, (unsigned long)start_time, (unsigned long)end_time);

    return ESP_OK;
}

// ============================================================================
// AGGREGATION OPERATIONS
// ============================================================================

esp_err_t tsdb_aggregate_h(tsdb_t *db,
                           uint32_t start_time,
                           uint32_t end_time,
                           uint8_t param_index,
                           tsdb_agg_type_t agg_type,
                           int32_t *result) {
    if (db == NULL || !db->is_open || result == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (param_index >= (db->header.num_params + db->extra_param_count)) {
        ESP_LOGE(TAG, "Invalid parameter index: %d", param_index);
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize query for single parameter
    uint8_t params[] = {param_index};
    tsdb_query_t query;
    esp_err_t ret = tsdb_query_init_h(db, &query, start_time, end_time, params, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // Accumulators
    int64_t sum = 0;
    int32_t min_val = INT32_MAX;
    int32_t max_val = INT32_MIN;
    uint32_t count = 0;
    int16_t first_val = 0;
    int16_t last_val = 0;

    // Single-pass aggregation
    uint32_t ts;
    int16_t value;

    while (tsdb_query_next(&query, &ts, &value) == ESP_OK) {
        if (count == 0) {
            first_val = value;
        }
        last_val = value;

        sum += value;
        count++;

        if (value < min_val) min_val = value;
        if (value > max_val) max_val = value;
    }

    tsdb_query_close(&query);

    // Calculate result based on aggregation type
    if (count == 0) {
        *result = 0;
        ESP_LOGW(TAG, "No records found in range");
        return ESP_OK;
    }

    switch (agg_type) {
        case TSDB_AGG_SUM:
            *result = (int32_t)sum;
            break;
        case TSDB_AGG_AVG:
            *result = (int32_t)(sum / count);
            break;
        case TSDB_AGG_MIN:
            *result = min_val;
            break;
        case TSDB_AGG_MAX:
            *result = max_val;
            break;
        case TSDB_AGG_COUNT:
            *result = count;
            break;
        case TSDB_AGG_FIRST:
            *result = first_val;
            break;
        case TSDB_AGG_LAST:
            *result = last_val;
            break;
        default:
            ESP_LOGE(TAG, "Unknown aggregation type: %d", agg_type);
            return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Aggregation complete: type=%d, result=%ld, count=%lu",
             agg_type, (long)*result, (unsigned long)count);

    return ESP_OK;
}

esp_err_t tsdb_aggregate_multi_h(tsdb_t *db,
                                  uint32_t start_time,
                                  uint32_t end_time,
                                  tsdb_agg_request_t *requests,
                                  uint8_t num_requests,
                                  uint32_t *record_count) {
    if (db == NULL || !db->is_open || requests == NULL || num_requests == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    // Collect unique param indices needed
    uint8_t unique_params[TSDB_MAX_PARAMS];
    uint8_t num_unique = 0;
    for (uint8_t i = 0; i < num_requests; i++) {
        if (requests[i].param_index >= (db->header.num_params + db->extra_param_count)) {
            ESP_LOGE(TAG, "Invalid parameter index: %d", requests[i].param_index);
            return ESP_ERR_INVALID_ARG;
        }
        // Check if already in unique list
        bool found = false;
        for (uint8_t j = 0; j < num_unique; j++) {
            if (unique_params[j] == requests[i].param_index) {
                found = true;
                break;
            }
        }
        if (!found) {
            unique_params[num_unique++] = requests[i].param_index;
        }
    }

    // Open single query for all needed params
    tsdb_query_t query;
    esp_err_t ret = tsdb_query_init_h(db, &query, start_time, end_time, unique_params, num_unique);
    if (ret != ESP_OK) {
        return ret;
    }

    // Per-request accumulators
    typedef struct {
        int64_t sum;
        int32_t min_val;
        int32_t max_val;
        int16_t first_val;
        int16_t last_val;
    } agg_acc_t;

    agg_acc_t acc[TSDB_MAX_PARAMS];
    for (uint8_t i = 0; i < num_requests; i++) {
        acc[i].sum = 0;
        acc[i].min_val = INT32_MAX;
        acc[i].max_val = INT32_MIN;
        acc[i].first_val = 0;
        acc[i].last_val = 0;
    }

    // Build a mapping: for each request, which index in the query values array has its param?
    uint8_t req_to_query_idx[TSDB_MAX_PARAMS];
    for (uint8_t i = 0; i < num_requests; i++) {
        for (uint8_t j = 0; j < num_unique; j++) {
            if (unique_params[j] == requests[i].param_index) {
                req_to_query_idx[i] = j;
                break;
            }
        }
    }

    // Single pass
    uint32_t ts;
    int16_t values[TSDB_MAX_PARAMS];
    uint32_t count = 0;

    while (tsdb_query_next(&query, &ts, values) == ESP_OK) {
        for (uint8_t i = 0; i < num_requests; i++) {
            int16_t val = values[req_to_query_idx[i]];
            if (count == 0) {
                acc[i].first_val = val;
            }
            acc[i].last_val = val;
            acc[i].sum += val;
            if (val < acc[i].min_val) acc[i].min_val = val;
            if (val > acc[i].max_val) acc[i].max_val = val;
        }
        count++;
    }

    tsdb_query_close(&query);

    // Compute results
    for (uint8_t i = 0; i < num_requests; i++) {
        if (count == 0) {
            requests[i].result = 0;
            continue;
        }
        switch (requests[i].agg_type) {
            case TSDB_AGG_SUM:   requests[i].result = (int32_t)acc[i].sum; break;
            case TSDB_AGG_AVG:   requests[i].result = (int32_t)(acc[i].sum / count); break;
            case TSDB_AGG_MIN:   requests[i].result = acc[i].min_val; break;
            case TSDB_AGG_MAX:   requests[i].result = acc[i].max_val; break;
            case TSDB_AGG_COUNT: requests[i].result = (int32_t)count; break;
            case TSDB_AGG_FIRST: requests[i].result = acc[i].first_val; break;
            case TSDB_AGG_LAST:  requests[i].result = acc[i].last_val; break;
            default:             requests[i].result = 0; break;
        }
    }

    if (record_count) {
        *record_count = count;
    }

    ESP_LOGI(TAG, "Multi-aggregate complete: %d requests, %lu records scanned",
             num_requests, (unsigned long)count);

    return ESP_OK;
}

// ============================================================================
// LEGACY GLOBAL API
// ============================================================================

esp_err_t tsdb_query_init(tsdb_query_t *query,
                          uint32_t start_time,
                          uint32_t end_time,
                          const uint8_t *param_indices,
                          uint8_t num_params_to_fetch) {
    return tsdb_query_init_h(g_default_handle, query, start_time, end_time,
                             param_indices, num_params_to_fetch);
}

esp_err_t tsdb_query_count(uint32_t start_time,
                           uint32_t end_time,
                           uint32_t *count) {
    return tsdb_query_count_h(g_default_handle, start_time, end_time, count);
}

esp_err_t tsdb_aggregate(uint32_t start_time,
                         uint32_t end_time,
                         uint8_t param_index,
                         tsdb_agg_type_t agg_type,
                         int32_t *result) {
    return tsdb_aggregate_h(g_default_handle, start_time, end_time,
                            param_index, agg_type, result);
}

esp_err_t tsdb_aggregate_multi(uint32_t start_time,
                                uint32_t end_time,
                                tsdb_agg_request_t *requests,
                                uint8_t num_requests,
                                uint32_t *record_count) {
    return tsdb_aggregate_multi_h(g_default_handle, start_time, end_time,
                                   requests, num_requests, record_count);
}
