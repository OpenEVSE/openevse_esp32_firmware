#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_INPUT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>

#include "emonesp.h"
#include "input.h"
#include "app_config.h"
#include "divert.h"
#include "event.h"
#include "net_manager.h"
#include "openevse.h"
#include "espal.h"
#include "emoncms.h"
#include "tesla_client.h"
#include "manual.h"
#include "rfid.h"

#include "LedManagerTask.h"

#include "RapiSender.h"

class InputTask : public MicroTasks::Task
{
  private:
    MicroTasks::EventListener _evseState;
    MicroTasks::EventListener _evseData;

  protected:
    void setup()
    {
      evse.onStateChange(&_evseState);
      evse.onDataReady(&_evseData);
    }

    unsigned long loop(MicroTasks::WakeReason reason)
    {
      DBUG("InputTask woke: ");
      DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
            WakeReason_Event == reason ? "WakeReason_Event" :
            WakeReason_Message == reason ? "WakeReason_Message" :
            WakeReason_Manual == reason ? "WakeReason_Manual" :
            "UNKNOWN");

      if(_evseData.IsTriggered())
      {
        if(!Update.isRunning())
        {
          DynamicJsonDocument data(4096);

          create_rapi_json(data); // create JSON Strings for EmonCMS and MQTT
          event_send(data);
          // Already evented to MQTT etc
          teslaClient.getChargeInfoJson(data);
          emoncms_publish(data);
        }
      }

      if(_evseState.IsTriggered())
      {
        // Send to all clients
        StaticJsonDocument<512> event;
        event["state"] = evse.getEvseState();
        event["flags"] = evse.getFlags();
        event["vehicle"] = evse.isVehicleConnected() ? 1 : 0;
        event["colour"] = evse.getStateColour();
        event["pilot"] = evse.getPilotState();
        event["manual_override"] = manual.isActive() ? 1 : 0; //TODO: remove this
        event["status"] = evse.getState().toString();
        event["elapsed"] = evse.getSessionElapsed();
        event["amp"] = evse.getAmps() * AMPS_SCALE_FACTOR;
        event["voltage"] = evse.getVoltage() * VOLTS_SCALE_FACTOR;

        event_send(event);
      }

      return MicroTask.Infinate;
    }
  public:
    InputTask() :
      MicroTasks::Task(),
      _evseState(this),
      _evseData(this)
    {

    }
} input;

void create_rapi_json(JsonDocument &doc)
{
  if(config_rfid_enabled()) {
    doc["rfid_auth"] = rfid.getAuthenticatedTag();
  }
  doc["amp"] = evse.getAmps() * AMPS_SCALE_FACTOR;
  doc["voltage"] = evse.getVoltage() * VOLTS_SCALE_FACTOR;
  doc["pilot"] = evse.getChargeCurrent();
  doc["wh"] = evse.getTotalEnergy() * TOTAL_ENERGY_SCALE_FACTOR;
  doc["session_energy"] = evse.getSessionEnergy();
  doc["total_energy"] = evse.getTotalEnergy();
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)) {
    doc["temp"] = evse.getTemperature(EVSE_MONITOR_TEMP_MONITOR) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp"] = false;
  }
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_MAX)) {
    doc["temp_max"] = evse.getTemperature(EVSE_MONITOR_TEMP_MAX) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp_max"] = false;
  }
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_EVSE_DS3232)) {
    doc["temp1"] = evse.getTemperature(EVSE_MONITOR_TEMP_EVSE_DS3232) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp1"] = false;
  }
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_EVSE_MCP9808)) {
    doc["temp2"] = evse.getTemperature(EVSE_MONITOR_TEMP_EVSE_MCP9808) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp2"] = false;
  }
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_EVSE_TMP007)) {
    doc["temp3"] = evse.getTemperature(EVSE_MONITOR_TEMP_EVSE_TMP007) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp3"] = false;
  }
#ifdef ENABLE_MCP9808
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_ESP_MCP9808)) {
    doc["temp4"] = evse.getTemperature(EVSE_MONITOR_TEMP_ESP_MCP9808) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp4"] = false;
  }
#endif
  doc["state"] = evse.getEvseState();
  doc["status"] = evse.getState().toString();
  doc["flags"] = evse.getFlags();
  doc["vehicle"] = evse.isVehicleConnected() ? 1 : 0;
  doc["colour"] = evse.getStateColour();
  doc["manual_override"] = manual.isActive() ? 1 : 0;
  doc["freeram"] = ESPAL.getFreeHeap();
  doc["divertmode"] = divertmode;
  doc["srssi"] = WiFi.RSSI();
  // get the current time
  char time[64];
  char offset[8];
  struct timeval local_time;
  gettimeofday(&local_time, NULL);
  struct tm * timeinfo = gmtime(&local_time.tv_sec);
  strftime(time, sizeof(time), "%FT%TZ", timeinfo);
  strftime(offset, sizeof(offset), "%z", timeinfo);
  doc["time"] = time;
  doc["offset"] = offset;
  doc["elapsed"] = evse.getSessionElapsed();
}

void
handleRapiRead()
{
  Profile_Start(handleRapiRead);

  OpenEVSE.getTime([](int ret, time_t evse_time)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      time_t local_time = time(NULL);
      if(evse_time > local_time) {
        struct timeval set_time = { evse_time, 0 };
        settimeofday(&set_time, NULL);
      }
    }
  });

  Profile_End(handleRapiRead, 10);
}

void input_setup()
{
  MicroTask.startTask(input);

  OpenEVSE.onWiFi([](uint8_t wifiMode)
  {
    DBUGVAR(wifiMode);
    switch(wifiMode)
    {
      case OPENEVSE_WIFI_MODE_AP:
      case OPENEVSE_WIFI_MODE_AP_DEFAULT:
        net_wifi_turn_on_ap();
        break;
      case OPENEVSE_WIFI_MODE_CLIENT:
        net_wifi_turn_off_ap();
        break;
    }
  });
}
