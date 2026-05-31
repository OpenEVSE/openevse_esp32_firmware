#if defined(ENABLE_SCREEN_LVGL)

#include <WiFi.h>
#include "evse_man.h"
#include "evse_monitor.h"
#include "openevse.h"
#include "evse_ui_model.h"

EvseUiModel::EvseUiModel(EvseManager &evse) : _evse(evse) {}

bool EvseUiModel::evseConnected()     { return _evse.isConnected(); }
uint8_t EvseUiModel::evseState()      { return _evse.getEvseState(); }
bool EvseUiModel::vehicleConnected()  { return _evse.isVehicleConnected(); }
bool EvseUiModel::charging()          { return _evse.isCharging(); }
bool EvseUiModel::active()            { return _evse.isActive(); }
bool EvseUiModel::error()             { return _evse.isError(); }
double EvseUiModel::voltage()         { return _evse.getVoltage(); }
double EvseUiModel::amps()            { return _evse.getAmps(); }
double EvseUiModel::power()           { return _evse.getPower(); }
uint32_t EvseUiModel::pilotCurrent()  { return _evse.getChargeCurrent(); }
uint32_t EvseUiModel::sessionElapsed(){ return _evse.getSessionElapsed(); }
double EvseUiModel::sessionEnergy()   { return _evse.getSessionEnergy(); }
bool EvseUiModel::tempValid()         { return _evse.isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR); }
double EvseUiModel::temperatureC()    { return _evse.getTemperature(EVSE_MONITOR_TEMP_MONITOR); }

bool EvseUiModel::wifiConnected()     { return WiFi.isConnected(); }
bool EvseUiModel::wifiApMode()        { return (WiFi.getMode() & WIFI_MODE_AP) != 0; }
int EvseUiModel::wifiRssi()           { return WiFi.RSSI(); }

const char *EvseUiModel::stateText()
{
  switch (_evse.getEvseState()) {
    case OPENEVSE_STATE_STARTING:           return "Starting";
    case OPENEVSE_STATE_NOT_CONNECTED:      return "Ready";
    case OPENEVSE_STATE_CONNECTED:          return "Connected";
    case OPENEVSE_STATE_CHARGING:           return "Charging";
    case OPENEVSE_STATE_VENT_REQUIRED:      return "Vent required";
    case OPENEVSE_STATE_DIODE_CHECK_FAILED: return "Diode check failed";
    case OPENEVSE_STATE_GFI_FAULT:          return "GFCI fault";
    case OPENEVSE_STATE_NO_EARTH_GROUND:    return "No ground";
    case OPENEVSE_STATE_STUCK_RELAY:        return "Stuck relay";
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED: return "GFCI self-test failed";
    case OPENEVSE_STATE_OVER_TEMPERATURE:   return "Over temperature";
    case OPENEVSE_STATE_OVER_CURRENT:       return "Over current";
    case OPENEVSE_STATE_SLEEPING:           return "Sleeping";
    case OPENEVSE_STATE_DISABLED:           return "Disabled";
    default:                                return "Unknown";
  }
}

#endif // ENABLE_SCREEN_LVGL
