#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_INPUT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <ArduinoJson.h>

#include "emonesp.h"
#include "input.h"
#include "app_config.h"
#include "divert.h"
#include "mqtt.h"
#include "web_server.h"
#include "net_manager.h"
#include "openevse.h"
#include "hal.h"

#include "RapiSender.h"

int espflash = 0;
int espfree = 0;

int rapi_command = 1;

long amp = 0;                         // OpenEVSE Current Sensor
long volt = 0;                        // Not currently in used
long temp1 = 0;                       // Sensor DS3232 Ambient
long temp2 = 0;                       // Sensor MCP9808 Ambient
long temp3 = 0;                       // Sensor TMP007 Infared
long pilot = 0;                       // OpenEVSE Pilot Setting
long state = OPENEVSE_STATE_STARTING; // OpenEVSE State
long elapsed = 0;                     // Elapsed time (only valid if charging)
#ifdef ENABLE_LEGACY_API
String estate = "Unknown"; // Common name for State
#endif

// Defaults OpenEVSE Settings
byte rgb_lcd = 1;
byte serial_dbg = 0;
byte auto_service = 1;
int service = 1;

#ifdef ENABLE_LEGACY_API
long current_l1min = 0;
long current_l2min = 0;
long current_l1max = 0;
long current_l2max = 0;
#endif

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

// Default OpenEVSE Fault Counters
long gfci_count = 0;
long nognd_count = 0;
long stuck_count = 0;

// OpenEVSE Session options
#ifdef ENABLE_LEGACY_API
long kwh_limit = 0;
long time_limit = 0;
#endif

// OpenEVSE Usage Statistics
long wattsec = 0;
long watthour_total = 0;

void create_rapi_json(String &data)
{
  const size_t capacity = JSON_OBJECT_SIZE(10);
  DynamicJsonDocument doc(capacity);

  doc["amp"] = amp;
  if (volt > 0) {
    doc["volt"] = volt;
  }
  doc["pilot"] = pilot;
  doc["wh"] = watthour_total;
  doc["temp1"] = temp1;
  doc["temp2"] = temp2;
  doc["temp3"] = temp3;
  doc["state"] = state;
  doc["freeram"] = HAL.getFreeHeap();
  doc["divertmode"] = divertmode;

  serializeJson(doc, data);

  //DEBUG.print(emoncms_server.c_str() + String(url));
}

// -------------------------------------------------------------------
// OpenEVSE Request
//
// Get RAPI Values
// Runs from arduino main loop, runs a new command in the loop
// on each call.  Used for values that change at runtime.
// -------------------------------------------------------------------

void
update_rapi_values() {
  Profile_Start(update_rapi_values);

  switch(rapi_command)
  {
    case 1:
      rapiSender.sendCmd("$GE", [](int ret)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          if(rapiSender.getTokenCnt() >= 3)
          {
            const char *val = rapiSender.getToken(1);
            pilot = strtol(val, NULL, 10);
          }
        }
      });
      break;
    case 2:
      OpenEVSE.getStatus([](int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          state = evse_state;
          elapsed = session_time;

#ifdef ENABLE_LEGACY_API
          switch (state) {
            case 1:
              estate = "Not Connected";
              break;
            case 2:
              estate = "EV Connected";
              break;
            case 3:
              estate = "Charging";
              break;
            case 4:
              estate = "Vent Required";
              break;
            case 5:
              estate = "Diode Check Failed";
              break;
            case 6:
              estate = "GFCI Fault";
              break;
            case 7:
              estate = "No Earth Ground";
              break;
            case 8:
              estate = "Stuck Relay";
              break;
            case 9:
              estate = "GFCI Self Test Failed";
              break;
            case 10:
              estate = "Over Temperature";
              break;
            case 254:
              estate = "Sleeping";
              break;
            case 255:
              estate = "Disabled";
              break;
            default:
              estate = "Invalid";
              break;
          }
#endif
        }
      });
      break;
    case 3:
      rapiSender.sendCmd("$GG", [](int ret)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          if(rapiSender.getTokenCnt() >= 3)
          {
            const char *val;
            val = rapiSender.getToken(1);
            amp = strtol(val, NULL, 10);
            val = rapiSender.getToken(2);
            volt = strtol(val, NULL, 10);
          }
        }
      });
      break;
    case 4:
      rapiSender.sendCmd("$GP", [](int ret)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          if(rapiSender.getTokenCnt() >= 4)
          {
            const char *val;
            val = rapiSender.getToken(1);
            temp1 = strtol(val, NULL, 10);
            val = rapiSender.getToken(2);
            temp2 = strtol(val, NULL, 10);
            val = rapiSender.getToken(3);
            temp3 = strtol(val, NULL, 10);
          }
        }
      });
      break;
    case 5:
      rapiSender.sendCmd("$GU", [](int ret)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          if(rapiSender.getTokenCnt() >= 3)
          {
            const char *val;
            val = rapiSender.getToken(1);
            wattsec = strtol(val, NULL, 10);
            val = rapiSender.getToken(2);
            watthour_total = strtol(val, NULL, 10);
          }
        }
      });
      break;
    case 6:
      rapiSender.sendCmd("$GF", [](int ret)
      {
        if(RAPI_RESPONSE_OK == ret) {
          if(rapiSender.getTokenCnt() >= 4)
          {
            const char *val;
            val = rapiSender.getToken(1);
            gfci_count = strtol(val, NULL, 16);
            val = rapiSender.getToken(2);
            nognd_count = strtol(val, NULL, 16);
            val = rapiSender.getToken(3);
            stuck_count = strtol(val, NULL, 16);
          }
        }
      });
      rapi_command = 0;         //Last RAPI command
      break;
  }
  rapi_command++;

  Profile_End(update_rapi_values, 5);
}

void
handleRapiRead()
{
  Profile_Start(handleRapiRead);

  rapiSender.sendCmd("$GV", [](int ret)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      if(rapiSender.getTokenCnt() >= 3)
      {
        firmware = rapiSender.getToken(1);
        protocol = rapiSender.getToken(2);
      }
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

#ifdef ENABLE_LEGACY_API
  rapiSender.sendCmd("$GH", [](int ret)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      if(rapiSender.getTokenCnt() >= 2)
      {
        const char *val;
        val = rapiSender.getToken(1);
        kwh_limit = strtol(val, NULL, 10);
      }
    }
  });

  rapiSender.sendCmd("$G3", [](int ret)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      if(rapiSender.getTokenCnt() >= 2)
      {
        const char *val;
        val = rapiSender.getToken(1);
        time_limit = strtol(val, NULL, 10);
      }
    }
  });
#endif

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

#ifdef ENABLE_LEGACY_API
  rapiSender.sendCmd("$GC", [](int ret)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      if(rapiSender.getTokenCnt() >= 3)
      {
        const char *val;
        if (service == 1) {
          val = rapiSender.getToken(1);
          current_l1min = strtol(val, NULL, 10);
          val = rapiSender.getToken(2);
          current_l1max = strtol(val, NULL, 10);
        } else {
          val = rapiSender.getToken(1);
          current_l2min = strtol(val, NULL, 10);
          val = rapiSender.getToken(2);
          current_l2max = strtol(val, NULL, 10);
        }
      }
    }
  });
#endif

  Profile_End(handleRapiRead, 10);
}

void input_setup()
{
  OpenEVSE.onState([](uint8_t evse_state, uint8_t pilot_state, uint32_t current_capacity, uint32_t vflags)
  {
    // Update our global state
    DBUGVAR(evse_state);
    state = evse_state;

    // Send to all clients
    String event = F("{\"state\":");
    event += state;
    event += F("}");
    web_server_event(event);

    if (config_mqtt_enabled()) {
      event = F("state:");
      event += String(state);
      mqtt_publish(event);
    }
  });

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
