#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_DIVERT)
#undef ENABLE_DEBUG
#endif

// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#include <Arduino.h>
#include "emonesp.h"
#include "input.h"
#include "app_config.h"
#include "RapiSender.h"
#include "mqtt.h"
#include "event.h"
#include "openevse.h"
#include "divert.h"
#include "emoncms.h"

#include <sys/time.h>

// 1: Normal / Fast Charge (default):
// Charging at maximum rate irrespective of solar PV / grid_ie output

// 2: Eco :
// Either modulate charge rate based solar PV generation (if only solar PV feed is available)

// Or modulate charge rate based on on excess power (if grid feed (positive import / negative export) is available) i.e. power that would otherwise be exported to the grid is diverted to EVSE
// Note: it's Assumed EVSE power is included in grid feed e.g. (charge rate = gen - use - EVSE).

// It's better to never import current from the grid but because of charging current quantification (min value of 6A and change in steps of 1A),
// it may be better to import a fraction of the charge current and use the next charging level sooner, depending on electricity buying/selling prices.
// The marginal fraction of current that is required to come from PV is the divert_PV_ratio. The requiring a pure PV charge is obtained by
// setting divert_PV_ratio to 1.0. This is the best choice when the kWh selling price is the same as the kWh night buying price.  If instead the night tarif
// is the same as the day tarif, any available current is good to take and divert_PV_ratio optimal setting is close to 0.0. Beyond 1.0, the excess of
// divert_PV_ratio indicates the amount of power (in kW) that OpenEVSE will try to preserve. A value of 1.1 will start charging only when the
// PV power - 100 W (reserve) reaches the minimum charging power (reproducing the legacy behavior of OpenEVSE).

// If EVSE is sleeping, charging will start as soon as solar PV / excess power exceeds the divert_PV_ratio fraction of the minimum charging power.
// Once charging begins it will not pause before a minimum amount of time has passed and this even if solar PV / excess power drops less then minimum charge rate.
// This avoids wear on the relay and the car.

// Default to normal charging unless set. Divert mode always defaults back to 1 if unit is reset (_mode not saved in EEPROM)

int solar = 0;
int grid_ie = 0;

// define as 'weak' so the simulator can override
time_t __attribute__((weak)) divertmode_get_time()
{
  struct timeval now;
  gettimeofday(&now, NULL);
  return now.tv_sec;
}

DivertTask::DivertTask(EvseManager &evse) :
  _evse(&evse),
  _mode(DivertMode::Normal),
  _state(EvseState::None),
  _last_update(0),
  _charge_rate(0),
  _evseState(this),
  _available_current(0),
  _smoothed_available_current(0),
  _min_charge_end(0)
{

}

DivertTask::~DivertTask()
{

}

void DivertTask::begin()
{
  MicroTask.startTask(this);
}

void DivertTask::setup()
{
  _evse->onStateChange(&_evseState);
}

unsigned long DivertTask::loop(MicroTasks::WakeReason reason)
{
  DBUG("Divert woke: ");
  DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
         WakeReason_Event == reason ? "WakeReason_Event" :
         WakeReason_Message == reason ? "WakeReason_Message" :
         WakeReason_Manual == reason ? "WakeReason_Manual" :
         "UNKNOWN");

  if(_evseState.IsTriggered() && _evse_last_state != _evse->getEvseState())
  {
    _evse_last_state = _evse->getEvseState();
    if(_evse->isCharging()) {
      _min_charge_end = divertmode_get_time() + divert_min_charge_time;
    }
  }

  return MicroTask.Infinate;
}

// Update divert mode e.g. Normal / Eco
// function called when divert mode is changed
void DivertTask::setMode(DivertMode mode)
{
  DBUGF("Set _mode: %d", mode);
  if(_mode != mode)
  {
    _mode = mode;

    StaticJsonDocument<128> event;
    event["divertmode"] = (uint8_t)_mode;
    _state = EvseState::None;
    event["divert_active"] = false;

    switch(_mode)
    {
      case DivertMode::Normal:
        _evse->release(EvseClient_OpenEVSE_Divert);
        break;

      case DivertMode::Eco:
      {
        _min_charge_end = 0;

        event["charge_rate"] = _charge_rate = 0;
        event["available_current"] = _available_current = 0;
        event["smoothed_available_current"] = _smoothed_available_current = 0;

        EvseProperties props(EvseState::Disabled);
        _evse->claim(EvseClient_OpenEVSE_Divert, EvseManager_Priority_Default, props);
      } break;

      default:
        return;
    }

    event_send(event);
  }
}

// Set charge rate depending on divert mode and solar / grid_ie
void DivertTask::update_state()
{
  Profile_Start(DivertTask::update_state);

  StaticJsonDocument<256> event;
  event["divert_update"] = 0;

  if(mqtt_grid_ie != "") {
    event["grid_ie"] = grid_ie;
  } else {
    event["solar"] = solar;
  }

  // If divert mode = Eco (2)
  if (_mode == DivertMode::Eco)
  {
    double voltage = _evse->getVoltage();

    // Calculate current
    if (mqtt_grid_ie != "")
    {
      // if grid feed is available and exporting increment the charge rate,
      // if importing drop the charge rate.
      // grid_ie is negative when exporting
      // If grid feeds is available and exporting (negative)

      DBUGVAR(voltage);
      double Igrid_ie = (double)grid_ie / voltage;
      DBUGVAR(Igrid_ie);

      // Subtract the current charge the EV is using from the Grid IE
      double amps = _evse->getAmps();
      DBUGVAR(amps);

      Igrid_ie -= amps;
      DBUGVAR(Igrid_ie);

      if (Igrid_ie < 0)
      {
        // If excess power
        double reserve = (1000.0 * ((divert_PV_ratio > 1.0) ? (divert_PV_ratio - 1.0) : 0.0)) / voltage;
        DBUGVAR(reserve);
        _available_current = (-Igrid_ie - reserve);
      }
      else
      {
        // no excess, so use the min charge
        _available_current = 0;
      }
    }
    else if (mqtt_solar!="")
    {
      // if grid feed is not available: charge rate = solar generation
      DBUGVAR(voltage);
      _available_current = (double)solar / voltage;
    }

    if(_available_current < 0) {
      _available_current = 0;
    }
    DBUGVAR(_available_current);

    double scale = (_available_current > _smoothed_available_current ?
                      divert_attack_smoothing_factor :
                      divert_decay_smoothing_factor);
    _smoothed_available_current = (_available_current * scale) + (_smoothed_available_current * (1 - scale));
    DBUGVAR(_smoothed_available_current);

    _charge_rate = (int)floor(_available_current);
    // if the remaining current can be used with a sufficient ratio of PV current in it, use it
    if ((_available_current - _charge_rate) > min(1.0, divert_PV_ratio)) {
      _charge_rate += 1;
    }

    DBUGVAR(_charge_rate);

    time_t min_charge_time_remaining = getMinChargeTimeRemaining();
    DBUGVAR(min_charge_time_remaining);

    double trigger_current = _evse->getMinCurrent() * min(1.0, divert_PV_ratio);

    // the smoothed current suffices to ensure a sufficient ratio of PV power
    if (_smoothed_available_current >= trigger_current)
    {
      EvseProperties props(EvseState::Active);
      props.setChargeCurrent(_charge_rate);
      _evse->claim(EvseClient_OpenEVSE_Divert, EvseManager_Priority_Divert, props);
    }
    else
    {
      if( EvseState::Active == _evse->getState(EvseClient_OpenEVSE_Divert) && 0 == min_charge_time_remaining)
      {
        EvseProperties props(EvseState::Disabled);
        _evse->claim(EvseClient_OpenEVSE_Divert, EvseManager_Priority_Default, props);
      }
    }

    EvseState current_evse_state = _evse->getState();
    if(_evse->getState(EvseClient_OpenEVSE_Divert) == current_evse_state)
    {
      // We are in the state we expect
      if(_state != current_evse_state)
      {
        _state = current_evse_state;
        event["divert_active"] = isActive();
      }
    }

    event["charge_rate"] = _charge_rate;
    event["trigger_current"] = trigger_current;
    event["voltage"] = voltage;
    event["available_current"] = _available_current;
    event["smoothed_available_current"] = _smoothed_available_current;
    event["pilot"] = _evse->getChargeCurrent();
    event["min_charge_end"] = min_charge_time_remaining;
  } // end ecomode

  event_send(event);
  emoncms_publish(event);

  _last_update = millis();

  Profile_End(DivertTask::update_state, 5);
} //end divert_update_state

bool DivertTask::isActive()
{
  return EvseState::Active == _state;
}

time_t DivertTask::getMinChargeTimeRemaining()
{
  return (isActive() &&
          _evse->isCharging() &&
          divertmode_get_time() < _min_charge_end) ?
            _min_charge_end - divertmode_get_time() :
            0;
}
