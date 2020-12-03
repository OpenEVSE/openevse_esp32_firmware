#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_INPUT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <ArduinoJson.h>

#include "emonesp.h"
#include "input.h"
#include "app_config.h"
#include "divert.h"
#include "event.h"
#include "net_manager.h"
#include "openevse.h"
#include "espal.h"

#include "LedManagerTask.h"

#include "RapiSender.h"

#ifdef ENABLE_MCP9808
#include <Wire.h>
#include <Adafruit_MCP9808.h>
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();

#ifndef I2C_SDA
#define I2C_SDA -1
#endif

#ifndef I2C_SCL
#define I2C_SCL -1
#endif
#endif

int espflash = 0;
int espfree = 0;

int rapi_command = 1;

double amp = 0;                         // OpenEVSE Current Sensor
double voltage = DEFAULT_VOLTAGE;     // Voltage from OpenEVSE or MQTT
double temp1 = 0;                       // Sensor DS3232 Ambient
bool temp1_valid = false;
double temp2 = 0;                       // Sensor MCP9808 Ambiet
bool temp2_valid = false;
double temp3 = 0;                       // Sensor TMP007 Infared
bool temp3_valid = false;
long pilot = 0;                       // OpenEVSE Pilot Setting
long state = OPENEVSE_STATE_STARTING; // OpenEVSE State
long elapsed = 0;                     // Elapsed time (only valid if charging)

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

// Default OpenEVSE Fault Counters
long gfci_count = 0;
long nognd_count = 0;
long stuck_count = 0;

// OpenEVSE Usage Statistics
long wattsec = 0;
long watthour_total = 0;

class InputTask : public MicroTasks::Task
{
  private:
    MicroTasks::EventListener _evseState;
  protected:
    void setup() 
    {
      evse.onStateChange(&_evseState);
    }

    unsigned long loop(MicroTasks::WakeReason reason)
    {
      if(_evseState.IsTriggered())
      {
        state = evse.getEvseState();
        ledManager.setEvseState(state);

        // Send to all clients
        StaticJsonDocument<32> event;
        event["state"] = state;
        event_send(event);
      }

      return MicroTask.Infinate;
    }
  public:
    InputTask() :
      _evseState(this)
    {

    }
} input;

void create_rapi_json(JsonDocument &doc)
{
  doc["amp"] = amp * AMPS_SCALE_FACTOR;
  doc["voltage"] = voltage * VOLTS_SCALE_FACTOR;
  doc["pilot"] = pilot;
  doc["wh"] = watthour_total;
  if(temp1_valid) {
    doc["temp1"] = temp1 * TEMP_SCALE_FACTOR;
  } else {
    doc["temp1"] = false;
  }
  if(temp2_valid) {
    doc["temp2"] = temp2 * TEMP_SCALE_FACTOR;
  } else {
    doc["temp2"] = false;
  }
  if(temp3_valid) {
    doc["temp3"] = temp3 * TEMP_SCALE_FACTOR;
  } else {
    doc["temp3"] = false;
  }
#ifdef ENABLE_MCP9808
  float temp4 = tempsensor.readTempC();
  DBUGVAR(temp4);
  if(!isnan(temp4)) {
    doc["temp4"] = temp4 * TEMP_SCALE_FACTOR;
  } else {
    doc["temp4"] = false;
  }
#endif
  doc["state"] = state;
  doc["freeram"] = ESPAL.getFreeHeap();
  doc["divertmode"] = divertmode;
  doc["srssi"] = WiFi.RSSI();
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
          DBUGF("evse_state = %02x, session_time = %d, pilot_state = %02x, vflags = %08x", evse_state, session_time, pilot_state, vflags);

          state = evse_state;
          ledManager.setEvseState(evse_state);
          elapsed = session_time;
        }
      });
      break;
    case 3:
      OpenEVSE.getChargeCurrentAndVoltage([](int ret, double a, double volts)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          amp = a;
          if(volts >= 0) {
            voltage = volts;
          }
        }
      });
      break;
    case 4:
      OpenEVSE.getTemperature([](int ret, double t1, bool t1_valid, double t2, bool t2_valid, double t3, bool t3_valid)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          temp1 = t1;
          temp1_valid = t1_valid;
          temp2 = t2;
          temp2_valid = t2_valid;
          temp3 = t3;
          temp3_valid = t3_valid;
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
#ifdef ENABLE_MCP9808
  Wire.begin(I2C_SDA, I2C_SCL);

  if(tempsensor.begin())
  {
    DBUGLN("Found MCP9808!");
    // Mode Resolution SampleTime
    //  0    0.5°C       30 ms
    //  1    0.25°C      65 ms
    //  2    0.125°C     130 ms
    //  3    0.0625°C    250 ms
    tempsensor.setResolution(1); // sets the resolution mode of reading, the modes are defined in the table bellow:
    tempsensor.wake();   // wake up, ready to read!
  } else {
    DBUGLN("Couldn't find MCP9808!");
  }
#endif

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
