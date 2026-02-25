#ifndef LCD_BACKLIGHT_H
#define LCD_BACKLIGHT_H

#include <Arduino.h>
#include <functional>
#include "evse_man.h"

class LcdBacklight
{
  public:
    typedef std::function<void(bool on)> BacklightControlFn;

    LcdBacklight();

    void begin(EvseManager &evse, BacklightControlFn controlFn);

    // Wake the backlight (turn on and reset timeout)
    void wake();

    // Update backlight state based on EVSE state and timeout
    void update();

    bool isOn() const { return _backlightOn; }

  private:
    void setBacklight(bool on);

    EvseManager *_evse;
    BacklightControlFn _controlFn;
    unsigned long _backlightTimeout;
    bool _backlightOn;
    uint8_t _previousEvseState;
    bool _previousVehicleState;
};

#endif // LCD_BACKLIGHT_H
