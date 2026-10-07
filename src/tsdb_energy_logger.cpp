#ifdef ENABLE_TSDB
#include "tsdb_energy_logger.h"
#include "tsdb_sample.h"
#include "esp_tsdb.h"
#include "energy_logger.h"   // MonthlyMetrics, AnnualMetrics, serialize/deserialize
#include "debug.h"
#include <LittleFS.h>
#include <time.h>
#include "sd_card.h"
#include "sdlog_store.h"


TsdbEnergyLogger tsdbEnergyLogger;

bool TsdbEnergyLogger::init_db() {
  tsdb_config_t cfg = {};
  cfg.filepath     = TSDB_ENERGY_FILE;
  cfg.num_params   = TSDB_NUM_COLS;
  cfg.param_names  = TSDB_PARAM_NAMES;
  // Clamp the on-disk ring to the actual LittleFS partition, reserving room for
  // the other users (config, certs, schedule, emeter, JSON energy logs). On the
  // 16 MB build this leaves the full TSDB_ENERGY_BYTES; on a small partition
  // (e.g. the 4 MB build's 128 KB) it shrinks the ring so it can't fill the FS.
  {
    const size_t reserve   = 384u * 1024u;  // headroom for everything else
    size_t       fs_total  = LittleFS.totalBytes();
    size_t       fs_budget = fs_total > reserve ? fs_total - reserve : fs_total / 4;
    size_t       budget    = TSDB_ENERGY_BYTES;
    if(budget > fs_budget) {
      budget = fs_budget;
      DEBUG_PORT.printf("[tsdb] ring clamped to %u bytes for %u byte FS\n",
                        (unsigned)budget, (unsigned)fs_total);
    }
    cfg.max_records = TSDB_CALC_MAX_RECORDS(budget, TSDB_NUM_COLS);
  }
  cfg.index_stride = 380;
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(BOARD_HAS_PSRAM)   // P4, S3 LCD board
  cfg.alloc_strategy      = TSDB_ALLOC_PSRAM;
  cfg.buffer_pool_size    = 16 * 1024;
  cfg.use_paged_allocation= true;
  cfg.page_size           = 4096;
#else                                          // 16 MB WROOM-32E (xtensa): no PSRAM
  // Internal-RAM path, HW-validated after fixing an esp_tsdb bug: its
  // TSDB_ALLOC_INTERNAL_RAM strategy allocated block buffers with
  // MALLOC_CAP_INTERNAL only, which can land in IRAM (word-access only) and
  // faulted on the int16 block stores (LoadStoreError). Fixed to
  // MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT upstream (zakery292/esp_tsdb#3), in
  // src/tsdb_buffer.c.
  cfg.alloc_strategy      = TSDB_ALLOC_INTERNAL_RAM;
  cfg.buffer_pool_size    = 12 * 1024;
  cfg.use_paged_allocation= false;
  cfg.page_size           = 0;
#endif
  esp_err_t e = tsdb_init(&cfg);
  _init_err = (int)e;   // surfaced via /status tsdb_err for diagnosis
  if (e != ESP_OK) {
    // Unconditional (not DBUGF): init failure silently disables all history, so
    // make it diagnosable without a debug build.
    DEBUG_PORT.printf("[tsdb] init failed (%d): energy history disabled\n", (int)e);
    return false;
  }
  return true;
}

// The writer shares core 1 with loopTask at the same priority, so a long
// compaction time-slices against the main loop instead of stalling it. It must
// not sit on core 0: the task watchdog also watches idle0, and a multi-second
// write there would trip it just the same.
#define TSDB_WRITER_STACK   6144
#define TSDB_WRITER_QUEUE   4

bool TsdbEnergyLogger::start_writer() {
  _jobs = xQueueCreate(TSDB_WRITER_QUEUE, sizeof(TsdbWriteJob));
  if (_jobs == nullptr) return false;
  TaskHandle_t h = nullptr;
  if (xTaskCreatePinnedToCore(writer_task, "tsdb_writer", TSDB_WRITER_STACK, this,
                              1, &h, APP_CPU_NUM) != pdPASS) {
    vQueueDelete(_jobs);
    _jobs = nullptr;
    return false;
  }
  return true;
}

bool energy_query_count_any(uint32_t start_ts, uint32_t end_ts, uint32_t &count)
{
  count = 0;
#ifdef ENABLE_SD_CARD
  if (sdlog_store_ready()) {
    return sdlog_query_count(start_ts, end_ts, count);
  }
#endif
  if (!tsdbEnergyLogger.isReady()) {
    return false;
  }
  return tsdb_query_count(start_ts, end_ts, &count) == ESP_OK;
}

bool energy_aggregate_multi(uint32_t start_ts, uint32_t end_ts,
                            tsdb_agg_request_t *reqs, uint8_t num_reqs,
                            uint32_t &scanned)
{
  scanned = 0;
  if (reqs == nullptr || num_reqs == 0) {
    return false;
  }

#ifdef ENABLE_SD_CARD
  if (sdlog_store_ready()) {
    SdlogAggRequest sreqs[8];
    if (num_reqs > 8) {
      num_reqs = 8;
    }
    for (uint8_t r = 0; r < num_reqs; r++) {
      sreqs[r].col    = reqs[r].param_index;
      sreqs[r].result = 0;
      switch (reqs[r].agg_type) {
        case TSDB_AGG_SUM:   sreqs[r].agg = SDLOG_AGG_SUM;   break;
        case TSDB_AGG_AVG:   sreqs[r].agg = SDLOG_AGG_AVG;   break;
        case TSDB_AGG_MIN:   sreqs[r].agg = SDLOG_AGG_MIN;   break;
        case TSDB_AGG_MAX:   sreqs[r].agg = SDLOG_AGG_MAX;   break;
        case TSDB_AGG_COUNT: sreqs[r].agg = SDLOG_AGG_COUNT; break;
        case TSDB_AGG_FIRST: sreqs[r].agg = SDLOG_AGG_FIRST; break;
        case TSDB_AGG_LAST:  sreqs[r].agg = SDLOG_AGG_LAST;  break;
        default:             return false;
      }
    }
    if (!sdlog_aggregate_multi(start_ts, end_ts, sreqs, num_reqs, scanned)) {
      return false;
    }
    for (uint8_t r = 0; r < num_reqs; r++) {
      reqs[r].result = sreqs[r].result;
    }
    return true;
  }
#endif

  // Guard on isReady(): with a failed tsdb_init the global handle is invalid.
  if (!tsdbEnergyLogger.isReady()) {
    return false;
  }
  return tsdb_aggregate_multi(start_ts, end_ts, reqs, num_reqs, &scanned) == ESP_OK;
}

void TsdbEnergyLogger::writer_task(void *arg) {
  TsdbEnergyLogger *self = static_cast<TsdbEnergyLogger *>(arg);
  TsdbWriteJob job;
  for (;;) {
    if (xQueueReceive(self->_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
    if (job.rollup) self->rollup_yesterday();

    // Prefer the card when one is fitted and healthy; fall back to internal
    // flash otherwise. Not both -- writing each sample twice would double the
    // wear for no benefit, since only one store answers queries at a time.
    //
    // This runs on the writer task rather than loopTask, so a slow card costs
    // history rather than a watchdog reset, exactly as the flash path does.
    // The card branch compiles away entirely without ENABLE_SD_CARD, leaving
    // the original tsdb_write() call and identical behaviour on the shipped
    // boards.
    bool logged = false;
#ifdef ENABLE_SD_CARD
    // sd_card_loop() owns the card and the ring, opening it once a card is
    // mounted and closing it before an unmount -- both on loopTask, which is
    // why sdlog_store takes its own lock rather than trusting "ready" to still
    // be true by the time the append lands.
    if (sdlog_store_ready()) {
      logged = sdlog_store_append(job.ts, job.row);
      if (!logged) {
        // append() has already marked itself not-ready, so this falls through
        // to flash now and stays there rather than retrying a broken card.
        DBUGLN("card append failed, using internal flash for this sample");
      }
    }
#endif
    if (!logged) {
      esp_err_t e = tsdb_write(job.ts, job.row);
      if (e != ESP_OK) DBUGF("tsdb_write failed: %d", e);
    }
  }
}

void TsdbEnergyLogger::begin(EvseManager &evse) { _evse = &evse; MicroTask.startTask(this); }

void TsdbEnergyLogger::setup() {
  _ready = init_db() && start_writer();
  if (!_ready && _init_err == 0) {
    DEBUG_PORT.println("[tsdb] writer task failed: energy history disabled");
  }
  _last_session_wh = _evse ? _evse->getSessionEnergy() : 0;

  // Seed rollover tracker to TODAY so the first real rollup fires at the next
  // midnight crossing, not immediately at boot (which would be a partial day).
  time_t now = time(NULL);
  if ((unsigned long)now >= TSDB_TIME_VALID_FLOOR) {
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    _last_rolled_yday = tm_now.tm_yday;
    _last_rolled_year = tm_now.tm_year;
  }
}

// ---------------------------------------------------------------------------
// rollup_yesterday() – aggregate the previous calendar day from tsdb and
// append/update the monthly + annual JSON rollup files.
//
// Called only when _last_rolled_{yday,year} ≠ today AND the wall clock is
// valid (≥ TSDB_TIME_VALID_FLOOR).  At that point `now` is already past
// midnight, so we compute yesterday's window as:
//   d0 = midnight (00:00:00) of yesterday
//   d1 = d0 + 86400 - 1  (inclusive, avoids boundary double-counting)
// ---------------------------------------------------------------------------

// JSON capacities (≤12 months/yr, small number of years)
#define TSDB_MONTHLY_JSON_CAP \
  (JSON_ARRAY_SIZE(13) + 13 * JSON_OBJECT_SIZE(4) + 13 * 12)
#define TSDB_ANNUAL_JSON_CAP \
  (JSON_ARRAY_SIZE(50) + 50 * JSON_OBJECT_SIZE(4))

void TsdbEnergyLogger::rollup_yesterday() {
  time_t now = time(NULL);

  // Compute yesterday's start-of-day (local time)
  time_t yesterday_approx = now - 86400;
  struct tm yday_tm;
  localtime_r(&yesterday_approx, &yday_tm);
  yday_tm.tm_hour = 0;
  yday_tm.tm_min  = 0;
  yday_tm.tm_sec  = 0;
  time_t d0 = mktime(&yday_tm);
  uint32_t d0u = (uint32_t)d0;
  uint32_t d1u = d0u + 86400 - 1;  // inclusive end

  int yday_year  = yday_tm.tm_year + 1900;
  int yday_month = yday_tm.tm_mon  + 1;

  // Check record count cheaply; skip if no data
  uint32_t cnt = 0;
  if (!energy_query_count_any(d0u, d1u, cnt) || cnt == 0) {
    DBUGF("[tsdb rollup] no data for yesterday %04d-%02d-%02d, skipping",
          yday_year, yday_month, yday_tm.tm_mday);
    return;
  }

  // Aggregate: energy SUM (Wh), temp MAX and MIN (deci-degC)
  tsdb_agg_request_t reqs[3] = {
    { TSDB_COL_ENERGY, TSDB_AGG_SUM, 0 },
    { TSDB_COL_TEMP,   TSDB_AGG_MAX, 0 },
    { TSDB_COL_TEMP,   TSDB_AGG_MIN, 0 },
  };
  uint32_t nscanned = 0;
  if (!energy_aggregate_multi(d0u, d1u, reqs, 3, nscanned) || nscanned == 0) {
    DBUGLN("[tsdb rollup] aggregate failed for yesterday");
    return;
  }

  double energy_wh = (double)reqs[0].result;          // Wh (sum of int16 deltas)
  double peak_c    = (double)reqs[1].result / 10.0;   // deci-degC → degC
  double min_c     = (double)reqs[2].result / 10.0;
  double energy_kwh = energy_wh / 1000.0;

  DBUGF("[tsdb rollup] yesterday %04d-%02d-%02d: %.1f Wh, pk=%.1f, mn=%.1f",
        yday_year, yday_month, yday_tm.tm_mday, energy_wh, peak_c, min_c);

  // ---- Update monthly rollup file (/logs/monthly/YYYY.json) ----
  // Ensure directory exists
  if (!LittleFS.exists(ENERGY_LOGGER_MONTHLY_DIR)) {
    LittleFS.mkdir(ENERGY_LOGGER_DIR);
    LittleFS.mkdir(ENERGY_LOGGER_MONTHLY_DIR);
  } else if (!LittleFS.exists(ENERGY_LOGGER_DIR)) {
    LittleFS.mkdir(ENERGY_LOGGER_DIR);
  }

  char monthly_path[64];
  snprintf(monthly_path, sizeof(monthly_path), "%s/%04d.json",
           ENERGY_LOGGER_MONTHLY_DIR, yday_year);

  char month_key[8];
  snprintf(month_key, sizeof(month_key), "%04d-%02d", yday_year, yday_month);

  {
    DynamicJsonDocument mdoc(TSDB_MONTHLY_JSON_CAP);
    JsonArray marr = mdoc.to<JsonArray>();

    File mf = LittleFS.open(monthly_path, "r");
    if (mf) {
      deserializeJson(mdoc, mf);
      mf.close();
      if (!mdoc.is<JsonArray>()) marr = mdoc.to<JsonArray>();
    }

    // Find existing entry for this month or create a new one
    JsonObject found;
    for (JsonObject item : marr) {
      const char *mo = item["mo"] | "";
      if (strcmp(mo, month_key) == 0) { found = item; break; }
    }
    if (found.isNull()) {
      found = marr.createNestedObject();
      found["mo"] = (char *)month_key;
      found["pk"] = peak_c;
      found["mn"] = min_c;
      found["en"] = energy_kwh;
    } else {
      // Accumulate into existing month entry
      double cur_en  = found["en"] | 0.0;
      double cur_pk  = found["pk"] | peak_c;
      double cur_mn  = found["mn"] | min_c;
      found["en"] = cur_en + energy_kwh;
      if (peak_c > cur_pk) found["pk"] = peak_c;
      if (min_c  < cur_mn) found["mn"] = min_c;
    }

    File wf = LittleFS.open(monthly_path, "w");
    if (wf) {
      if (serializeJson(mdoc, wf) == 0) {
        DBUG("[tsdb rollup] monthly write failed");
        wf.close();
        LittleFS.remove(monthly_path);
      } else {
        wf.close();
        DBUGF("[tsdb rollup] monthly updated: %s", monthly_path);
      }
    } else {
      DBUGF("[tsdb rollup] monthly open failed: %s", monthly_path);
    }
  }

  // ---- Update annual rollup file (/logs/annual.json) ----
  {
    DynamicJsonDocument adoc(TSDB_ANNUAL_JSON_CAP);
    JsonArray aarr = adoc.to<JsonArray>();

    File af = LittleFS.open(ENERGY_LOGGER_ANNUAL_FILE, "r");
    if (af) {
      deserializeJson(adoc, af);
      af.close();
      if (!adoc.is<JsonArray>()) aarr = adoc.to<JsonArray>();
    }

    // Find existing entry for this year or create a new one
    JsonObject found;
    for (JsonObject item : aarr) {
      if ((int)(item["yr"] | 0) == yday_year) { found = item; break; }
    }
    if (found.isNull()) {
      found = aarr.createNestedObject();
      found["yr"] = yday_year;
      found["pk"] = peak_c;
      found["mn"] = min_c;
      found["en"] = energy_kwh;
    } else {
      double cur_en = found["en"] | 0.0;
      double cur_pk = found["pk"] | peak_c;
      double cur_mn = found["mn"] | min_c;
      found["en"] = cur_en + energy_kwh;
      if (peak_c > cur_pk) found["pk"] = peak_c;
      if (min_c  < cur_mn) found["mn"] = min_c;
    }

    File wf = LittleFS.open(ENERGY_LOGGER_ANNUAL_FILE, "w");
    if (wf) {
      if (serializeJson(adoc, wf) == 0) {
        DBUG("[tsdb rollup] annual write failed");
        wf.close();
        LittleFS.remove(ENERGY_LOGGER_ANNUAL_FILE);
      } else {
        wf.close();
        DBUGF("[tsdb rollup] annual updated: %s", ENERGY_LOGGER_ANNUAL_FILE);
      }
    } else {
      DBUGF("[tsdb rollup] annual open failed: %s", ENERGY_LOGGER_ANNUAL_FILE);
    }
  }
}

unsigned long TsdbEnergyLogger::loop(MicroTasks::WakeReason) {
  // Cadence depends on what we are writing to (card vs flash) and whether a
  // session is running; see tsdb_sample_interval_ms().
  unsigned long next_ms = tsdb_sample_interval_ms(true, false);
  if (_ready && _evse) {
    bool charging = _evse->isCharging();
    next_ms = tsdb_sample_interval_ms(charging, sdlog_store_ready());

    // Advance the energy baseline every wake (even when we skip the write below),
    // so deltas stay honest once logging resumes. A session reset to 0 on vehicle
    // unplug yields a small positive delta (cur_wh), not a negative one. (Lossy
    // only across an unplug+replug within the same 60s window — negligible.)
    double cur_wh = _evse->getSessionEnergy();
    double delta  = (cur_wh >= _last_session_wh) ? (cur_wh - _last_session_wh) : cur_wh;
    _last_session_wh = cur_wh;

    // Skip the write until the wall clock is real; a pre-NTP ~1970 timestamp would
    // create a non-monotonic gap that corrupts the tsdb time index on disk.
    time_t now = time(NULL);
    if ((unsigned long)now >= TSDB_TIME_VALID_FLOOR) {
      struct tm now_tm;
      localtime_r(&now, &now_tm);

      // -- Day-rollover: when local date has advanced, roll up yesterday --
      // _last_rolled_{yday,year} == -1 means setup() ran before NTP was valid;
      // seed it now and skip the rollup (partial day since boot).
      if (_last_rolled_yday == -1) {
        _last_rolled_yday = now_tm.tm_yday;
        _last_rolled_year = now_tm.tm_year;
      } else if (now_tm.tm_yday  != _last_rolled_yday ||
                 now_tm.tm_year  != _last_rolled_year) {
        _rollup_pending = true;   // done by the writer, ahead of the next sample it accepts
        _last_rolled_yday = now_tm.tm_yday;
        _last_rolled_year = now_tm.tm_year;
      }

      // -- Write tsdb sample --
      // Temperature is logged in every sample (slow-moving, useful idle or not).
      // Charge current, power, pilot and SoC are only meaningful while charging:
      // when idle they stay at their idle sentinels (0 / SoC -1) so the history
      // does not show post-charge current or a SoC that drifts as the car drives.
      EnergySample s;
      s.energy_wh_delta = delta;   // ~0 while idle; keeps cumulative totals exact
      s.temp_c  = _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)
                    ? _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) : 0;
      if (charging) {
        s.amps    = _evse->getAmps();
        s.volts   = _evse->getVoltage();
        s.power_w = s.amps * s.volts;
        s.soc     = _evse->isVehicleStateOfChargeValid() ? _evse->getVehicleStateOfCharge() : -1;
        // getChargeCurrent() with no args returns _monitor.getPilot() (the actual
        // pilot current in amps); getPilot() is not forwarded directly on EvseManager.
        s.pilot_a = _evse->getChargeCurrent();
      }

      TsdbWriteJob job;
      job.ts = (uint32_t)now;
      tsdb_scale_sample(s, job.row);
      job.rollup = _rollup_pending;
      // Never block here: if the writer is still inside a slow flash operation
      // the sample is dropped, which costs one point of history, not a reboot.
      // A pending rollup stays pending until a job carrying it is accepted.
      //
      // The card-or-flash choice is the writer's, not ours -- see writer_task().
      // Queueing both the same way keeps the rollup bookkeeping in one place and
      // gets card I/O off loopTask along with the flash writes.
      if (xQueueSend(_jobs, &job, 0) == pdTRUE) {
        _rollup_pending = false;
      } else {
        _dropped++;
        DBUGF("tsdb sample dropped, writer busy (%lu total)", (unsigned long)_dropped);
      }
    }
  }
  return next_ms;
}
#endif
