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
  if (e != ESP_OK) { DBUGF("tsdb_init failed: %d", e); return false; }
  return true;
}

void TsdbEnergyLogger::begin(EvseManager &evse) { _evse = &evse; MicroTask.startTask(this); }

void TsdbEnergyLogger::setup() {
  _ready = init_db();
  _last_sample = time(NULL);
  _last_session_wh = _evse ? _evse->getSessionEnergy() : 0;
}

unsigned long TsdbEnergyLogger::loop(MicroTasks::WakeReason) {
  if (_ready && _evse) {
    EnergySample s;
    s.amps    = _evse->getAmps();
    s.volts   = _evse->getVoltage();
    s.power_w = s.amps * s.volts;
    double cur_wh = _evse->getSessionEnergy();
    s.energy_wh_delta = (cur_wh >= _last_session_wh) ? (cur_wh - _last_session_wh) : cur_wh;
    _last_session_wh  = cur_wh;
    s.temp_c  = _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)
                  ? _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) : 0;
    s.soc     = _evse->isVehicleStateOfChargeValid() ? _evse->getVehicleStateOfCharge() : -1;
    // getChargeCurrent() with no args returns _monitor.getPilot() (the actual
    // pilot current in amps); getPilot() is not forwarded directly on EvseManager.
    s.pilot_a = _evse->getChargeCurrent();

    int16_t row[TSDB_NUM_COLS];
    tsdb_scale_sample(s, row);
    esp_err_t e = tsdb_write((uint32_t)time(NULL), row);
    if (e != ESP_OK) DBUGF("tsdb_write failed: %d", e);
  }
  return TSDB_ENERGY_SAMPLE_MS;
}
#endif
