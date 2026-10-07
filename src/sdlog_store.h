// The on-card energy log: a fixed-size circular file of SDLOG_RECORD_BYTES slots.
//
// Preferred over the internal tsdb when a card is mounted; the tsdb path is used
// unchanged when it is not. The two are not written together -- one store owns
// the samples at any moment -- so history written to a card stays on that card,
// and history written to flash stays in flash.
//
// The file is created at full size up front rather than grown. Preallocating
// keeps every later write an in-place overwrite of an existing block, which is
// both faster and much less likely to disturb FAT metadata on a card that can
// lose power mid-write.
#ifndef __SDLOG_STORE_H
#define __SDLOG_STORE_H

#include "sdlog_record.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef SDLOG_PATH
#define SDLOG_PATH "/sdcard/energy.log"
#endif

// Ring capacity in records. 1 048 576 records x 32 B = 32 MB, which at one sample
// a minute is ~2 years -- against the internal tsdb's ~100 days in 2.5 MB. The
// card is the reason to hold more, so hold more.
// 4M records x 32 B = 128 MB. Space is not the constraint on a card; at the
// 10 s charging cadence this is well over a year of continuous charging before
// the ring wraps, and the file is created once, in the background.
#ifndef SDLOG_CAPACITY
#define SDLOG_CAPACITY (4UL * 1024UL * 1024UL)
#endif

#ifdef ENABLE_SD_CARD

// Open (or create and preallocate) the ring and recover the head position.
// Returns false if the card is not mounted or the file cannot be established, in
// which case the caller should stay on internal flash.
bool sdlog_store_begin();

// True if the ring file exists on the card (any size). begin() removes a file
// of the wrong size, so "exists but begin() failed" means an I/O problem.
bool sdlog_store_exists();

// Create the ring file, blank. Slow (a full write of SDLOG_CAPACITY records),
// so run it from a background task, never from the main loop. Safe to call
// while the store is not open.
bool sdlog_store_preallocate();

bool sdlog_store_ready();

// Append one sample. Returns false on a write error, which also marks the store
// not-ready so the logger falls back rather than silently dropping samples.
bool sdlog_store_append(uint32_t timestamp, const int16_t cols[SDLOG_RECORD_COLS]);

// Close the ring, e.g. after the card is pulled.
void sdlog_store_end();

// Oldest and newest timestamps held, for /status and the query range.
bool sdlog_store_range(uint32_t &oldest, uint32_t &newest);

// --- Query ---------------------------------------------------------------
// Shaped like esp_tsdb's query so the web handlers can dispatch between the two
// without a second serialisation path.

struct SdlogQuery {
  uint32_t start_ts;
  uint32_t end_ts;
  uint32_t next_seq;    // sequence cursor, walking forwards
  uint32_t end_seq;     // one past the newest available
  bool     open;
};

// Position a cursor over [start_ts, end_ts]. Returns false if the store is not
// ready; an empty range is a successful open that yields no rows.
bool sdlog_query_init(SdlogQuery &q, uint32_t start_ts, uint32_t end_ts);

// Fetch the next record in range. Returns false at the end of the range.
// Records that fail their CRC are skipped, not surfaced as zeroes.
bool sdlog_query_next(SdlogQuery &q, uint32_t &ts, int16_t cols[SDLOG_RECORD_COLS]);

void sdlog_query_close(SdlogQuery &q);

// Count intact records in a range, without materialising them.
bool sdlog_query_count(uint32_t start_ts, uint32_t end_ts, uint32_t &count);

// --- Aggregation ---------------------------------------------------------
// Deliberately mirrors esp_tsdb's tsdb_aggregate_multi() so the two stores can
// be dispatched between without the caller restating the query. Kept as its own
// enum rather than reusing tsdb_agg_type_t so this header stays free of
// esp_tsdb, which the native ring tests do not link.

typedef enum {
  SDLOG_AGG_SUM,
  SDLOG_AGG_AVG,
  SDLOG_AGG_MIN,
  SDLOG_AGG_MAX,
  SDLOG_AGG_COUNT,
  SDLOG_AGG_FIRST,
  SDLOG_AGG_LAST
} sdlog_agg_type_t;

struct SdlogAggRequest {
  uint8_t          col;      // index into the record's columns
  sdlog_agg_type_t agg;
  int32_t          result;   // filled in by sdlog_aggregate_multi()
};

// Run every request over [start_ts, end_ts] in ONE pass of the ring, so N
// aggregations cost one scan rather than N. `scanned` receives the number of
// intact records seen, which is how callers tell "no data" from "all zeroes".
// Results for an empty range are left at 0 and scanned is 0.
bool sdlog_aggregate_multi(uint32_t start_ts, uint32_t end_ts,
                           SdlogAggRequest *reqs, uint8_t num_reqs,
                           uint32_t &scanned);

#else

// No-op stubs so call sites stay free of #ifdefs, matching sd_card.h and
// rtc_ds3231.h. Only the lifecycle and write entry points need them; the query
// functions are always called from behind an ENABLE_SD_CARD guard, because the
// caller has to pick a cursor type as well as a source.
static inline bool sdlog_store_begin() { return false; }
static inline bool sdlog_store_exists() { return false; }
static inline bool sdlog_store_preallocate() { return false; }
static inline bool sdlog_store_ready() { return false; }
static inline bool sdlog_store_append(uint32_t, const int16_t *) { return false; }
static inline void sdlog_store_end() { }
static inline bool sdlog_store_range(uint32_t &, uint32_t &) { return false; }

#endif // ENABLE_SD_CARD

#endif // __SDLOG_STORE_H
