#ifndef _FS_UTIL_H
#define _FS_UTIL_H

#include <LittleFS.h>

// Opening a file with "w"/FILE_WRITE truncates it immediately, so free space
// must be checked BEFORE opening. Otherwise, on a (nearly) full filesystem the
// existing file is emptied and the replacement write fails, leaving a
// truncated/corrupt file — which can then crash on the next read or at boot.
//
// Requires `needed` bytes plus an 8 KB safety margin (LittleFS also needs free
// blocks for its own metadata/garbage collection, especially on the small 4 MB
// 128 KB partition).
// Free bytes, clamped at 0. Two full filesystem traversals: expensive.
static inline size_t littlefs_free_bytes()
{
  size_t total = LittleFS.totalBytes();
  size_t used  = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

static inline bool littlefs_has_space(size_t needed)
{
  return littlefs_free_bytes() >= needed + 8192;
}

#endif // _FS_UTIL_H
