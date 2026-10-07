#ifndef _TSDB_ENERGY_LOGGER_H
#define _TSDB_ENERGY_LOGGER_H
#ifdef ENABLE_TSDB
#include <Arduino.h>
#include <MicroTasks.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_tsdb.h>          // tsdb_agg_request_t, used by the dispatchers below
#include "evse_man.h"
#include "tsdb_sample.h"

#define TSDB_ENERGY_FILE          "/littlefs/energy.tsdb"
// Sample cadence and on-disk budget. Overridable via build flags so a debug
// build can force a fast ring-wrap (e.g. -DTSDB_ENERGY_SAMPLE_MS=1000
// -DTSDB_ENERGY_BYTES=4096UL) for bench testing the wrapped-ring query path.
// The TSDB_ENERGY_*_SAMPLE_MS cadences live in tsdb_sample.h next to
// tsdb_sample_interval_ms(), which picks between them.
#ifndef TSDB_ENERGY_BYTES
#define TSDB_ENERGY_BYTES         (2500UL * 1024UL)     // ~2.5 MB -> ~100 days
#endif
// Wall-clock must be past this (2023-11-14) before we trust time(NULL) for a
// tsdb timestamp; writing a pre-NTP ~1970 epoch would corrupt the time index.
#define TSDB_TIME_VALID_FLOOR     1700000000UL

// Monthly/annual rollups reuse the legacy on-disk paths/format
// (ENERGY_LOGGER_MONTHLY_DIR / ENERGY_LOGGER_ANNUAL_FILE from energy_logger.h)
// so the /energy/monthly + /energy/annual handlers just stream the files.

// A sample is taken on loopTask but written from its own task: tsdb_write
// fsyncs the database and its header sidecar, and either fsync can land on a
// LittleFS metadata compaction that runs for seconds (O(tags^2) over the
// directory, one cache-disabled flash read per tag). On loopTask that is a task
// watchdog panic; on a worker it is just a slow write.
struct TsdbWriteJob {
  uint32_t ts;
  int16_t  row[TSDB_NUM_COLS];
  bool     rollup;      // roll up yesterday before writing this sample
};

class TsdbEnergyLogger : public MicroTasks::Task {
private:
  EvseManager *_evse = nullptr;
  bool         _ready = false;
  int          _init_err = 0;          // esp_err_t from tsdb_init (0 = OK), for /status diag
  double       _last_session_wh = 0;   // for per-sample energy delta
  QueueHandle_t _jobs = nullptr;
  uint32_t     _dropped = 0;           // samples lost to a full queue (writer stalled)
  bool         _rollup_pending = false; // day changed, rollup job not yet accepted by the queue

  // Day-rollover tracking: seeded to today at setup() so the first real
  // rollup fires at the next true midnight, not at boot.
  int          _last_rolled_yday = -1; // tm_yday of the last rollup
  int          _last_rolled_year = -1; // tm_year of the last rollup

  bool init_db();
  void rollup_yesterday();
  bool start_writer();
  static void writer_task(void *arg);
protected:
  void setup();
  unsigned long loop(MicroTasks::WakeReason reason);
public:
  void begin(EvseManager &evse);
  bool isReady() { return _ready; }
  int  initError() { return _init_err; }   // esp_err_t from tsdb_init (0 = OK)
  uint32_t droppedSamples() { return _dropped; }
};

// Count records in a range on whichever store currently holds the samples.
// Same reasoning as energy_aggregate_multi() below: a count against the wrong
// store reads zero and the caller skips a range that does have data.
bool energy_query_count_any(uint32_t start_ts, uint32_t end_ts, uint32_t &count);

// Aggregate over whichever store currently holds the samples.
//
// The write path sends each sample to exactly one store -- the card when a
// card is fitted and healthy, internal flash otherwise -- so anything that
// aggregates has to make the same choice or it reads an empty database. Takes
// esp_tsdb's request type because both call sites already speak it; on the
// card it is translated to the equivalent sdlog request.
//
// Returns false if the aggregation could not run at all. `scanned` is the
// number of records seen, so callers can still distinguish "no data in this
// range" from "data that sums to zero".
bool energy_aggregate_multi(uint32_t start_ts, uint32_t end_ts,
                            tsdb_agg_request_t *reqs, uint8_t num_reqs,
                            uint32_t &scanned);

extern TsdbEnergyLogger tsdbEnergyLogger;
#endif
#endif
