#ifdef ENABLE_SD_CARD

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

#include "sdlog_store.h"
#include <unistd.h>
#include <sys/stat.h>
#include "sdlog_ring.h"
#include "sd_card.h"
#include "debug.h"

// The ring is reached from two tasks. tsdb's writer task appends to it, while
// loopTask opens and closes it around card insertion/removal (sd_card.cpp) and
// reads it to answer history queries from the web server. They share one FILE*
// and one block cache, so every public entry point below takes this lock --
// without it a card pulled mid-append lets loopTask fclose() the handle the
// writer is still inside.
//
// Recursive because the query helpers call one another, and created in a global
// initialiser so it exists before any caller can reach it: there is no module
// init hook that is guaranteed to run first, and sdlog_store_ready() in
// particular is called before begin().
static SemaphoreHandle_t _lock = xSemaphoreCreateRecursiveMutex();

namespace {
// Scoped lock. Falls through un-held if the mutex could not be created, which
// only happens if the allocation failed at boot; the module is then no worse
// off than it was before the lock existed.
struct SdlogLock {
  bool held;
  SdlogLock() : held(_lock && xSemaphoreTakeRecursive(_lock, portMAX_DELAY) == pdTRUE) { }
  ~SdlogLock() { if(held) { xSemaphoreGiveRecursive(_lock); } }
  SdlogLock(const SdlogLock &) = delete;
  SdlogLock &operator=(const SdlogLock &) = delete;
};
}

static FILE    *_fp = nullptr;
static volatile bool _ready = false;   // read lock-free by sdlog_store_ready()
static uint32_t _next_seq = 0;
static uint32_t _oldest_seq = 0;   // lowest sequence still held
static uint32_t _newest_ts = 0;

// Read cache. A query walks records in sequence, and without this every one of
// them is its own fseek + 32-byte fread, which FATFS turns into a fresh 512-byte
// sector fetch over the 1-bit bus: ~10 ms a record, so a 500-point chart took
// six seconds. One 4 KB block (128 records) serves a run of sequential reads
// from a single multi-sector read. The cache is dropped whenever a slot in the
// cached block is written, and when the store closes.
#define SDLOG_CACHE_BYTES 4096
#define SDLOG_CACHE_RECS  (SDLOG_CACHE_BYTES / SDLOG_RECORD_BYTES)
static uint8_t *_cache = nullptr;
static uint32_t _cache_block = UINT32_MAX;   // index / SDLOG_CACHE_RECS, or none
static_assert(SDLOG_CAPACITY % SDLOG_CACHE_RECS == 0, "ring capacity must be a whole number of cache blocks");

static void cache_drop()
{
  _cache_block = UINT32_MAX;
}

static bool slot_read(void *ctx, uint32_t index, uint8_t out[SDLOG_RECORD_BYTES])
{
  (void)ctx;
  if(_fp == nullptr) {
    return false;
  }

  if(_cache == nullptr) {
    if(fseek(_fp, (long)index * SDLOG_RECORD_BYTES, SEEK_SET) != 0) {
      return false;
    }
    return fread(out, SDLOG_RECORD_BYTES, 1, _fp) == 1;
  }

  uint32_t block = index / SDLOG_CACHE_RECS;
  if(block != _cache_block)
  {
    // SDLOG_CAPACITY is a multiple of SDLOG_CACHE_RECS, so a block never runs
    // off the end of the file.
    if(fseek(_fp, (long)block * SDLOG_CACHE_BYTES, SEEK_SET) != 0 ||
       fread(_cache, SDLOG_CACHE_BYTES, 1, _fp) != 1)
    {
      cache_drop();
      return false;
    }
    _cache_block = block;
  }
  memcpy(out, _cache + (index % SDLOG_CACHE_RECS) * SDLOG_RECORD_BYTES, SDLOG_RECORD_BYTES);
  return true;
}

// Create the ring at full size. Written in blocks rather than record-by-record:
// a million 32-byte writes through the FAT layer would take minutes.
bool sdlog_store_preallocate()
{
  // Deliberately NOT locked. This writes the whole ring (128 MB over 1-bit
  // SDMMC -- minutes, not milliseconds) and runs on the sd_job task. Holding
  // _lock across it would park every loopTask caller of sdlog_store_ready() on
  // the mutex and trip the 5 s task watchdog, which is a reboot loop on a fresh
  // card: panic, reboot, begin() discards the short file, job restarts.
  //
  // It needs no lock. start_job() calls sdlog_store_end() first and
  // sd_card_mounted() reports false for the job's duration, so begin() and
  // append() both refuse to touch the file while this runs. The only shared
  // state here is the local FILE *fp.
  DBUGF("[sdlog] creating %lu-record ring (%lu MB), this takes a moment",
        (unsigned long)SDLOG_CAPACITY,
        (unsigned long)((uint64_t)SDLOG_CAPACITY * SDLOG_RECORD_BYTES / (1024 * 1024)));

  unsigned long t0 = millis();
  FILE *fp = fopen(SDLOG_PATH, "wb");
  if(fp == nullptr) {
    return false;
  }

  // 0xFF, matching erased flash and what sdlog_is_blank() treats as "never
  // written". Zeroes would work too, but 0xFF is what an unwritten card region
  // tends to look like and keeps the two indistinguishable on purpose.
  static const size_t CHUNK = 4096;
  uint8_t *blank = (uint8_t *)malloc(CHUNK);
  if(blank == nullptr) {
    fclose(fp);
    return false;
  }
  memset(blank, 0xFF, CHUNK);

  uint64_t total = (uint64_t)SDLOG_CAPACITY * SDLOG_RECORD_BYTES;
  bool ok = true;
  for(uint64_t written = 0; written < total; written += CHUNK)
  {
    size_t want = (size_t)((total - written) < CHUNK ? (total - written) : CHUNK);
    if(fwrite(blank, 1, want, fp) != want) {
      ok = false;
      break;
    }
  }

  free(blank);
  fclose(fp);

  if(!ok) {
    DBUGLN("[sdlog] ring creation failed (card full?)");
    remove(SDLOG_PATH);
  } else {
    // The one slow step in bring-up; SDLOG_CAPACITY is the knob if this is unreasonable.
    DBUGF("[sdlog] ring created in %lu ms", millis() - t0);
  }
  return ok;
}

bool sdlog_store_begin()
{
  SdlogLock lock;
  if(!sd_card_mounted()) {
    return false;
  }

  _fp = fopen(SDLOG_PATH, "r+b");
  if(_fp == nullptr)
  {
    // No ring yet. Creating one is slow, so it is left to sd_card_loop(),
    // which runs sdlog_store_preallocate() in a background task and calls
    // begin() again when it is done.
    if(sdlog_store_exists()) {
      DBUGLN("[sdlog] ring exists but would not open");
    }
    return false;
  }

  // A file that is not the expected size cannot be used: the seq % capacity
  // invariant that all the recovery logic rests on would break. This happens
  // when SDLOG_CAPACITY changes between builds or a preallocation was cut
  // short. Drop it so the next sd_card_loop() pass creates a fresh ring;
  // the old records were written under a different capacity and are not
  // recoverable in place.
  if(fseek(_fp, 0, SEEK_END) != 0) {
    fclose(_fp); _fp = nullptr;
    return false;
  }
  long size = ftell(_fp);
  if(size != (long)((uint64_t)SDLOG_CAPACITY * SDLOG_RECORD_BYTES)) {
    DBUGF("[sdlog] ring is %ld bytes, expected %llu -- discarding it",
          size, (unsigned long long)SDLOG_CAPACITY * SDLOG_RECORD_BYTES);
    fclose(_fp); _fp = nullptr;
    remove(SDLOG_PATH);
    return false;
  }

  if(_cache == nullptr) {
    _cache = (uint8_t *)malloc(SDLOG_CACHE_BYTES);   // PSRAM-eligible; nullptr just means uncached reads
  }
  cache_drop();

  SdlogRingScan scan;
  if(!sdlog_ring_scan(slot_read, nullptr, SDLOG_CAPACITY, scan)) {
    DBUGLN("[sdlog] head recovery failed");
    fclose(_fp); _fp = nullptr;
    return false;
  }

  _next_seq = scan.next_seq;
  _oldest_seq = (scan.next_seq > SDLOG_CAPACITY) ? (scan.next_seq - SDLOG_CAPACITY) : 0;
  _ready = true;

  if(scan.any_records) {
    DBUGF("[sdlog] resuming at seq %lu (head %lu, %lu probes, %lu damaged slots)",
          (unsigned long)scan.next_seq, (unsigned long)scan.newest_seq,
          (unsigned long)scan.probes, (unsigned long)scan.corrupt_seen);
  } else {
    DBUGLN("[sdlog] empty ring, starting at seq 0");
  }
  return true;
}

bool sdlog_store_exists()
{
  // No lock: a bare stat() touching no shared state, polled from loopTask.
  struct stat st;
  return stat(SDLOG_PATH, &st) == 0;
}

bool sdlog_store_ready()
{
  // No lock: this is polled from loopTask (/status, /energy/*, the tsdb
  // cadence) and must never block behind a card operation. A single volatile
  // bool read is atomic on this target, and every caller already has to cope
  // with the answer going stale the moment it returns.
  return _ready;
}

bool sdlog_store_append(uint32_t timestamp, const int16_t cols[SDLOG_RECORD_COLS])
{
  SdlogLock lock;
  if(!_ready || _fp == nullptr) {
    return false;
  }

  SdlogRecord rec;
  rec.seq = _next_seq;
  rec.timestamp = timestamp;
  memcpy(rec.cols, cols, sizeof(rec.cols));

  uint8_t raw[SDLOG_RECORD_BYTES];
  sdlog_encode(rec, raw);

  uint32_t index = _next_seq % SDLOG_CAPACITY;
  // fflush() only hands the record to FATFS, whose sector window is not written
  // to the card until something evicts it or fsync() forces it. On a board
  // whose power goes with the EVSE's, that window is exactly what gets lost:
  // the first card on the S3 board came up "empty ring" after every reset
  // although the sample had been readable. One sector write per sample.
  if(index / SDLOG_CACHE_RECS == _cache_block) {
    cache_drop();
  }
  if(fseek(_fp, (long)index * SDLOG_RECORD_BYTES, SEEK_SET) != 0 ||
     fwrite(raw, SDLOG_RECORD_BYTES, 1, _fp) != 1 ||
     fflush(_fp) != 0 ||
     fsync(fileno(_fp)) != 0)
  {
    // Do not keep trying into a broken card: drop to not-ready so the caller
    // falls back to internal flash on this and every later sample.
    //
    // Close the handle as well as clearing the flag. sd_card_loop() reopens the
    // ring whenever it sees !ready && !gave_up, and begin() assigns straight
    // over _fp -- so leaving this one open leaked a FILE* per failure until
    // SD_MMC's maxOpenFiles (5) was exhausted and fopen() failed for good.
    DBUGLN("[sdlog] append failed, falling back to internal flash");
    fclose(_fp);
    _fp = nullptr;
    cache_drop();
    _ready = false;
    return false;
  }

  _next_seq++;
  _newest_ts = timestamp;
  if(_next_seq > SDLOG_CAPACITY) {
    _oldest_seq = _next_seq - SDLOG_CAPACITY;
  }
  return true;
}

void sdlog_store_end()
{
  SdlogLock lock;
  cache_drop();
  if(_fp != nullptr) {
    fclose(_fp);
    _fp = nullptr;
  }
  _ready = false;
}

// Read one record by sequence, verifying it belongs where it was found.
static bool read_seq(uint32_t seq, SdlogRecord &rec)
{
  uint32_t index = seq % SDLOG_CAPACITY;
  uint8_t raw[SDLOG_RECORD_BYTES];
  if(!slot_read(nullptr, index, raw)) {
    return false;
  }
  if(!sdlog_decode(raw, rec)) {
    return false;
  }
  // The slot holds SOME intact record; make sure it is the lap we asked for and
  // not the previous one still sitting there.
  return rec.seq == seq;
}

bool sdlog_store_range(uint32_t &oldest, uint32_t &newest)
{
  SdlogLock lock;
  if(!_ready || _next_seq == 0) {
    return false;
  }

  SdlogRecord rec;
  oldest = 0;
  newest = _newest_ts;

  // Walk forward from the nominal oldest until an intact record turns up; a
  // damaged tail should not present as "no history".
  for(uint32_t seq = _oldest_seq; seq < _next_seq && seq < _oldest_seq + SDLOG_RECS_PER_SECTOR * 4; seq++)
  {
    if(read_seq(seq, rec)) {
      oldest = rec.timestamp;
      break;
    }
  }

  if(newest == 0)
  {
    for(uint32_t back = 1; back <= SDLOG_RECS_PER_SECTOR * 4 && back <= _next_seq; back++)
    {
      if(read_seq(_next_seq - back, rec)) {
        newest = rec.timestamp;
        break;
      }
    }
  }

  return oldest != 0 || newest != 0;
}

// Find the lowest sequence whose timestamp is >= `ts`. Timestamps ascend with
// sequence, so this is a binary search -- the same reason the head scan is one.
static uint32_t seek_seq_for_ts(uint32_t ts)
{
  uint32_t lo = _oldest_seq;
  uint32_t hi = _next_seq;

  while(lo < hi)
  {
    uint32_t mid = lo + (hi - lo) / 2;

    SdlogRecord rec;
    uint32_t probe = mid;
    bool got = false;

    // Step forward over damage; the window only ever shrinks because the
    // narrowing below uses mid, never probe.
    for(uint32_t step = 0; step < SDLOG_RECS_PER_SECTOR && probe + step < hi; step++)
    {
      if(read_seq(probe + step, rec)) {
        got = true;
        break;
      }
    }

    if(got && rec.timestamp < ts) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  return lo;
}

bool sdlog_query_init(SdlogQuery &q, uint32_t start_ts, uint32_t end_ts)
{
  SdlogLock lock;
  q.open = false;
  if(!_ready) {
    return false;
  }

  q.start_ts = start_ts;
  q.end_ts   = end_ts;
  q.next_seq = seek_seq_for_ts(start_ts);
  q.end_seq  = _next_seq;
  q.open     = true;
  return true;
}

bool sdlog_query_next(SdlogQuery &q, uint32_t &ts, int16_t cols[SDLOG_RECORD_COLS])
{
  SdlogLock lock;
  if(!q.open) {
    return false;
  }
  if(!_ready) {
    // Card pulled between calls. Every slot_read() would now fail and we would
    // grind through the whole ring holding the lock to learn that. End it here.
    q.open = false;
    return false;
  }

  while(q.next_seq < q.end_seq)
  {
    SdlogRecord rec;
    uint32_t seq = q.next_seq++;

    if(!read_seq(seq, rec)) {
      continue;   // torn or overwritten mid-read; skip rather than emit zeroes
    }
    if(rec.timestamp < q.start_ts) {
      continue;
    }
    if(rec.timestamp > q.end_ts) {
      q.open = false;
      return false;
    }

    ts = rec.timestamp;
    memcpy(cols, rec.cols, sizeof(rec.cols));
    return true;
  }

  q.open = false;
  return false;
}

void sdlog_query_close(SdlogQuery &q)
{
  SdlogLock lock;
  q.open = false;
}

bool sdlog_aggregate_multi(uint32_t start_ts, uint32_t end_ts,
                           SdlogAggRequest *reqs, uint8_t num_reqs,
                           uint32_t &scanned)
{
  SdlogLock lock;
  scanned = 0;
  if(reqs == nullptr || num_reqs == 0) {
    return false;
  }

  // Running state per request. AVG needs the sum and the count separately, and
  // MIN/MAX need to know whether they have seen anything yet -- seeding them
  // from the column's first value is the only way to avoid a sentinel that
  // would be indistinguishable from a real reading.
  int64_t acc[8]   = {0};
  bool    seeded[8] = {false};
  if(num_reqs > 8) {
    num_reqs = 8;
  }
  for(uint8_t r = 0; r < num_reqs; r++) {
    reqs[r].result = 0;
  }

  SdlogQuery q;
  if(!sdlog_query_init(q, start_ts, end_ts)) {
    return false;
  }

  uint32_t ts;
  int16_t cols[SDLOG_RECORD_COLS];
  while(sdlog_query_next(q, ts, cols))
  {
    scanned++;
    for(uint8_t r = 0; r < num_reqs; r++)
    {
      if(reqs[r].col >= SDLOG_RECORD_COLS) {
        continue;
      }
      int32_t v = cols[reqs[r].col];

      switch(reqs[r].agg)
      {
        case SDLOG_AGG_SUM:
        case SDLOG_AGG_AVG:
          acc[r] += v;
          break;
        case SDLOG_AGG_COUNT:
          acc[r]++;
          break;
        case SDLOG_AGG_MIN:
          if(!seeded[r] || v < acc[r]) { acc[r] = v; }
          break;
        case SDLOG_AGG_MAX:
          if(!seeded[r] || v > acc[r]) { acc[r] = v; }
          break;
        case SDLOG_AGG_FIRST:
          if(!seeded[r]) { acc[r] = v; }
          break;
        case SDLOG_AGG_LAST:
          acc[r] = v;
          break;
      }
      seeded[r] = true;
    }
  }
  sdlog_query_close(q);

  for(uint8_t r = 0; r < num_reqs; r++) {
    int64_t out = acc[r];
    if(SDLOG_AGG_AVG == reqs[r].agg) {
      out = (scanned > 0) ? (acc[r] / (int64_t)scanned) : 0;
    }
    // The column type is int16 but SUM over a day of samples is not, so the
    // result is int32 like esp_tsdb's; clamp rather than wrap on a pathological
    // range.
    if(out > INT32_MAX) { out = INT32_MAX; }
    if(out < INT32_MIN) { out = INT32_MIN; }
    reqs[r].result = (int32_t)out;
  }
  return true;
}

bool sdlog_query_count(uint32_t start_ts, uint32_t end_ts, uint32_t &count)
{
  SdlogLock lock;
  SdlogQuery q;
  if(!sdlog_query_init(q, start_ts, end_ts)) {
    return false;
  }

  count = 0;
  uint32_t ts;
  int16_t cols[SDLOG_RECORD_COLS];
  while(sdlog_query_next(q, ts, cols)) {
    count++;
  }
  sdlog_query_close(q);
  return true;
}

#endif // ENABLE_SD_CARD
