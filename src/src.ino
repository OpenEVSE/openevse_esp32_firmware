
/*
 * Copyright (c) 2015-2016 Chris Howell
 *
 * -------------------------------------------------------------------
 *
 * Additional Adaptation of OpenEVSE ESP Wifi
 * by Trystan Lea, Glyn Hudson, OpenEnergyMonitor
 * All adaptation GNU General Public License as below.
 *
 * -------------------------------------------------------------------
 *
 * This file is part of Open EVSE.
 * Open EVSE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * Open EVSE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with Open EVSE; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ESP8266WiFi.h>              // Connect to Wifi
#include <WiFiClientSecure.h>         // Secure https GET request
#include <ESP8266WebServer.h>         // Config portal
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>                   // Save config settings
#include "FS.h"                       // SPIFFS file-system: store web server html, CSS etc.
#include <ArduinoOTA.h>               // local OTA update from Arduino IDE
#include <ESP8266mDNS.h>              // Resolve URL for update server etc.
#include <ESP8266httpUpdate.h>        // remote OTA update from server
#include <ESP8266HTTPUpdateServer.h>  // upload update
#include <DNSServer.h>                // Required for captive portal
#include <PubSubClient.h>             // MQTT https://github.com/knolleary/pubsubclient PlatformIO lib: 89

#include "emonesp.h"
#include "config.h"
#include "wifi.h"
#include "web_server.h"
#include "ohm.h"
#include "input.h"
#include "emoncms.h"
#include "mqtt.h"
#include "divert.h"

unsigned long Timer1; // Timer for events once every 30 seconds
unsigned long Timer2; // Timer for events once every 1 Minute
unsigned long Timer3; // Timer for events once every 5 seconds

// -------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------
void setup() {
  delay(2000);
  Serial.begin(115200);
  pinMode(0, INPUT);

#ifdef DEBUG_SERIAL1
  Serial1.begin(115200);
#endif

  DEBUG.println();
  DEBUG.print("OpenEVSE WiFI ");
  DEBUG.println(ESP.getChipId());
  DEBUG.println("Firmware: "+ currentfirmware);
  
  config_load_settings();
  wifi_setup();
  web_server_setup();
  // delay(5000); //gives OpenEVSE time to finish self test on cold start
  handleRapiRead(); //Read all RAPI values
  //  ota_setup();

} // end setup

// -------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------
void loop(){
  
  web_server_loop();
  wifi_loop();
  
  if (wifi_mode==WIFI_MODE_STA || wifi_mode==WIFI_MODE_AP_AND_STA){
  
  if (mqtt_server !=0) mqtt_loop();
// -------------------------------------------------------------------
// Do these things once every 10s
// -------------------------------------------------------------------
    if ((millis() - Timer3) >= 10000){
      update_rapi_values();
      Timer3 = millis();
    }
// -------------------------------------------------------------------
// Do these things once every Minute
// -------------------------------------------------------------------
    if ((millis() - Timer2) >= 60000){
      ohm_loop();
      divert_current_loop();
      Timer2 = millis();
    }

// -------------------------------------------------------------------
// Do these things once every 30 seconds
// -------------------------------------------------------------------
    if ((millis() - Timer1) >= 30000){
      create_rapi_json(); // create JSON Strings for EmonCMS and MQTT
      if(emoncms_apikey != 0) emoncms_publish(url);
      if(mqtt_server != 0) mqtt_publish(data);
      Timer1 = millis();
    }
  } // end WiFi connected

  
  
} // end loop
