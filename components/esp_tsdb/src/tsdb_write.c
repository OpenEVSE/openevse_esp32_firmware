/**
 * @file tsdb_write.c
 * @brief Write operations with LRU eviction and ring buffer
 */

#include "tsdb_internal.h"
#include "esp_log.h"
#include <string.h>
#include <unistd.h>

static const char *TAG = "TSDB_WRITE";

// ============================================================================
// BLOCK I/O
// ============================================================================

/**
 * @brief Read a data block from file
 */
esp_err_t tsdb_read_block(tsdb_t *db, uint32_t block_num, tsdb_block_t *block) {
    if (db == NULL || db->file == NULL || block == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t block_offset = tsdb_calc_block_offset(&db->header, block_num);

    fseek(db->file, block_offset, SEEK_SET);
    size_t read = fread(block, TSDB_BLOCK_SIZE, 1, db->file);

    if (read != 1) {
        ESP_LOGD(TAG, "Block %lu not found or uninitialized", (unsigned long)block_num);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/**
 * @brief Write a data block to file
 */
esp_err_t tsdb_write_block(tsdb_t *db, uint32_t block_num, const tsdb_block_t *block) {
    if (db == NULL || db->file == NULL || block == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t block_offset = tsdb_calc_block_offset(&db->header, block_num);

    fseek(db->file, block_offset, SEEK_SET);
    size_t written = fwrite(block, TSDB_BLOCK_SIZE, 1, db->file);
    fflush(db->file);
    fsync(fileno(db->file));

    if (written != 1) {
        ESP_LOGE(TAG, "Failed to write block %lu", (unsigned long)block_num);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Wrote block %lu at offset %lu",
             (unsigned long)block_num, (unsigned long)block_offset);

    return ESP_OK;
}

// ============================================================================
// WRITE OPERATIONS
// ============================================================================

esp_err_t tsdb_write_h(tsdb_t *db, uint32_t timestamp, const int16_t *values) {
    if (db == NULL || !db->is_open) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (values == NULL) {
        ESP_LOGE(TAG, "NULL values pointer");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(db->mutex, portMAX_DELAY);

    // Calculate record index (ring buffer or unlimited)
    bool unlimited = (db->header.max_records == 0);
    uint32_t record_idx = unlimited ? db->header.total_records :
                          (db->header.total_records % db->header.max_records);

    // Determine if this overwrites old data (LRU eviction) — never in unlimited mode
    bool is_eviction = (!unlimited && db->header.total_records >= db->header.max_records);

    if (is_eviction) {
        db->header.total_evictions++;
        db->header.oldest_record_idx = (db->header.oldest_record_idx + 1) %
                                           db->header.max_records;
        ESP_LOGD(TAG, "LRU eviction: oldest_idx=%lu",
                 (unsigned long)db->header.oldest_record_idx);
    }

    // Calculate block number and offset within block
    uint32_t block_num = record_idx / db->header.records_per_block;
    uint16_t offset_in_block = record_idx % db->header.records_per_block;

    ESP_LOGD(TAG, "Writing record %lu: block=%lu, offset=%d",
             (unsigned long)db->header.total_records,
             (unsigned long)block_num, offset_in_block);

    // Get pointer to write buffer in pool
    tsdb_block_t *block = (tsdb_block_t*)tsdb_get_buffer_ptr(&db->pool,
                                                              db->write_cache_offset,
                                                              sizeof(tsdb_block_t));

    tsdb_block_t temp_block;
    if (block == NULL) {
        // Paged mode and spans pages, use temp buffer
        block = &temp_block;
    }

    // Read existing block
    esp_err_t ret = tsdb_read_block(db, block_num, block);

    // Initialize block if new or read failed
    uint8_t *raw_blk = (uint8_t *)block;
    if (ret != ESP_OK || TSDB_BLOCK_MAGIC(raw_blk) != 0x424C4B54) {
        ESP_LOGD(TAG, "Initializing new block %lu", (unsigned long)block_num);
        memset(block, 0, TSDB_BLOCK_SIZE);
        TSDB_BLOCK_MAGIC(raw_blk) = 0x424C4B54;  // "BLKT"
        TSDB_BLOCK_COUNT(raw_blk) = 0;
    }

    // Write data in columnar format (runtime offsets for correct disk layout)
    uint16_t rpb = db->header.records_per_block;
    uint8_t *raw = (uint8_t *)block;
    TSDB_BLOCK_TS(raw, offset_in_block) = timestamp;
    for (uint8_t i = 0; i < db->header.num_params; i++) {
        TSDB_BLOCK_PARAM(raw, rpb, i, offset_in_block) = values[i];
    }

    // Update block record count
    if (offset_in_block >= TSDB_BLOCK_COUNT(raw)) {
        TSDB_BLOCK_COUNT(raw) = offset_in_block + 1;
    }

    // Write block back to file
    ret = tsdb_write_block(db, block_num, block);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write block");
        xSemaphoreGive(db->mutex);
        return ret;
    }

    // Write overflow data if active
    if (db->extra_param_count > 0 &&
        db->header.total_records >= db->first_overflow_record_idx) {
        uint32_t overflow_idx = db->header.total_records - db->first_overflow_record_idx;
        uint32_t ovf_offset = db->overflow_data_offset +
                              (overflow_idx * db->overflow_record_size);

        fseek(db->file, ovf_offset, SEEK_SET);
        size_t ovf_written = fwrite(&values[db->header.num_params],
                                     sizeof(int16_t),
                                     db->extra_param_count,
                                     db->file);
        if (ovf_written != db->extra_param_count) {
            ESP_LOGE(TAG, "Failed to write overflow data");
            // Don't fail the whole write -- base data is already written
        }
    }

    // Update header
    db->header.total_records++;
    db->header.total_writes++;
    db->header.newest_record_idx = record_idx;
    db->header.newest_timestamp = timestamp;

    // Update oldest timestamp if needed
    if (is_eviction) {
        // Read oldest timestamp from file
        uint32_t oldest_block = db->header.oldest_record_idx / db->header.records_per_block;
        uint16_t oldest_offset = db->header.oldest_record_idx % db->header.records_per_block;

        tsdb_block_t *oldest_block_data = (tsdb_block_t*)tsdb_get_buffer_ptr(&db->pool,
                                                                              db->read_buffer_offset,
                                                                              sizeof(tsdb_block_t));
        tsdb_block_t temp_oldest;
        if (oldest_block_data == NULL) {
            oldest_block_data = &temp_oldest;
        }

        if (tsdb_read_block(db, oldest_block, oldest_block_data) == ESP_OK) {
            db->header.oldest_timestamp = TSDB_BLOCK_TS((uint8_t *)oldest_block_data, oldest_offset);
        }
    } else if (db->header.total_records == 1) {
        db->header.oldest_timestamp = timestamp;
    }

    // Update sparse index if at stride boundary
    if (record_idx % db->header.index_stride == 0) {
        uint32_t index_entry_num = record_idx / db->header.index_stride;
        tsdb_index_entry_t entry = {
            .timestamp = timestamp,
            .block_number = block_num
        };

        uint32_t index_file_offset = db->header.index_offset +
                                     (index_entry_num * sizeof(tsdb_index_entry_t));

        fseek(db->file, index_file_offset, SEEK_SET);
        fwrite(&entry, sizeof(tsdb_index_entry_t), 1, db->file);

        ESP_LOGD(TAG, "Updated index entry %lu: timestamp=%lu, block=%lu",
                 (unsigned long)index_entry_num,
                 (unsigned long)timestamp,
                 (unsigned long)block_num);
    }

    // Update header in file
    tsdb_write_header(db->file, &db->header);
    fflush(db->file);
    fsync(fileno(db->file));

    ESP_LOGD(TAG, "Write complete: total_records=%lu, newest_ts=%lu",
             (unsigned long)db->header.total_records,
             (unsigned long)db->header.newest_timestamp);

    xSemaphoreGive(db->mutex);
    return ESP_OK;
}

esp_err_t tsdb_write_batch_h(tsdb_t *db,
                              const uint32_t *timestamps,
                              const int16_t **values,
                              uint32_t count) {
    if (db == NULL || !db->is_open) {
        return ESP_ERR_INVALID_STATE;
    }

    if (timestamps == NULL || values == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Batch writing %lu records", (unsigned long)count);

    // Write records one by one. tsdb_write_h takes the mutex per call.
    // TODO: Optimize by batching block writes under a single lock acquire.
    for (uint32_t i = 0; i < count; i++) {
        esp_err_t ret = tsdb_write_h(db, timestamps[i], values[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write record %lu in batch", (unsigned long)i);
            return ret;
        }
    }

    ESP_LOGI(TAG, "Batch write complete: %lu records", (unsigned long)count);

    return ESP_OK;
}

// ============================================================================
// LEGACY GLOBAL API
// ============================================================================

esp_err_t tsdb_write(uint32_t timestamp, const int16_t *values) {
    return tsdb_write_h(g_default_handle, timestamp, values);
}

esp_err_t tsdb_write_batch(const uint32_t *timestamps,
                            const int16_t **values,
                            uint32_t count) {
    return tsdb_write_batch_h(g_default_handle, timestamps, values, count);
}
