#ifdef ENABLE_TSDB
#include "tsdb_energy_logger.h"
#include "tsdb_sample.h"
#include "esp_tsdb.h"
#include "energy_logger.h"   // MonthlyMetrics, AnnualMetrics, serialize/deserialize
#include "debug.h"
#include "fs_usage.h"
#include "psram.h"
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
  esp_err_t e = ESP_FAIL;
  if(psram_available()) {
    // P4, and xtensa/S3 modules with PSRAM fitted. Decided at runtime: some
    // board definitions set BOARD_HAS_PSRAM with none fitted (see psram.h),
    // and asking for TSDB_ALLOC_PSRAM there fails the whole store.
    tsdb_config_t pcfg = cfg;
    pcfg.alloc_strategy      = TSDB_ALLOC_PSRAM;
    pcfg.buffer_pool_size    = 16 * 1024;
    pcfg.use_paged_allocation= true;
    pcfg.page_size           = 4096;
    e = tsdb_init(&pcfg);
    if(e != ESP_OK) {
      // PSRAM is fitted but the pages would not allocate; tsdb_init leaves no
      // handle behind on failure, so the internal-RAM config can still run.
      DEBUG_PORT.printf("[tsdb] PSRAM init failed (%d), retrying in internal RAM\n", (int)e);
    }
  }
  if(e != ESP_OK) {
    e = tsdb_init(&cfg);
  }
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
#define TSDB_USAGE_REFRESH_MS 30000   // same age limit as LFS_USAGE_MAX_AGE_MS

bool TsdbEnergyLogger::start_writer() {
  _jobs = xQueueCreate(TSDB_WRITER_QUEUE, sizeof(TsdbWriteJob));
  if (_jobs == nullptr) return false;
  TaskHandle_t h = nullptr;
  // First sample here, before the writer exists to hold the lock, so a request
  // never finds the cache empty and reads LittleFS from loopTask itself.
  littlefs_usage.refresh(millis());
  if (xTaskCreatePinnedToCore(writer_task, "tsdb_writer", TSDB_WRITER_STACK, this,
                              1, &h, APP_CPU_NUM) != pdPASS) {
    vQueueDelete(_jobs);
    _jobs = nullptr;
    return false;
  }
  // This task holds the FS lock for the length of each write, so it keeps the
  // /status and /config usage numbers fresh and loopTask never waits on it.
  littlefs_usage.setBackgroundSampler();
  return true;
}

#ifdef ENABLE_SD_CARD
static bool card_aggregate_multi(uint32_t start_ts, uint32_t end_ts,
                                 tsdb_agg_request_t *reqs, uint8_t num_reqs,
                                 uint32_t &scanned)
{
  {
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
}
#endif

static bool flash_aggregate_multi(uint32_t start_ts, uint32_t end_ts,
                                  tsdb_agg_request_t *reqs, uint8_t num_reqs,
                                  uint32_t &scanned)
{
  // Guard on isReady(): with a failed tsdb_init the global handle is invalid.
  if (!tsdbEnergyLogger.isReady()) {
    return false;
  }
  return tsdb_aggregate_multi(start_ts, end_ts, reqs, num_reqs, &scanned) == ESP_OK;
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
    return card_aggregate_multi(start_ts, end_ts, reqs, num_reqs, scanned);
  }
#endif
  return flash_aggregate_multi(start_ts, end_ts, reqs, num_reqs, scanned);
}

// One store's share of a day for the rollup. A store with nothing in the
// window (or whose count query fails, as the single-store rollup treated it)
// is an empty share; false means it has samples but would not aggregate them.
static bool day_agg_from(bool card, uint32_t d0, uint32_t d1, EnergyDayAgg &out)
{
  out = EnergyDayAgg();
  uint32_t cnt = 0;
  bool counted;
#ifdef ENABLE_SD_CARD
  if (card) {
    counted = sdlog_query_count(d0, d1, cnt);
  } else
#endif
  {
    counted = tsdb_query_count(d0, d1, &cnt) == ESP_OK;
  }
  if (!counted || cnt == 0) {
    return true;
  }

  tsdb_agg_request_t reqs[3] = {
    { TSDB_COL_ENERGY, TSDB_AGG_SUM, 0 },
    { TSDB_COL_TEMP,   TSDB_AGG_MAX, 0 },
    { TSDB_COL_TEMP,   TSDB_AGG_MIN, 0 },
  };
  uint32_t scanned = 0;
  bool ok;
#ifdef ENABLE_SD_CARD
  if (card) {
    ok = card_aggregate_multi(d0, d1, reqs, 3, scanned);
  } else
#endif
  {
    (void)card;
    ok = flash_aggregate_multi(d0, d1, reqs, 3, scanned);
  }
  if (!ok || scanned == 0) {
    return false;
  }
  out.count     = cnt;
  out.energy_wh = reqs[0].result;
  out.temp_max  = reqs[1].result;
  out.temp_min  = reqs[2].result;
  return true;
}

void TsdbEnergyLogger::writer_task(void *arg) {
  TsdbEnergyLogger *self = static_cast<TsdbEnergyLogger *>(arg);
  TsdbWriteJob job;
  for (;;) {
    // Wake at least every TSDB_USAGE_REFRESH_MS even with no jobs: until the
    // clock is set there are none, and other subsystems write LittleFS too.
    if (xQueueReceive(self->_jobs, &job, pdMS_TO_TICKS(TSDB_USAGE_REFRESH_MS)) != pdTRUE) {
      littlefs_usage.refresh(millis());
      continue;
    }
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
    // Only into an open flash store: with tsdb_init failed the handle is invalid.
    if (!logged && self->_ready) {
      esp_err_t e = tsdb_write(job.ts, job.row);
      if (e != ESP_OK) DBUGF("tsdb_write failed: %d", e);
    }
    littlefs_usage.refresh(millis());
  }
}

void TsdbEnergyLogger::begin(EvseManager &evse) { _evse = &evse; MicroTask.startTask(this); }

void TsdbEnergyLogger::setup() {
  _ready = init_db();
  // The writer also feeds the card, so on a card-capable board it runs even
  // when the flash store would not open: a healthy card still takes history.
  // Elsewhere a failed flash store leaves nothing to write to, as before.
#ifdef ENABLE_SD_CARD
  const bool want_writer = true;
#else
  const bool want_writer = _ready;
#endif
  _writing = want_writer && start_writer();
  if (want_writer && !_writing) {
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

  // A day can straddle a store change: samples go to flash until the card
  // comes up (or after it fails) and to the card otherwise, each to exactly one.
  // Live reads use the active store only, but the persisted daily total has to
  // cover both, so ask each store that is open and merge.
  EnergyDayAgg day;
  bool any_ok = false;
  bool any_failed = false;
#ifdef ENABLE_SD_CARD
  if (sdlog_store_ready()) {
    EnergyDayAgg part;
    if (day_agg_from(true, d0u, d1u, part)) {
      day = energy_day_merge(day, part);
      any_ok = true;
    } else {
      any_failed = true;
    }
  }
#endif
  if (isReady()) {
    EnergyDayAgg part;
    if (day_agg_from(false, d0u, d1u, part)) {
      day = energy_day_merge(day, part);
      any_ok = true;
    } else {
      any_failed = true;
    }
  }
  if (any_failed) {
    // A partial total persisted now would never be corrected; skip the day.
    DBUGLN("[tsdb rollup] aggregate failed for yesterday");
    return;
  }
  if (!any_ok || day.count == 0) {
    DBUGF("[tsdb rollup] no data for yesterday %04d-%02d-%02d, skipping",
          yday_year, yday_month, yday_tm.tm_mday);
    return;
  }

  double energy_wh = (double)day.energy_wh;           // Wh (sum of int16 deltas)
  double peak_c    = (double)day.temp_max / 10.0;     // deci-degC → degC
  double min_c     = (double)day.temp_min / 10.0;
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
    JsonDocument mdoc;
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
      found = marr.add<JsonObject>();
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
    JsonDocument adoc;
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
      found = aarr.add<JsonObject>();
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
  if (_writing && _evse) {
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
