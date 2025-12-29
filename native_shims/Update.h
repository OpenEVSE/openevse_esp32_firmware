#pragma once

// Minimal host-build stub for ESP32 Arduino Update API.
// Used only for PlatformIO `native` builds.

#ifdef __cplusplus

#include <stddef.h>
#include <stdint.h>

class UpdateClass {
 public:
  bool isRunning() const { return false; }
  bool isFinished() const { return true; }
  bool begin(size_t /*size*/ = 0, int /*cmd*/ = 0) { return true; }
  size_t write(const uint8_t* /*data*/, size_t len) { return len; }
  bool end(bool /*evenIfRemaining*/ = false) { return true; }
  bool hasError() const { return false; }
  int getError() const { return 0; }
  void printError(void* /*out*/) const {}
};

extern UpdateClass Update;

#endif
