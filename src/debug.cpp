#include <StreamSpy.h>
#include <cstdlib>  // for getenv

#if defined(RAPI_RX_PULLUP) && !defined(EPOXY_DUINO)
#include <driver/gpio.h>
#endif

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

// Helper function to get RAPI serial port path from environment or default
static const char* get_rapi_serial_path() {
  const char* env_path = std::getenv("RAPI_SERIAL_PORT");
  if (env_path && *env_path) {
    return env_path;
  }
  return "/tmp/rapi_pty";  // Default fallback
}

PtySerial SerialRapi(get_rapi_serial_path());

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
  // 1KB rings rather than 2KB. These are scrollback for the debug and RAPI
  // consoles; on a board this tight the 2KB of contiguous heap is worth more
  // than the extra history.
  DEBUG_PORT.begin(115200);
#if defined(ESP32) && ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // DEBUG_PORT is the USB-Serial-JTAG CDC. Once the host stops reading, its TX
  // ring fills and HWCDC::write() then blocks per byte for the TX timeout
  // (100 ms default): one JSON event serialised to the console outlasts the
  // 5 s task watchdog. Drop output instead of waiting when nobody drains it.
  DEBUG_PORT.setTxTimeoutMs(0);
#endif
  SerialDebug.begin(1024);

  RAPI_PORT.begin(115200);

#if defined(RAPI_RX_PULLUP) && !defined(EPOXY_DUINO)
  // Boards whose RAPI RX is level-shifted by a bare diode (anode on the GPIO) have
  // nothing pulling the line back up: the controller drives it low through the
  // diode and releases it to float. Without this the port reads as dead.
  //
  // gpio_set_pull_mode(), not pinMode(): on Arduino-ESP32 3.x pinMode() runs the
  // peripheral manager, which would detach the UART we just attached to this pin.
  gpio_set_pull_mode((gpio_num_t)RAPI_RX_PULLUP, GPIO_PULLUP_ONLY);
#endif

  SerialEvse.begin(1024);
}
