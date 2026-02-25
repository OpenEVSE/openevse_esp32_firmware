#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "lcd_backlight.h"
#include "app_config.h"

LcdBacklight::LcdBacklight() :
  _evse(NULL),
  _controlFn(nullptr),
  _backlightTimeout(0),
  _backlightOn(true),
  _previousEvseState(OPENEVSE_STATE_STARTING),
  _previousVehicleState(false)
{
}

void LcdBacklight::begin(EvseManager &evse, BacklightControlFn controlFn)
{
  _evse = &evse;
  _controlFn = controlFn;
}

void LcdBacklight::setBacklight(bool on)
{
  if(_backlightOn == on) {
    return;
  }

  _backlightOn = on;

  if(_controlFn) {
    DBUGF("Setting LCD backlight %s", on ? "ON" : "OFF");
    _controlFn(on);
  }
}

void LcdBacklight::wake()
{
  if(lcd_backlight_timeout > 0) {
    DBUGLN("Waking LCD backlight");
    setBacklight(true);
    // Cap maximum timeout to ~49 days to prevent overflow
    uint32_t timeout_sec = lcd_backlight_timeout;
    if(timeout_sec > 4294967UL) {
      timeout_sec = 4294967UL;
    }
    _backlightTimeout = millis() + (timeout_sec * 1000UL);
  }
}

void LcdBacklight::update()
{
  if(!_evse) {
    return;
  }

  // If backlight timeout is disabled (0), keep backlight on
  if(lcd_backlight_timeout == 0) {
    if(!_backlightOn) {
      setBacklight(true);
    }
    return;
  }

  // Check for state changes that should wake the backlight
  bool vehicleState = _evse->isVehicleConnected();
  uint8_t evseState = _evse->getEvseState();

  if(_previousEvseState != evseState || _previousVehicleState != vehicleState) {
    wake();
    _previousEvseState = evseState;
    _previousVehicleState = vehicleState;
    return;
  }

  // Keep backlight on during error conditions
  if(_evse->isError()) {
    if(!_backlightOn) {
      setBacklight(true);
    }
    return;
  }

  // Keep backlight on during charging
  if(_evse->isCharging()) {
    if(!_backlightOn) {
      setBacklight(true);
    }
    return;
  }

  // Check if timeout has expired (handle millis() rollover)
  if(_backlightOn && (long)(millis() - _backlightTimeout) >= 0) {
    DBUGLN("LCD backlight timeout expired");
    setBacklight(false);
  }
}
