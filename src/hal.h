#ifndef _HAL_H
#define _HAL_H

#include <Arduino.h>

class HalClass
{
  public:
    virtual uint64_t getChipId() = 0;
    virtual uint32_t getFreeHeap() = 0;
    virtual uint32_t getFlashChipSize() = 0;

    virtual void reset() = 0;
    virtual void eraseConfig() = 0;

    virtual String getShortId();
    virtual void begin();
};

#if defined(ESP32)
#include "hal_esp32.h"
extern HalEsp32 HAL;
#elif defined(ESP8266)
#include "hal_esp8266.h"
extern HalEsp8622 HAL;
#else
#error Platform not supported
#endif

#endif // !_HAL_H
