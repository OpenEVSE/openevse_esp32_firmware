#ifdef ENABLE_TSDB
#include "tsdb_energy_logger.h"
#include "tsdb_sample.h"
#include "esp_tsdb.h"
#include "debug.h"

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
#else                                          // 16 MB WROOM-32E: no PSRAM
  cfg.alloc_strategy      = TSDB_ALLOC_INTERNAL_RAM;
  cfg.buffer_pool_size    = 10 * 1024;
  cfg.use_paged_allocation= true;
  cfg.page_size           = 2048;
#endif
  esp_err_t e = tsdb_init(&cfg);
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
