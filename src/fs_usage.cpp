#include "fs_usage.h"

#include <LittleFS.h>

// Without a background sampler, /status and /config refresh at most this often.
#define LFS_USAGE_MAX_AGE_MS 30000

FsUsageCache littlefs_usage(
  []() -> uint32_t { return (uint32_t)LittleFS.totalBytes(); },
  []() -> uint32_t { return (uint32_t)LittleFS.usedBytes(); },
  LFS_USAGE_MAX_AGE_MS);
