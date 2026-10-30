// src/event_log_space.h -- keeping the event log out of the last 8 KB of LittleFS.
//
// LittleFS.totalBytes() + usedBytes() are two full filesystem traversals
// (30-140 ms each, flash reads under the FS lock), which stalled the main loop
// when they ran on every log() call. So the guard measures on a timer and,
// between measurements, deducts the bytes the event log itself writes.
//
// Other LittleFS writers (energy logger, certificates, config, OCPP) are not
// counted between measurements. HEADROOM bounds that: while the estimate is
// more than HEADROOM above the reserve, a write is trusted to the 30 s sample,
// which holds as long as the other writers add less than HEADROOM in that time.
// Once the estimate is inside the headroom, or below the reserve, it is
// re-measured before deciding, but no more often than NEAR_SAMPLE_MS, so a
// nearly full filesystem does not bring the per-entry stall back. The remaining
// exposure is a foreign write landing inside that window.
// LittleFS also allocates whole blocks, so a short append can consume a full
// block. HEADROOM covers that as well.
//
// Deliberately free of Arduino and LittleFS so it can be unit tested on the host.
#ifndef __EVENT_LOG_SPACE_H
#define __EVENT_LOG_SPACE_H

#include <stdint.h>
#include <stddef.h>

#ifndef EVENTLOG_FREE_SAMPLE_MS
#define EVENTLOG_FREE_SAMPLE_MS      30000
#endif
#ifndef EVENTLOG_NEAR_SAMPLE_MS
#define EVENTLOG_NEAR_SAMPLE_MS      5000
#endif
#ifndef EVENTLOG_MIN_FREE_BYTES
#define EVENTLOG_MIN_FREE_BYTES      8192
#endif
// Larger than an energy-logger raw chunk (up to ~9 KB) plus a few LittleFS blocks.
#ifndef EVENTLOG_FREE_HEADROOM_BYTES
#define EVENTLOG_FREE_HEADROOM_BYTES 32768
#endif

class EventLogSpaceGuard
{
  public:
    // Returns the filesystem's real free bytes. Expensive on the device.
    typedef size_t (*MeasureFree)();

  private:
    MeasureFree _measure;
    uint32_t _sampledAt;
    bool _sampled;
    int32_t _estimate;

    void sample(uint32_t nowMs)
    {
      size_t free = _measure();
      _estimate = free > INT32_MAX ? INT32_MAX : (int32_t)free;
      _sampledAt = nowMs;
      _sampled = true;
    }

  public:
    explicit EventLogSpaceGuard(MeasureFree measure) :
      _measure(measure),
      _sampledAt(0),
      _sampled(false),
      _estimate(0)
    {
    }

    // May an entry be written now?
    bool canWrite(uint32_t nowMs)
    {
      uint32_t age = nowMs - _sampledAt;
      if(!_sampled || age >= EVENTLOG_FREE_SAMPLE_MS) {
        sample(nowMs);
      } else if(_estimate < EVENTLOG_MIN_FREE_BYTES + EVENTLOG_FREE_HEADROOM_BYTES &&
                age >= EVENTLOG_NEAR_SAMPLE_MS) {
        sample(nowMs);
      }
      return _estimate >= EVENTLOG_MIN_FREE_BYTES;
    }

    // Deduct the bytes actually written, so the estimate stays an upper bound
    // on what this writer has consumed.
    void noteWritten(size_t bytes)
    {
      _estimate -= bytes > INT32_MAX ? INT32_MAX : (int32_t)bytes;
    }

    int32_t estimate() const { return _estimate; }
};

#endif // __EVENT_LOG_SPACE_H
