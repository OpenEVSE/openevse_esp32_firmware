#include <StreamSpy.h>

StreamSpy SerialDebug(DEBUG_PORT);
StreamSpy SerialEvse(RAPI_PORT);

#ifndef DEBUG_PORT_RX
#define DEBUG_PORT_RX -1
#endif

#ifndef DEBUG_PORT_TX
#define DEBUG_PORT_TX -1
#endif

#ifndef RAPI_PORT_RX
#define RAPI_PORT_RX -1
#endif

#ifndef RAPI_PORT_TX
#define RAPI_PORT_TX -1
#endif

void debug_setup()
{
  DEBUG_PORT.begin(115200, SERIAL_8N1, DEBUG_PORT_RX, DEBUG_PORT_TX);
  SerialDebug.begin(2048);

  RAPI_PORT.begin(115200, SERIAL_8N1, RAPI_PORT_RX, RAPI_PORT_TX);
  SerialEvse.begin(2048);

#if RAPI_PORT_RX != -1 && defined(RAPI_PORT_RX_PULLUP)
  // https://forums.adafruit.com/viewtopic.php?f=57&t=153553&p=759890&hilit=esp32+serial+pullup#p769168
  pinMode(RAPI_PORT_RX, INPUT_PULLUP);
#endif
}
