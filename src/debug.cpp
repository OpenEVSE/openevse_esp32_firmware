#include <StreamSpy.h>

#ifndef DEBUG_PORT
#ifdef ESP32
#define DEBUG_PORT Serial
#elif defined(ESP8266)
#define DEBUG_PORT Serial1
#else
#error Platform not supported
#endif
#endif

#ifndef RAPI_PORT
#ifdef ESP32
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
