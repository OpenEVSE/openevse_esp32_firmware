#include "temp_throttle.h"

TempThrottleTask tempThrottle;

TempThrottleTask::TempThrottleTask() : MicroTasks::Task() {
  _evse             = nullptr;
  _enabled          = false;
  _setpoint         = TEMP_THROTTLE_SETPOINT_DEFAULT;
  _start_current    = 0;
  _throttled_current = 0;
}

TempThrottleTask::~TempThrottleTask() {
  if (_evse) {
    _evse->release(EvseClient_OpenEVSE_TempThrottle);
  }
}

void TempThrottleTask::setup() {}

unsigned long TempThrottleTask::loop(MicroTasks::WakeReason reason) {
  if (!_evse) {
    return TEMP_THROTTLE_LOOP_TIME;
  }
  if (!_enabled) {
    if (_evse->clientHasClaim(EvseClient_OpenEVSE_TempThrottle)) {
      _evse->release(EvseClient_OpenEVSE_TempThrottle);
      _start_current    = 0;
      _throttled_current = 0;
    }
    return TEMP_THROTTLE_LOOP_TIME;
  }

  if (!_evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)) {
    return TEMP_THROTTLE_LOOP_TIME;
  }

  double temp = _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR);

  if (temp >= (double)_setpoint) {
    if (_start_current == 0) {
      // Only engage if actively charging
      if (!_evse->isCharging()) {
        return TEMP_THROTTLE_LOOP_TIME;
      }
      uint32_t pilot = (uint32_t)_evse->getChargeCurrent();
      if (pilot == 0) {
        return TEMP_THROTTLE_LOOP_TIME;
      }
      _start_current    = pilot;
      _throttled_current = pilot;
      DBUGF("TempThrottle: throttle start at %uA, setpoint %u°C, temp %.1f°C",
            _start_current, _setpoint, temp);
    }

    uint32_t floor_current = (uint32_t)_evse->getMinCurrent();
    if (_throttled_current > floor_current) {
      _throttled_current -= 1;
    }

    DBUGF("TempThrottle: temp %.1f°C >= %u°C, throttling to %uA",
          temp, _setpoint, _throttled_current);

    EvseProperties props;
    props.setChargeCurrent(_throttled_current);
    _evse->claim(EvseClient_OpenEVSE_TempThrottle, EvseManager_Priority_Safety, props);

  } else {
    // Temperature below setpoint — recover
    if (_evse->clientHasClaim(EvseClient_OpenEVSE_TempThrottle)) {
      if (_throttled_current < _start_current) {
        _throttled_current += 1;

        DBUGF("TempThrottle: temp %.1f°C < %u°C, recovering to %uA",
              temp, _setpoint, _throttled_current);

        EvseProperties props;
        props.setChargeCurrent(_throttled_current);
        _evse->claim(EvseClient_OpenEVSE_TempThrottle, EvseManager_Priority_Safety, props);
      } else {
        // Fully recovered — release claim entirely
        DBUGF("TempThrottle: fully recovered, releasing claim");
        _evse->release(EvseClient_OpenEVSE_TempThrottle);
        _start_current    = 0;
        _throttled_current = 0;
      }
    }
  }

  return TEMP_THROTTLE_LOOP_TIME;
}

void TempThrottleTask::begin(EvseManager &evse) {
  _evse    = &evse;
  _enabled  = config_temp_throttle_enabled();
  _setpoint = temp_throttle_setpoint;
  MicroTask.startTask(this);
  DBUGF("TempThrottle: started, enabled=%d setpoint=%u°C", _enabled, _setpoint);
}

void TempThrottleTask::notifyConfigChanged(bool enabled, uint32_t setpoint) {
  DBUGF("TempThrottle: config changed, enabled=%d setpoint=%u", enabled, setpoint);
  _enabled  = enabled;
  _setpoint = setpoint;
  // _evse may still be null if a config callback fires before begin() at boot.
  if (!enabled && _evse && _evse->clientHasClaim(EvseClient_OpenEVSE_TempThrottle)) {
    _evse->release(EvseClient_OpenEVSE_TempThrottle);
    _start_current    = 0;
    _throttled_current = 0;
  }
}

bool TempThrottleTask::isThrottling() {
  return _evse ? _evse->clientHasClaim(EvseClient_OpenEVSE_TempThrottle) : false;
}

uint32_t TempThrottleTask::getThrottledCurrent() {
  return _throttled_current;
}
