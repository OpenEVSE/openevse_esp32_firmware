/**
 * @file tsdb_benchmark.c
 * @brief On-device benchmark for TSDB read/write/query performance
 */

#include "esp_tsdb.h"
#include "tsdb_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "TSDB_BENCH";

esp_err_t tsdb_run_benchmark(uint32_t num_records, uint8_t num_params,
                              tsdb_benchmark_results_t *results) {
    if (results == NULL || num_records == 0 || num_params == 0 || num_params > 16) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(results, 0, sizeof(tsdb_benchmark_results_t));
    results->num_params = num_params;

    // Close existing DB if open
    if (tsdb_is_initialized()) {
        ESP_LOGW(TAG, "Closing existing TSDB for benchmark");
        tsdb_close();
    }

    // Delete any existing benchmark file
    const char *bench_path = "/littlefs/bench.tsdb";
    unlink(bench_path);

    // Build param names
    const char *param_names[16];
    char name_buf[16][8];
    for (uint8_t i = 0; i < num_params; i++) {
        snprintf(name_buf[i], sizeof(name_buf[i]), "P%d", i);
        param_names[i] = name_buf[i];
    }

    // Init benchmark DB
    tsdb_config_t config = {
        .filepath = bench_path,
        .num_params = num_params,
        .param_names = param_names,
        .max_records = num_records + 100,  // Slightly more than we'll write
        .index_stride = 380,
        .buffer_pool_size = 10 * 1024,
        .alloc_strategy = TSDB_ALLOC_AUTO,
        .use_paged_allocation = true,
        .page_size = 2048
    };

    esp_err_t ret = tsdb_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init benchmark DB: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "=== TSDB Benchmark: %lu records, %d params ===",
             (unsigned long)num_records, num_params);

    // ========================================================================
    // WRITE BENCHMARK
    // ========================================================================
    int16_t values[16];
    uint32_t base_ts = 1700000000;  // Fixed base timestamp
    uint32_t write_min = UINT32_MAX;
    uint32_t write_max = 0;
    uint64_t write_total = 0;

    ESP_LOGI(TAG, "--- Write Benchmark ---");

    for (uint32_t i = 0; i < num_records; i++) {
        // Generate test values
        for (uint8_t p = 0; p < num_params; p++) {
            values[p] = (int16_t)((i * 7 + p * 13) % 10000);
        }

        uint64_t start = esp_timer_get_time();
        ret = tsdb_write(base_ts + (i * 300), values);  // 5-min intervals
        uint64_t elapsed = esp_timer_get_time() - start;

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at record %lu", (unsigned long)i);
            tsdb_close();
            unlink(bench_path);
            return ret;
        }

        uint32_t elapsed_us = (uint32_t)elapsed;
        write_total += elapsed_us;
        if (elapsed_us < write_min) write_min = elapsed_us;
        if (elapsed_us > write_max) write_max = elapsed_us;
    }

    results->writes_count = num_records;
    results->write_total_us = (uint32_t)write_total;
    results->write_min_us = write_min;
    results->write_max_us = write_max;
    results->write_avg_us = (float)write_total / num_records;

    ESP_LOGI(TAG, "Writes: %lu records in %lu ms (avg=%.1f us, min=%lu us, max=%lu us)",
             (unsigned long)num_records,
             (unsigned long)(write_total / 1000),
             results->write_avg_us,
             (unsigned long)write_min,
             (unsigned long)write_max);

    // ========================================================================
    // FULL QUERY BENCHMARK (all params)
    // ========================================================================
    ESP_LOGI(TAG, "--- Full Query Benchmark ---");

    uint32_t query_start_ts = base_ts;
    uint32_t query_end_ts = base_ts + (num_records * 300);

    tsdb_query_t query;
    uint32_t ts;
    int16_t qvalues[16];
    uint32_t query_count = 0;

    uint64_t q_start = esp_timer_get_time();
    ret = tsdb_query_init(&query, query_start_ts, query_end_ts, NULL, 0);
    if (ret == ESP_OK) {
        while (tsdb_query_next(&query, &ts, qvalues) == ESP_OK) {
            query_count++;
        }
        tsdb_query_close(&query);
    }
    uint64_t q_elapsed = esp_timer_get_time() - q_start;

    results->query_records = query_count;
    results->query_total_us = (uint32_t)q_elapsed;
    results->query_per_record_us = query_count > 0 ? (float)q_elapsed / query_count : 0;

    ESP_LOGI(TAG, "Full query: %lu records in %lu ms (%.1f us/record)",
             (unsigned long)query_count,
             (unsigned long)(q_elapsed / 1000),
             results->query_per_record_us);

    // ========================================================================
    // SINGLE PARAM QUERY BENCHMARK
    // ========================================================================
    ESP_LOGI(TAG, "--- Single Param Query Benchmark ---");

    uint8_t single_param[] = {0};
    query_count = 0;
    int16_t single_val;

    q_start = esp_timer_get_time();
    ret = tsdb_query_init(&query, query_start_ts, query_end_ts, single_param, 1);
    if (ret == ESP_OK) {
        while (tsdb_query_next(&query, &ts, &single_val) == ESP_OK) {
            query_count++;
        }
        tsdb_query_close(&query);
    }
    q_elapsed = esp_timer_get_time() - q_start;

    results->single_param_records = query_count;
    results->single_param_total_us = (uint32_t)q_elapsed;
    results->single_param_per_record_us = query_count > 0 ? (float)q_elapsed / query_count : 0;

    ESP_LOGI(TAG, "Single param query: %lu records in %lu ms (%.1f us/record)",
             (unsigned long)query_count,
             (unsigned long)(q_elapsed / 1000),
             results->single_param_per_record_us);

    // ========================================================================
    // AGGREGATION BENCHMARK (single)
    // ========================================================================
    ESP_LOGI(TAG, "--- Aggregation Benchmark ---");

    int32_t agg_result;
    q_start = esp_timer_get_time();
    ret = tsdb_aggregate(query_start_ts, query_end_ts, 0, TSDB_AGG_AVG, &agg_result);
    q_elapsed = esp_timer_get_time() - q_start;

    results->agg_records = num_records;
    results->agg_total_us = (uint32_t)q_elapsed;

    ESP_LOGI(TAG, "Aggregation (AVG): %lu ms over %lu records (result=%ld)",
             (unsigned long)(q_elapsed / 1000),
             (unsigned long)num_records,
             (long)agg_result);

    // ========================================================================
    // MULTI-AGGREGATION BENCHMARK
    // ========================================================================
    ESP_LOGI(TAG, "--- Multi-Aggregation Benchmark ---");

    uint8_t num_aggs = num_params < 8 ? num_params : 8;
    tsdb_agg_request_t requests[8];
    for (uint8_t i = 0; i < num_aggs; i++) {
        requests[i].param_index = i % num_params;
        requests[i].agg_type = (i % 2 == 0) ? TSDB_AGG_AVG : TSDB_AGG_MAX;
        requests[i].result = 0;
    }

    uint32_t agg_count = 0;
    q_start = esp_timer_get_time();
    ret = tsdb_aggregate_multi(query_start_ts, query_end_ts, requests, num_aggs, &agg_count);
    q_elapsed = esp_timer_get_time() - q_start;

    results->multi_agg_records = agg_count;
    results->multi_agg_total_us = (uint32_t)q_elapsed;

    ESP_LOGI(TAG, "Multi-agg (%d aggs): %lu ms over %lu records",
             num_aggs,
             (unsigned long)(q_elapsed / 1000),
             (unsigned long)agg_count);

    // ========================================================================
    // OVERFLOW BENCHMARK (if enough records)
    // ========================================================================
    if (num_records >= 100) {
        ESP_LOGI(TAG, "--- Overflow Write/Query Benchmark ---");

        const char *extra_names[] = {"X0", "X1", "X2"};
        ret = tsdb_add_extra_params(extra_names, 3);

        if (ret == ESP_OK) {
            results->extra_params = 3;
            uint32_t overflow_writes = num_records / 10;  // Write 10% more
            int16_t ext_values[16 + 3];
            uint64_t ovf_write_total = 0;

            for (uint32_t i = 0; i < overflow_writes; i++) {
                for (uint8_t p = 0; p < num_params; p++) {
                    ext_values[p] = (int16_t)((i * 11 + p * 17) % 10000);
                }
                ext_values[num_params] = (int16_t)(i * 3);
                ext_values[num_params + 1] = (int16_t)(i * 5);
                ext_values[num_params + 2] = (int16_t)(i * 7);

                uint64_t start = esp_timer_get_time();
                ret = tsdb_write(query_end_ts + (i * 300), ext_values);
                ovf_write_total += (esp_timer_get_time() - start);

                if (ret != ESP_OK) break;
            }

            results->overflow_writes_count = overflow_writes;
            results->overflow_write_total_us = (uint32_t)ovf_write_total;
            results->overflow_write_avg_us = overflow_writes > 0 ?
                (float)ovf_write_total / overflow_writes : 0;

            ESP_LOGI(TAG, "Overflow writes: %lu records in %lu ms (avg=%.1f us)",
                     (unsigned long)overflow_writes,
                     (unsigned long)(ovf_write_total / 1000),
                     results->overflow_write_avg_us);

            // Query with overflow params
            uint8_t ovf_params[] = {0, (uint8_t)num_params, (uint8_t)(num_params + 1)};
            int16_t ovf_vals[3];
            query_count = 0;

            q_start = esp_timer_get_time();
            ret = tsdb_query_init(&query, query_end_ts, query_end_ts + (overflow_writes * 300),
                                  ovf_params, 3);
            if (ret == ESP_OK) {
                while (tsdb_query_next(&query, &ts, ovf_vals) == ESP_OK) {
                    query_count++;
                }
                tsdb_query_close(&query);
            }
            q_elapsed = esp_timer_get_time() - q_start;

            results->overflow_query_records = query_count;
            results->overflow_query_total_us = (uint32_t)q_elapsed;
            results->overflow_query_per_record_us = query_count > 0 ?
                (float)q_elapsed / query_count : 0;

            ESP_LOGI(TAG, "Overflow query: %lu records in %lu ms (%.1f us/record)",
                     (unsigned long)query_count,
                     (unsigned long)(q_elapsed / 1000),
                     results->overflow_query_per_record_us);
        } else {
            ESP_LOGW(TAG, "Could not add overflow params: %d", ret);
        }
    }

    // Get file size
    struct stat st;
    if (stat(bench_path, &st) == 0) {
        results->file_size_bytes = st.st_size;
    }

    // ========================================================================
    // SUMMARY
    // ========================================================================
    ESP_LOGI(TAG, "=== Benchmark Complete ===");
    ESP_LOGI(TAG, "File size: %lu KB", (unsigned long)(results->file_size_bytes / 1024));
    ESP_LOGI(TAG, "Write:       %.1f us/record (%.1f records/sec)",
             results->write_avg_us,
             results->write_avg_us > 0 ? 1000000.0f / results->write_avg_us : 0);
    ESP_LOGI(TAG, "Full query:  %.1f us/record (%.0f records/sec)",
             results->query_per_record_us,
             results->query_per_record_us > 0 ? 1000000.0f / results->query_per_record_us : 0);
    ESP_LOGI(TAG, "Single col:  %.1f us/record (%.0f records/sec)",
             results->single_param_per_record_us,
             results->single_param_per_record_us > 0 ? 1000000.0f / results->single_param_per_record_us : 0);
    ESP_LOGI(TAG, "Aggregation: %lu ms total",
             (unsigned long)(results->agg_total_us / 1000));
    if (results->overflow_writes_count > 0) {
        ESP_LOGI(TAG, "Overflow write: %.1f us/record", results->overflow_write_avg_us);
        ESP_LOGI(TAG, "Overflow query: %.1f us/record", results->overflow_query_per_record_us);
    }

    // Cleanup — close and delete benchmark DB
    tsdb_close();
    unlink(bench_path);

    ESP_LOGI(TAG, "Benchmark DB cleaned up");

    return ESP_OK;
}
