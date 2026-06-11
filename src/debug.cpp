#include <StreamSpy.h>

#ifndef DEBUG_PORT
#if defined(ESP32) || defined(DIVERT_SIM) || defined(EPOXY_DUINO)
#define DEBUG_PORT Serial
#elif defined(ESP8266)
#define DEBUG_PORT Serial1
#else
#error Platform not supported
#endif
#endif

#ifndef RAPI_PORT
#if defined(DIVERT_SIM)
#define RAPI_PORT Serial
#elif defined(EPOXY_DUINO)
#define RAPI_PORT SerialRapi
#include "PtySerial.h"
PtySerial SerialRapi("/tmp/rapi_pty");

// Allow runtime override of the PTY path before begin()
extern "C" void debug_set_rapi_path(const char* path) {
  if (path && *path) {
    SerialRapi.setPortPath(path);
  }
}
#elif defined(ESP32)
#define RAPI_PORT Serial1
#elif defined(ESP8266)
#define RAPI_PORT Serial
#else
#error Platform not supported
#endif
#endif

StreamSpy SerialDebug(DEBUG_PORT);
StreamSpy SerialEvse(RAPI_PORT);

void debug_setup()
{
  DEBUG_PORT.begin(115200);
  SerialDebug.begin(2048);

  RAPI_PORT.begin(115200);
  SerialEvse.begin(2048);
}
