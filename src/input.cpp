#include "emonesp.h"
#include "input.h"
#include "config.h"

const char *e_url = "/input/post.json?node=";

String url = "";
String data = "";

int espflash = 0;
int espfree = 0;

int commDelay = 60;
int rapi_command = 1;
int rapi_command_sent = 0;
int comm_Delay = 1000; //Delay between each command and read or write
unsigned long comm_Timer = 0; //Timer for Comm requests

int amp = 0; //OpenEVSE Current Sensor
int volt = 0; //Not currently in used
int temp1 = 0; //Sensor DS3232 Ambient
int temp2 = 0; //Sensor MCP9808 Ambient
int temp3 = 0; //Sensor TMP007 Infared
int pilot = 0; //OpenEVSE Pilot Setting
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
  data += "amp:" + String(amp) + ",";
  data += "temp1:" + String(temp1) + ",";
  data += "temp2:" + String(temp2) + ",";
  data += "temp3:" + String(temp3) + ",";
  data += "pilot:" + String(pilot) + ",";
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
  if ((millis() - comm_Timer) >= comm_Delay) {
    if (rapi_command_sent == 0) {
       Serial.flush();
     }
     if (rapi_command_sent == 0 && rapi_command == 1){
       Serial.println("$GE*B0");
     }
    if (rapi_command_sent == 1 && rapi_command == 1) {
      while (Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK ")) {
           comm_success++;
           String qrapi;
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           pilot = qrapi.toInt();
         }
       }
     }
    if (rapi_command_sent == 0 && rapi_command == 2) {
       Serial.println("$GS*BE");
       }
    if (rapi_command_sent == 1 && rapi_command == 2) {
      while (Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK ")) {
           comm_success++;
           String qrapi = rapiString.substring(rapiString.indexOf(' '));
           state = strtol(qrapi.c_str(), NULL, 16);
           if (state == 1) {
             estate = "Not_Connected";
           }
           if (state == 2) {
             estate = "EV_Connected";
           }
           if (state == 3) {
             estate = "Charging";
           }
           if (state == 4) {
             estate = "Vent_Required";
           }
           if (state == 5) {
             estate = "Diode_Check_Failed";
           }
           if (state == 6) {
             estate = "GFCI_Fault";
           }
           if (state == 7) {
             estate = "No_Earth_Ground";
           }
           if (state == 8) {
             estate = "Stuck_Relay";
           }
           if (state == 9) {
             estate = "GFCI_Self_Test_Failed";
           }
           if (state == 10) {
             estate = "Over_Temperature";
           }
           if (state == 254) {
             estate = "Sleeping";
           }
           if (state == 255) {
             estate = "Disabled";
           }
         }
       }
     }
    if (rapi_command_sent == 0 && rapi_command == 3) {
       Serial.println("$GG*B2");
      }
    if (rapi_command_sent == 1 && rapi_command == 3) {
      while (Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK")) {
           comm_success++;
           String qrapi;
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           amp = qrapi.toInt();
           String qrapi1;
           qrapi1 = rapiString.substring(rapiString.lastIndexOf(' '));
           volt = qrapi1.toInt();
         }
       }
     }
    if (rapi_command_sent == 0 && rapi_command == 4) {
       Serial.println("$GP*BB");
     }
    if (rapi_command_sent == 1 && rapi_command == 4) {
      while (Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK")) {
          comm_success++;
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          temp1 = qrapi.toInt();
          String qrapi1;
          int firstRapiCmd = rapiString.indexOf(' ');
          qrapi1 = rapiString.substring(rapiString.indexOf(' ', firstRapiCmd + 1));
          temp2 = qrapi1.toInt();
          String qrapi2;
          qrapi2 = rapiString.substring(rapiString.lastIndexOf(' '));
          temp3 = qrapi2.toInt();
         }
      }
    }
    if (rapi_command_sent == 0 && rapi_command == 5) {
      Serial.println("$GU*C0");
    }
    if (rapi_command_sent == 1 && rapi_command == 5) {
      while (Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK")) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
          int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd + 1);
          wattsec = rapiString.substring(firstRapiCmd, secondRapiCmd);
          watthour_total = rapiString.substring(secondRapiCmd);
      }
    }
    }
    if (rapi_command_sent == 0 && rapi_command == 6) {
  Serial.println("$GF*B1");
    }
    if (rapi_command_sent == 1 && rapi_command == 6) {
      while (Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK")) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
          int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd + 1);
          int thirdRapiCmd = rapiString.indexOf(' ', secondRapiCmd + 1);
        gfci_count = rapiString.substring(firstRapiCmd, secondRapiCmd);
        nognd_count = rapiString.substring(secondRapiCmd, thirdRapiCmd);
        stuck_count = rapiString.substring(thirdRapiCmd);
      }
    }
      rapi_command = 0;         //Last RAPI command
    }
    if (rapi_command_sent == 0) {
      rapi_command_sent = 1;
  comm_sent++;
    } else {
      rapi_command++;
      rapi_command_sent = 0;
    }
    comm_Timer = millis();
  }
}

void
handleRapiRead() {
  Serial.flush();
  Serial.println("$GV*C1");
  comm_sent++;
  delay(commDelay);
  while (Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK")) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
      int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd + 1);
      firmware = rapiString.substring(firstRapiCmd, secondRapiCmd);
      protocol = rapiString.substring(secondRapiCmd);
        }
        }
  Serial.println("$GA*AC");
  comm_sent++;
  delay(commDelay);
  while (Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK")) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
      int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd + 1);
        current_scale = rapiString.substring(firstRapiCmd, secondRapiCmd);
        current_offset = rapiString.substring(secondRapiCmd);
      }
    }
  Serial.println("$GH");
  comm_sent++;
  delay(commDelay);
  while (Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK")) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        kwh_limit = rapiString.substring(firstRapiCmd);
      }
    }
  Serial.println("$G3");
  comm_sent++;
  delay(commDelay);
  while (Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK")) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        time_limit = rapiString.substring(firstRapiCmd);
      }
    }
    Serial.println("$GE*B0");
     comm_sent++;
     delay(commDelay);
  while (Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK ")) {
           comm_success++;
           String qrapi;
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           pilot = qrapi.toInt();
           String flag = rapiString.substring(rapiString.lastIndexOf(' '));
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
       }
  Serial.println("$GC*AE");
  comm_sent++;
  delay(commDelay);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK")) {
      comm_success++;
      int firstRapiCmd = rapiString.indexOf(' ');
      int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd + 1);
      if (service == 1) {
        current_l1min = rapiString.substring(firstRapiCmd, secondRapiCmd);
        current_l1max = rapiString.substring(secondRapiCmd);
      } else {
        current_l2min = rapiString.substring(firstRapiCmd, secondRapiCmd);
        current_l2max = rapiString.substring(secondRapiCmd);
     }
    }
  }
}
