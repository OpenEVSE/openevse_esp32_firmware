#include "hal.h"
#include "emonesp.h"

#include <base64.h>

#if defined(ESP32)
HalEsp32 HAL = HalEsp32();
#elif defined(ESP8266)
HalEsp8266 HAL = HalEsp8266();
#endif

String HalClass::getShortId()
{
  String longId = getLongId();
  unsigned int len = longId.length();
  return longId.substring(len - HAL_SHORT_ID_LENGTH);
}

String HalClass::getLongId(int base)
{
// This probably needs to be platform specific, assumes the ChipID is a MAC address, only true on the ESP32
  if(10 == base || 16 == base)
  {
    uint64_t chipId = getChipId();
    // Swap the bytes arround so the unique bit is at right
    chipId = (chipId & 0x00000000FFFFFFFF) << 32 | (chipId & 0xFFFFFFFF00000000) >> 32;
    chipId = (chipId & 0x0000FFFF0000FFFF) << 16 | (chipId & 0xFFFF0000FFFF0000) >> 16;
    chipId = (chipId & 0x00FF00FF00FF00FF) << 8  | (chipId & 0xFF00FF00FF00FF00) >> 8;
    chipId = chipId >> 16;

    char longId[16];

    sniprintf(longId, sizeof(longId), 10 == base ? "%llu" : "%llx", chipId);

    return String(longId);
  }
  else if(64 == base)
  {
    uint64_t chipId = getChipId();
    uint8_t *idBytes = (uint8_t *)&chipId;

    return base64::encode(idBytes, 6);
  }

  return "Not Supported";
}

void HalClass::begin()
{
  RAPI_PORT.begin(115200);
  DEBUG_BEGIN(115200);

#ifdef SERIAL_RX_PULLUP_PIN
  // https://forums.adafruit.com/viewtopic.php?f=57&t=153553&p=759890&hilit=esp32+serial+pullup#p769168
  pinMode(SERIAL_RX_PULLUP_PIN, INPUT_PULLUP);
#endif

#ifdef ONBOARD_LEDS
  uint8_t ledPins[] = {ONBOARD_LEDS};
  for (uint8_t pin = 0; pin < sizeof(ledPins); pin++) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, !ONBOARD_LED_ON_STATE);
  }
#endif

  enableLoopWDT();
}
