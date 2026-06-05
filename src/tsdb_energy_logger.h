#ifndef _TSDB_ENERGY_LOGGER_H
#define _TSDB_ENERGY_LOGGER_H
#ifdef ENABLE_TSDB
#include <Arduino.h>
#include <MicroTasks.h>
#include "evse_man.h"

#define TSDB_ENERGY_FILE          "/littlefs/energy.tsdb"
#define TSDB_ENERGY_SAMPLE_MS     60000                 // 1/min, matches EnergyLogger
#define TSDB_ENERGY_BYTES         (2500UL * 1024UL)     // ~2.5 MB -> ~100 days

class TsdbEnergyLogger : public MicroTasks::Task {
private:
  EvseManager *_evse = nullptr;
  bool         _ready = false;
  time_t       _last_sample = 0;
  double       _last_session_wh = 0;   // for per-sample energy delta
  bool init_db();
protected:
  void setup();
  unsigned long loop(MicroTasks::WakeReason reason);
public:
  void begin(EvseManager &evse);
  bool isReady() { return _ready; }
};

extern TsdbEnergyLogger tsdbEnergyLogger;
#endif
#endif
