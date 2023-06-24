#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_DIVERT)
#undef ENABLE_DEBUG
#endif

// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#include <Arduino.h>
#include "emonesp.h"
#include "divert.h"
#include "emoncms.h"
#include "event.h"

#include <sys/time.h>

// 1: Normal / Fast Charge (default):
// Charging at maximum rate irrespective of solar PV / grid_ie output

// 2: Eco :
// Either modulate charge rate based solar PV generation (if only solar PV feed is available)

// Or modulate charge rate based on on excess power (if grid feed (positive import / negative export) is available) i.e. power that would otherwise be exported to the grid is diverted to EVSE
// Note: it's Assumed EVSE power is included in grid feed e.g. (charge rate = gen - use - EVSE).

// It's better to never import current from the grid but because of charging current quantification (min value of 6A and change in steps of 1A),
// it may be better to import a fraction of the charge current and use the next charging level sooner, depending on electricity buying/selling prices.

// If EVSE is sleeping, charging will start as soon as solar PV / excess power exceeds the divert_reserve_power + divert_hysteresis_power.
// Once charging begins it will not pause before a minimum amount of time has passed and this even if solar PV / excess power drops less then minimum charge rate.
/// This avoids wear on the relay and the car.

// Default to normal charging unless set. Divert mode always defaults back to 1 if unit is reset (_mode not saved in EEPROM)

int divert_solar_w = 0;
int divert_grid_ie_w = 0;

// define as 'weak' so the simulator can override
time_t __attribute__((weak)) divertmode_get_time()
{
  struct timeval now;
  gettimeofday(&now, NULL);
  return now.tv_sec;
}

inline int current_to_int(double const current) {
  if (current < 0)
    return 0;
  return static_cast<int>(floor(current));
}


DivertTask::DivertTask(EvseManager &evse) :
  _evse(&evse),
  _mode(DivertMode::Normal),
  _state(EvseState::None),
  _last_update(0),
  _charge_current(0),
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
  // remove this after few versions
  initDivertType();
  //
  MicroTask.startTask(this);
}

void DivertTask::setup()
{
  _evse->onStateChange(&_evseState);
}

double DivertTask::power_w_to_current_a(double p) {
  return p / voltage();
}

double DivertTask::voltage() {
  double voltage = _evse->getVoltage();
  if (config_threephase_enabled()) {
    voltage = voltage * 3;
  }
  return voltage;
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
        event["divert_charge_current"] = _charge_current = 0;
        event["divert_available_current"] = _available_current = 0;
        event["divert_smoothed_available_current"] = _smoothed_available_current = 0;

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

  double divert_grid_ie_a = 0;
  double divert_solar_a = 0;

  if (divert_type == DIVERT_TYPE_GRID)
  {
    event["divert_grid_ie_w"] = divert_grid_ie_w;
    divert_grid_ie_a = power_w_to_current_a(divert_grid_ie_w);
    event["divert_grid_ie_a"] = divert_grid_ie_a;
  }
  else if (divert_type == DIVERT_TYPE_SOLAR)
  {
    event["divert_solar_w"] = divert_solar_w;
    divert_solar_a = power_w_to_current_a(divert_solar_w);
    event["divert_solar_a"] = divert_solar_a;
  }

  // If divert mode = Eco (2)
  if (_mode == DivertMode::Eco)
  {
    double const divert_reserve_current = power_w_to_current_a(divert_reserve_power_w);

    // Calculate current
    if (divert_type == DIVERT_TYPE_GRID)
    {
      // if grid feed is available and exporting increment the charge rate,
      // if importing drop the charge rate.
      // grid_ie is negative when exporting
      // If grid feeds is available and exporting (negative)
      _available_current = (-divert_grid_ie_a + _evse->getAmps() - divert_reserve_current);
    }
    else if (divert_type == DIVERT_TYPE_SOLAR)
    {
      // if grid feed is not available: charge rate = solar generation
      _available_current = max(0.0, divert_solar_a - divert_reserve_current);
    }
    DBUGVAR(_available_current);

    double scale = (_available_current > _smoothed_available_current ?
                      divert_attack_smoothing_time :
                      divert_decay_smoothing_time);
    _smoothed_available_current = _inputFilter.filter(_available_current, _smoothed_available_current, scale);
    DBUGVAR(_smoothed_available_current);
    _charge_current = current_to_int(_smoothed_available_current);
    DBUGVAR(_charge_current);

    time_t const min_charge_time_remaining = getMinChargeTimeRemaining();
    DBUGVAR(min_charge_time_remaining);

    double const trigger_current = static_cast<double>(_evse->getMinCurrent()) + power_w_to_current_a(divert_hysteresis_power_w);


    // the smoothed current suffices to ensure sufficient PV power
    if ((_smoothed_available_current >= trigger_current)
        || (_evse->getState(EvseClient_OpenEVSE_Divert) == EvseState::Active && _smoothed_available_current >= trigger_current))
    {
      EvseProperties props(EvseState::Active);
      props.setChargeCurrent(_charge_current);
      _evse->claim(EvseClient_OpenEVSE_Divert, EvseManager_Priority_Divert, props);
    }
    else if (_smoothed_available_current < trigger_current)
    {
      if( _evse->getState(EvseClient_OpenEVSE_Divert) == EvseState::Active
          && min_charge_time_remaining == 0)
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
      }
    }

    event["divert_active"] = isActive();
    event["divert_charge_current"] = _charge_current;
    event["divert_trigger_current"] = trigger_current;
    event["voltage"] = voltage();
    event["divert_available_current"] = _available_current;
    event["divert_smoothed_available_current"] = _smoothed_available_current;
    event["pilot"] = _evse->getChargeCurrent();
    event["divert_min_charge_time_rem_s"] = min_charge_time_remaining;
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

// compatiblity trick, to remove after few version upgrade
void DivertTask::initDivertType() {

  if (divert_type == DIVERT_TYPE_UNSET) {
    // divert_type unset, guess previous version setup for smoother upgrade
    if (mqtt_grid_ie) {
      divert_type == DIVERT_TYPE_GRID;
    }
    else {
      divert_type == DIVERT_TYPE_SOLAR;
    }
    DynamicJsonDocument doc(JSON_OBJECT_SIZE(1) + 1); // use JSON in no-copy mode
    doc["divert_type"] = divert_type;
    config_deserialize(doc);
    config_commit();
  }
}
//
