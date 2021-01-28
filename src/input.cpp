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

#include "LedManagerTask.h"

#include "RapiSender.h"

int espflash = 0;
int espfree = 0;

long pilot = 0;                       // OpenEVSE Pilot Setting

// Defaults OpenEVSE Settings
byte rgb_lcd = 1;
byte serial_dbg = 0;
byte auto_service = 1;
int service = 1;

long current_scale = 0;
long current_offset = 0;

// Default OpenEVSE Safety Configuration
byte diode_ck = 1;
byte gfci_test = 1;
byte ground_ck = 1;
byte stuck_relay = 1;
byte vent_ck = 1;
byte temp_ck = 1;
byte auto_start = 1;
String firmware = "-";
String protocol = "-";

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
          emoncms_publish(data);

          teslaClient.getChargeInfoJson(data);
          event_send(data);
        }
      }

      if(_evseState.IsTriggered())
      {
        uint8_t state = evse.getEvseState();
        ledManager.setEvseState(state);

        // Send to all clients
        StaticJsonDocument<32> event;
        event["state"] = state;
        event["vehicle"] = evse.isVehicleConnected();
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
  doc["amp"] = evse.getAmps() * AMPS_SCALE_FACTOR;
  doc["voltage"] = evse.getVoltage() * VOLTS_SCALE_FACTOR;
  doc["pilot"] = pilot;
  doc["wh"] = evse.getTotalEnergy() * TOTAL_ENERGY_SCALE_FACTOR;
  if(evse.isTempuratureValid(EVSE_MONITOR_TEMP_MONITOR)) {
    doc["temp"] = evse.getTempurature(EVSE_MONITOR_TEMP_MONITOR) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp"] = false;
  }
  if(evse.isTempuratureValid(EVSE_MONITOR_TEMP_EVSE_DS3232)) {
    doc["temp1"] = evse.getTempurature(EVSE_MONITOR_TEMP_EVSE_DS3232) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp1"] = false;
  }
  if(evse.isTempuratureValid(EVSE_MONITOR_TEMP_EVSE_MCP9808)) {
    doc["temp2"] = evse.getTempurature(EVSE_MONITOR_TEMP_EVSE_MCP9808) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp2"] = false;
  }
  if(evse.isTempuratureValid(EVSE_MONITOR_TEMP_EVSE_TMP007)) {
    doc["temp3"] = evse.getTempurature(EVSE_MONITOR_TEMP_EVSE_TMP007) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp3"] = false;
  }
#ifdef ENABLE_MCP9808
  if(evse.isTempuratureValid(EVSE_MONITOR_TEMP_ESP_MCP9808)) {
    doc["temp4"] = evse.getTempurature(EVSE_MONITOR_TEMP_ESP_MCP9808) * TEMP_SCALE_FACTOR;
  } else {
    doc["temp4"] = false;
  }
#endif
  doc["state"] = evse.getEvseState();
  doc["vehicle"] = evse.isVehicleConnected();
  doc["freeram"] = ESPAL.getFreeHeap();
  doc["divertmode"] = divertmode;
  doc["srssi"] = WiFi.RSSI();
}

void
handleRapiRead()
{
  Profile_Start(handleRapiRead);

  OpenEVSE.getVersion([](int ret, const char *returned_firmware, const char *returned_protocol) {
    if(RAPI_RESPONSE_OK == ret)
    {
      firmware = returned_firmware;
      protocol = returned_protocol;
    }
  });

  OpenEVSE.getTime([](int ret, time_t evse_time)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      struct timeval set_time = { evse_time, 0 };
      settimeofday(&set_time, NULL);
    }
  });

  rapiSender.sendCmd("$GA", [](int ret)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      if(rapiSender.getTokenCnt() >= 3)
      {
        const char *val;
        val = rapiSender.getToken(1);
        current_scale = strtol(val, NULL, 10);
        val = rapiSender.getToken(2);
        current_offset = strtol(val, NULL, 10);
      }
    }
  });

  rapiSender.sendCmd("$GE", [](int ret)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      const char *val;
      val = rapiSender.getToken(1);
      DBUGVAR(val);
      pilot = strtol(val, NULL, 10);

      val = rapiSender.getToken(2);
      DBUGVAR(val);
      long flags = strtol(val, NULL, 16);
      service = bitRead(flags, 0) + 1;
      diode_ck = bitRead(flags, 1);
      vent_ck = bitRead(flags, 2);
      ground_ck = bitRead(flags, 3);
      stuck_relay = bitRead(flags, 4);
      auto_service = bitRead(flags, 5);
      auto_start = bitRead(flags, 6);
      serial_dbg = bitRead(flags, 7);
      rgb_lcd = bitRead(flags, 8);
      gfci_test = bitRead(flags, 9);
      temp_ck = bitRead(flags, 10);
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
