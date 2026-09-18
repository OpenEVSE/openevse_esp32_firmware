#ifndef CERTIFICATE_STORAGE_TRANSACTION_H
#define CERTIFICATE_STORAGE_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Stage a complete record without allocating path storage.
 * @tparam MaxFinalPathLength Maximum final-path length excluding NUL. The default
 * fits /certificates/<16 hex digits>.json; configured directories must supply
 * their corresponding bound. The buffer also reserves ".tmp" and its NUL.
 * @return True after publication by rename, false on invalid arguments or storage
 * failure. Oversized paths fail before any storage call; failed writes/renames
 * retain the existing final record.
 */
template <size_t MaxFinalPathLength = sizeof("/certificates/FFFFFFFFFFFFFFFF.json") - 1,
          typename Storage>
bool certificate_storage_commit(Storage &storage, const char *final_path,
                                const uint8_t *record, size_t record_size)
{
  if(nullptr == final_path || '\0' == final_path[0] || nullptr == record || 0 == record_size) {
    return false;
  }

  static_assert(MaxFinalPathLength <= SIZE_MAX - sizeof(".tmp"), "Certificate path bound overflows");
  size_t path_length = 0;
  while(path_length < MaxFinalPathLength && '\0' != final_path[path_length]) {
    ++path_length;
  }
  if('\0' != final_path[path_length]) {
    return false;
  }
  char temporary_path[MaxFinalPathLength + sizeof(".tmp")];
  memcpy(temporary_path, final_path, path_length);
  memcpy(temporary_path + path_length, ".tmp", sizeof(".tmp"));

  if(storage.exists(temporary_path) && !storage.remove(temporary_path)) {
    return false;
  }

  if(!storage.hasSpace(record_size)) {
    return false;
  }

  size_t written = 0;
  bool write_started = storage.write(temporary_path, record, record_size, written);
  if(!write_started || written != record_size)
  {
    storage.remove(temporary_path);
    return false;
  }

  // Publish only after the complete record has been written and closed.
  if(!storage.rename(temporary_path, final_path))
  {
    storage.remove(temporary_path);
    return false;
  }

  return true;
}

#endif // CERTIFICATE_STORAGE_TRANSACTION_H
