#ifndef _FS_USAGE_H
#define _FS_USAGE_H

#include <stdint.h>
#include <atomic>

// Cached LittleFS total/used bytes for reporting (/status, /config).
//
// LittleFS.usedBytes() runs lfs_fs_size(), a full traversal of the filesystem,
// with the esp_littlefs lock held. Worse, it has to take that lock first: on
// the TSDB builds the tsdb_writer task holds it for the whole of a write, and a
// write that lands on a metadata compaction can run past the 5 s task watchdog.
// loopTask then waits on the lock inside a GET /config and the watchdog fires
// there instead (openevse-crash-service#22).
//
// So readers on loopTask only ever read the cached numbers. A task that writes
// the filesystem in the background registers as the sampler and refreshes the
// cache after each write, while it is the one paying for the lock anyway, and
// on a timer so writes by other subsystems show up too. It takes the first
// sample before registering, so get() never finds the cache empty.
// Without a background sampler (builds whose writers all run on loopTask, so
// there is no other task to wait for) get() refreshes when the sample is older
// than max_age_ms, as /status did before.
class FsUsageCache
{
  public:
    typedef uint32_t (*Reader)();

    FsUsageCache(Reader total, Reader used, uint32_t max_age_ms) :
      _read_total(total),
      _read_used(used),
      _max_age_ms(max_age_ms)
    {
    }

    // Takes the filesystem lock; call it from the task doing the writes.
    void refresh(uint32_t now)
    {
      // The partition size never changes, so only the first sample pays for it.
      if(0 == _total) {
        _total = _read_total();
      }
      _used = _read_used();
      _sampled_at = now;
      _sampled = true;
    }

    void setBackgroundSampler() {
      _background = true;
    }

    void get(uint32_t now, uint32_t &total, uint32_t &used)
    {
      if(!_sampled ||
         (!_background && (now - _sampled_at) >= _max_age_ms))
      {
        refresh(now);
      }
      total = _total;
      used = _used;
    }

  private:
    Reader _read_total;
    Reader _read_used;
    uint32_t _max_age_ms;
    std::atomic<uint32_t> _total{0};
    std::atomic<uint32_t> _used{0};
    std::atomic<uint32_t> _sampled_at{0};
    std::atomic<bool> _sampled{false};
    std::atomic<bool> _background{false};
};

extern FsUsageCache littlefs_usage;

#endif // _FS_USAGE_H
