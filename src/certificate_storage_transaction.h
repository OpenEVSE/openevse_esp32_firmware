#ifndef CERTIFICATE_STORAGE_TRANSACTION_H
#define CERTIFICATE_STORAGE_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include <string>

template <typename Storage>
bool certificate_storage_commit(Storage &storage, const char *final_path,
                                const uint8_t *record, size_t record_size)
{
  if(nullptr == final_path || '\0' == final_path[0] || nullptr == record || 0 == record_size) {
    return false;
  }

  if(storage.exists(final_path)) {
    return false;
  }

  std::string temporary_path(final_path);
  temporary_path += ".tmp";

  if(storage.exists(temporary_path.c_str()) && !storage.remove(temporary_path.c_str())) {
    return false;
  }

  if(!storage.hasSpace(record_size)) {
    return false;
  }

  size_t written = 0;
  bool write_started = storage.write(temporary_path.c_str(), record, record_size, written);
  if(!write_started || written != record_size)
  {
    storage.remove(temporary_path.c_str());
    return false;
  }

  // Publish only after the complete record has been written and closed.
  if(!storage.rename(temporary_path.c_str(), final_path))
  {
    storage.remove(temporary_path.c_str());
    return false;
  }

  return true;
}

#endif // CERTIFICATE_STORAGE_TRANSACTION_H
