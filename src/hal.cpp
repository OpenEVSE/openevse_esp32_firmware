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
  uint64_t chipId = getChipId();
  uint8_t shortId[3];
  shortId[0] = chipId & 0xff;
  shortId[1] = (chipId >> 8) & 0xff;
  shortId[2] = (chipId >> 16) & 0xff;

  return base64::encode(shortId, sizeof(shortId));
}

void HalClass::begin()
{
  RAPI_PORT.begin(115200);
  DEBUG_BEGIN(115200);

#ifdef SERIAL_RX_PULLUP_PIN
  pinMode(SERIAL_RX_PULLUP_PIN, INPUT_PULLUP);
#endif

#ifdef ONBOARD_LEDS
  uint8_t ledPins[] = {ONBOARD_LEDS};
  for (uint8_t pin = 0; pin < sizeof(ledPins); pin++) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, !ONBOARD_LED_ON_STATE);
  }
#endif
}
