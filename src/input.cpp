#include "emonesp.h"
#include "input.h"
#include "config.h"

#include "RapiSender.h"

const char *e_url = "/input/post.json?node=";

String url = "";
String data = "";

int espflash = 0;
int espfree = 0;

int rapi_command = 1;

String amp = "0";                    //OpenEVSE Current Sensor
String volt = "0";                   //Not currently in used
String temp1 = "0";                  //Sensor DS3232 Ambient
String temp2 = "0";                  //Sensor MCP9808 Ambient
String temp3 = "0";                  //Sensor TMP007 Infared
String pilot = "0";                  //OpenEVSE Pilot Setting
long state = 0; //OpenEVSE State
String estate = "Unknown"; // Common name for State

//Defaults OpenEVSE Settings
byte rgb_lcd = 1;
byte serial_dbg = 0;
byte auto_service = 1;
int service = 1;
int current_l1 = 0;
int current_l2 = 0;
String current_l1min = "-";
String current_l2min = "-";
String current_l1max = "-";
String current_l2max = "-";
String current_scale = "-";
String current_offset = "-";
//Default OpenEVSE Safety Configuration
byte diode_ck = 1;
byte gfci_test = 1;
byte ground_ck = 1;
byte stuck_relay = 1;
byte vent_ck = 1;
byte temp_ck = 1;
byte auto_start = 1;
String firmware = "-";
String protocol = "-";

//Default OpenEVSE Fault Counters
String gfci_count = "-";
String nognd_count = "-";
String stuck_count = "-";
//OpenEVSE Session options
String kwh_limit = "0";
String time_limit = "0";

//OpenEVSE Usage Statistics
String wattsec = "0";
String watthour_total = "0";

unsigned long comm_sent = 0;
unsigned long comm_success = 0;


void
create_rapi_json() {
  url = e_url;
  data = "";
  url += String(emoncms_node) + "&json={";
  data += "amp:" + amp + ",";
  data += "temp1:" + temp1 + ",";
  data += "temp2:" + temp2 + ",";
  data += "temp3:" + temp3 + ",";
  data += "pilot:" + pilot + ",";
  data += "state:" + String(state);
  url += data;
  if (emoncms_server == "data.openevse.com/emoncms") {
    // data.openevse uses device module
    url += "}&devicekey=" + String(emoncms_apikey);
  } else {
    // emoncms.org does not use device module
    url += "}&apikey=" + String(emoncms_apikey);
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
  if (rapi_command == 1) {
    if (0 == rapiSender.sendCmd("$GE"))
      comm_success++;
      pilot = rapiSender.getToken(1);
  }
  if (rapi_command == 2) {
    if (0 == rapiSender.sendCmd("$GS")) {
      comm_success++;
      String qrapi = rapiSender.getToken(1);
      state = strtol(qrapi.c_str(), NULL, 16);
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
    }
  }
  if (rapi_command == 3) {
    if (0 == rapiSender.sendCmd("$GG")) {
      comm_success++;
      amp = rapiSender.getToken(1);
      volt = rapiSender.getToken(2);
    }
  }
  if (rapi_command == 4) {
    if (0 == rapiSender.sendCmd("$GP")) {
      comm_success++;
      temp1 = rapiSender.getToken(1);
      temp2 = rapiSender.getToken(2);
      temp3 = rapiSender.getToken(3);
    }
  }
  if (rapi_command == 5) {
    if (0 == rapiSender.sendCmd("$GU")) {
      comm_success++;
      wattsec = rapiSender.getToken(1);
      watthour_total = rapiSender.getToken(2);
    }
  }
  if (rapi_command == 6) {
    if (0 == rapiSender.sendCmd("$GF")) {
      comm_success++;
      gfci_count = rapiSender.getToken(1);
      nognd_count = rapiSender.getToken(2);
      stuck_count = rapiSender.getToken(3);
    }
    rapi_command = 0;         //Last RAPI command
  }
  rapi_command++;

  Profile_End(update_rapi_values, 5);
}

void
handleRapiRead() {
  Profile_Start(handleRapiRead);
  comm_sent++;
  if (0 == rapiSender.sendCmd("$GV")) {
    comm_success++;
    firmware = rapiSender.getToken(1);
    protocol = rapiSender.getToken(2);
  }
  comm_sent++;
  if (0 == rapiSender.sendCmd("$GA")) {
    comm_success++;
    current_scale = rapiSender.getToken(1);
    current_offset = rapiSender.getToken(2);
  }
  comm_sent++;
  if (0 == rapiSender.sendCmd("$GH")) {
    comm_success++;
    kwh_limit = rapiSender.getToken(1);
  }
  comm_sent++;
  if (0 == rapiSender.sendCmd("$G3")) {
    comm_success++;
    time_limit = rapiSender.getToken(1);
  }
  comm_sent++;
  if (0 == rapiSender.sendCmd("$GE")) {
    comm_success++;
    pilot = rapiSender.getToken(1);
    String flag = rapiSender.getToken(2);

    long flags = strtol(flag.c_str(), NULL, 16);
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
  if (0 == rapiSender.sendCmd("$GC")) {
    comm_success++;
    if (service == 1) {
      current_l1min = rapiSender.getToken(1);
      current_l1max = rapiSender.getToken(2);
    } else {
      current_l2min = rapiSender.getToken(1);
      current_l2max = rapiSender.getToken(2);
    }
  }
  Profile_End(handleRapiRead, 10);
}
