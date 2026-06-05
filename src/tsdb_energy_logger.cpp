#ifdef ENABLE_TSDB
#include "tsdb_energy_logger.h"
#include "tsdb_sample.h"
#include "esp_tsdb.h"
#include "energy_logger.h"   // MonthlyMetrics, AnnualMetrics, serialize/deserialize
#include "debug.h"
#include <LittleFS.h>
#include <time.h>

TsdbEnergyLogger tsdbEnergyLogger;

bool TsdbEnergyLogger::init_db() {
  tsdb_config_t cfg = {};
  cfg.filepath     = TSDB_ENERGY_FILE;
  cfg.num_params   = TSDB_NUM_COLS;
  cfg.param_names  = TSDB_PARAM_NAMES;
  cfg.max_records  = TSDB_CALC_MAX_RECORDS(TSDB_ENERGY_BYTES, TSDB_NUM_COLS);
  cfg.index_stride = 380;
#if defined(CONFIG_IDF_TARGET_ESP32P4)        // P4 has PSRAM
  cfg.alloc_strategy      = TSDB_ALLOC_PSRAM;
  cfg.buffer_pool_size    = 16 * 1024;
  cfg.use_paged_allocation= true;
  cfg.page_size           = 4096;
#else                                          // 16 MB WROOM-32E (xtensa): no PSRAM
  // Internal-RAM path, HW-validated after fixing an esp_tsdb bug: its
  // TSDB_ALLOC_INTERNAL_RAM strategy allocated block buffers with
  // MALLOC_CAP_INTERNAL only, which can land in IRAM (word-access only) and
  // faulted on the int16 block stores (LoadStoreError). Fixed to
  // MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT in components/esp_tsdb/src/tsdb_buffer.c.
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

void TsdbEnergyLogger::begin(EvseManager &evse) { _evse = &evse; MicroTask.startTask(this); }

void TsdbEnergyLogger::setup() {
  _ready = init_db();
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
  if (tsdb_query_count(d0u, d1u, &cnt) != ESP_OK || cnt == 0) {
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
  esp_err_t err = tsdb_aggregate_multi(d0u, d1u, reqs, 3, &nscanned);
  if (err != ESP_OK || nscanned == 0) {
    DBUGF("[tsdb rollup] aggregate failed for yesterday, err=%d", (int)err);
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
  if (_ready && _evse) {
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
        rollup_yesterday();
        _last_rolled_yday = now_tm.tm_yday;
        _last_rolled_year = now_tm.tm_year;
      }

      // -- Write tsdb sample --
      EnergySample s;
      s.amps    = _evse->getAmps();
      s.volts   = _evse->getVoltage();
      s.power_w = s.amps * s.volts;
      s.energy_wh_delta = delta;
      s.temp_c  = _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)
                    ? _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) : 0;
      s.soc     = _evse->isVehicleStateOfChargeValid() ? _evse->getVehicleStateOfCharge() : -1;
      // getChargeCurrent() with no args returns _monitor.getPilot() (the actual
      // pilot current in amps); getPilot() is not forwarded directly on EvseManager.
      s.pilot_a = _evse->getChargeCurrent();

      int16_t row[TSDB_NUM_COLS];
      tsdb_scale_sample(s, row);
      esp_err_t e = tsdb_write((uint32_t)now, row);
      if (e != ESP_OK) DBUGF("tsdb_write failed: %d", e);
    }
  }
  return TSDB_ENERGY_SAMPLE_MS;
}
#endif
