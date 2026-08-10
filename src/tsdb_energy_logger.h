#ifndef _TSDB_ENERGY_LOGGER_H
#define _TSDB_ENERGY_LOGGER_H
#ifdef ENABLE_TSDB
#include <Arduino.h>
#include <MicroTasks.h>
#include "evse_man.h"

#define TSDB_ENERGY_FILE          "/littlefs/energy.tsdb"
// Sample cadence and on-disk budget. Overridable via build flags so a debug
// build can force a fast ring-wrap (e.g. -DTSDB_ENERGY_SAMPLE_MS=1000
// -DTSDB_ENERGY_BYTES=4096UL) for bench testing the wrapped-ring query path.
#ifndef TSDB_ENERGY_SAMPLE_MS
#define TSDB_ENERGY_SAMPLE_MS     60000                 // 1/min while charging
#endif
// When the vehicle is not charging we only log slow-moving temperature (charge
// current and SoC are meaningless, and reading SoC keeps the vehicle awake), so
// we throttle the write cadence to cut flash wear ~5x during idle periods.
#ifndef TSDB_ENERGY_IDLE_SAMPLE_MS
#define TSDB_ENERGY_IDLE_SAMPLE_MS (5UL * 60000UL)      // 1/5min while idle
#endif
#ifndef TSDB_ENERGY_BYTES
#define TSDB_ENERGY_BYTES         (2500UL * 1024UL)     // ~2.5 MB -> ~100 days
#endif
// Wall-clock must be past this (2023-11-14) before we trust time(NULL) for a
// tsdb timestamp; writing a pre-NTP ~1970 epoch would corrupt the time index.
#define TSDB_TIME_VALID_FLOOR     1700000000UL

// Monthly/annual rollups reuse the legacy on-disk paths/format
// (ENERGY_LOGGER_MONTHLY_DIR / ENERGY_LOGGER_ANNUAL_FILE from energy_logger.h)
// so the /energy/monthly + /energy/annual handlers just stream the files.

class TsdbEnergyLogger : public MicroTasks::Task {
private:
  EvseManager *_evse = nullptr;
  bool         _ready = false;
  int          _init_err = 0;          // esp_err_t from tsdb_init (0 = OK), for /status diag
  double       _last_session_wh = 0;   // for per-sample energy delta

  // Day-rollover tracking: seeded to today at setup() so the first real
  // rollup fires at the next true midnight, not at boot.
  int          _last_rolled_yday = -1; // tm_yday of the last rollup
  int          _last_rolled_year = -1; // tm_year of the last rollup

  bool init_db();
  void rollup_yesterday();
protected:
  void setup();
  unsigned long loop(MicroTasks::WakeReason reason);
public:
  void begin(EvseManager &evse);
  bool isReady() { return _ready; }
  int  initError() { return _init_err; }   // esp_err_t from tsdb_init (0 = OK)
};

extern TsdbEnergyLogger tsdbEnergyLogger;
#endif
#endif
