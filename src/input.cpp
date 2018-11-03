 #if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_RAPI)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "input.h"
#include "config.h"
#include "divert.h"
#include "mqtt.h"
#include "web_server.h"
#include "wifi.h"
#include "openevse.h"

#include "RapiSender.h"

#define OPENEVSE_WIFI_MODE_AP 0
#define OPENEVSE_WIFI_MODE_CLIENT 1
#define OPENEVSE_WIFI_MODE_AP_DEFAULT 2

const char *e_url = "/input/post.json?node=";

String url = "";
String data = "";

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

unsigned long comm_sent = 0;
unsigned long comm_success = 0;

void
create_rapi_json() {
  url = e_url;
  data = "";
  url += String(emoncms_node) + "&json={";
  data += "amp:" + String(amp) + ",";
  if (volt > 0) {
    data += "volt:" + String(volt) + ",";
  }
  data += "wh:" + String(watthour_total) + ",";
  data += "temp1:" + String(temp1) + ",";
  data += "temp2:" + String(temp2) + ",";
  data += "temp3:" + String(temp3) + ",";
  data += "pilot:" + String(pilot) + ",";
  data += "state:" + String(state) + ",";
  data += "freeram:" + String(ESP.getFreeHeap()) + ",";
  data += "divertmode:" + String(divertmode);
  url += data;
  if (emoncms_server == "data.openevse.com/emoncms") {
    // data.openevse uses device module
    url += "}&devicekey=" + emoncms_apikey;
  } else {
    // emoncms.org does not use device module
    url += "}&apikey=" + emoncms_apikey;
  }

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

  comm_sent++;
  switch(rapi_command)
  {
    case 1:
      if (0 == rapiSender.sendCmd("$GE"))
      {
        if(rapiSender.getTokenCnt() >= 3)
        {
          const char *val = rapiSender.getToken(1);
          pilot = strtol(val, NULL, 10);
          comm_success++;
        }
      }
      break;
    case 2:
      if (0 == rapiSender.sendCmd("$GS"))
      {
        if(rapiSender.getTokenCnt() >= 3)
        {
          const char *val = rapiSender.getToken(1);
          DBUGVAR(val);
          state = strtol(val, NULL, 10);
          DBUGVAR(state);
          val = rapiSender.getToken(2);
          DBUGVAR(val);
          elapsed = strtol(val, NULL, 10);
          DBUGVAR(elapsed);
          comm_success++;

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
      }
      break;
    case 3:
      if (0 == rapiSender.sendCmd("$GG"))
      {
        if(rapiSender.getTokenCnt() >= 3)
        {
          const char *val;
          val = rapiSender.getToken(1);
          amp = strtol(val, NULL, 10);
          val = rapiSender.getToken(2);
          volt = strtol(val, NULL, 10);
          comm_success++;
        }
      }
      break;
    case 4:
      if (0 == rapiSender.sendCmd("$GP"))
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
          comm_success++;
        }
      }
      break;
    case 5:
      if (0 == rapiSender.sendCmd("$GU"))
      {
        if(rapiSender.getTokenCnt() >= 3)
        {
          const char *val;
          val = rapiSender.getToken(1);
          wattsec = strtol(val, NULL, 10);
          val = rapiSender.getToken(2);
          watthour_total = strtol(val, NULL, 10);
          comm_success++;
        }
      }
      break;
    case 6:
      if (0 == rapiSender.sendCmd("$GF")) {
        if(rapiSender.getTokenCnt() >= 4)
        {
          const char *val;
          val = rapiSender.getToken(1);
          gfci_count = strtol(val, NULL, 16);
          val = rapiSender.getToken(2);
          nognd_count = strtol(val, NULL, 16);
          val = rapiSender.getToken(3);
          stuck_count = strtol(val, NULL, 16);
          comm_success++;
        }
      }
      rapi_command = 0;         //Last RAPI command
      break;
  }
  rapi_command++;

  Profile_End(update_rapi_values, 5);
}

void
handleRapiRead() {
  Profile_Start(handleRapiRead);

  comm_sent++;
  if (0 == rapiSender.sendCmd("$GV"))
  {
    if(rapiSender.getTokenCnt() >= 3)
    {
      firmware = rapiSender.getToken(1);
      protocol = rapiSender.getToken(2);
      comm_success++;
    }
  }
  comm_sent++;
  if (0 == rapiSender.sendCmd("$GA"))
  {
    if(rapiSender.getTokenCnt() >= 3)
    {
      const char *val;
      val = rapiSender.getToken(1);
      current_scale = strtol(val, NULL, 10);
      val = rapiSender.getToken(2);
      current_offset = strtol(val, NULL, 10);
      comm_success++;
    }
  }
#ifdef ENABLE_LEGACY_API
  comm_sent++;
  if (0 == rapiSender.sendCmd("$GH"))
  {
    if(rapiSender.getTokenCnt() >= 2)
    {
      const char *val;
      val = rapiSender.getToken(1);
      kwh_limit = strtol(val, NULL, 10);
      comm_success++;
    }
  }
  comm_sent++;
  if (0 == rapiSender.sendCmd("$G3"))
  {
    if(rapiSender.getTokenCnt() >= 2)
    {
      const char *val;
      val = rapiSender.getToken(1);
      time_limit = strtol(val, NULL, 10);
      comm_success++;
    }
  }
#endif
  comm_sent++;
  if (0 == rapiSender.sendCmd("$GE"))
  {
    comm_success++;
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
  comm_sent++;
#ifdef ENABLE_LEGACY_API
  if (0 == rapiSender.sendCmd("$GC"))
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
      comm_success++;
    }
  }
#endif

  Profile_End(handleRapiRead, 10);
}

void on_rapi_event()
{
  if(!strcmp(rapiSender.getToken(0), "$ST")) {
    const char *val = rapiSender.getToken(1);
    DBUGVAR(val);

    // Update our local state
    state = strtol(val, NULL, 16);
    DBUGVAR(state);

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
  } else if(!strcmp(rapiSender.getToken(0), "$WF")) {
    const char *val = rapiSender.getToken(1);
    DBUGVAR(val);

    long wifiMode = strtol(val, NULL, 10);
    DBUGVAR(wifiMode);
    switch(wifiMode)
    {
      case OPENEVSE_WIFI_MODE_AP:
      case OPENEVSE_WIFI_MODE_AP_DEFAULT:
        wifi_turn_on_ap();
        break;
      case OPENEVSE_WIFI_MODE_CLIENT:
        wifi_turn_off_ap();
        break;
    }
  }
}
