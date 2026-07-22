#ifndef CERTIFICATE_ID_H
#define CERTIFICATE_ID_H

#include <stdint.h>
#include <stdio.h>

#include <string>

// Certificate IDs are the low 64 bits of an X.509 serial number. Format them
// without Arduino String's platform-dependent integer conversion so native and
// ESP32 builds produce the same uppercase hexadecimal storage key.
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
