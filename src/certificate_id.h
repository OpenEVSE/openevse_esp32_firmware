#ifndef CERTIFICATE_ID_H
#define CERTIFICATE_ID_H

#include <stdint.h>
#include <stdio.h>

#include <string>

// Keep API IDs and storage filenames identical on native and ESP32 builds.
static inline std::string certificate_id_hex(uint64_t id)
{
  char encoded[17];
  int length = snprintf(encoded, sizeof(encoded), "%llX",
                        static_cast<unsigned long long>(id));
  if(length <= 0 || static_cast<size_t>(length) >= sizeof(encoded)) {
    return std::string();
  }
  return std::string(encoded, static_cast<size_t>(length));
}

#endif // CERTIFICATE_ID_H
