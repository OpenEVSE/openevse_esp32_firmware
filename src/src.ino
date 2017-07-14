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

#include <Arduino.h>
#include <ArduinoOTA.h>               // local OTA update from Arduino IDE

#include "emonesp.h"
#include "config.h"
#include "wifi.h"
#include "web_server.h"
#include "ohm.h"
#include "input.h"
#include "emoncms.h"
#include "mqtt.h"
#include "divert.h"
#include "ota.h"

unsigned long Timer1; // Timer for events once every 30 seconds
unsigned long Timer2; // Timer for events once every 1 Minute
unsigned long Timer3; // Timer for events once every 2 seconds

boolean rapi_read = 0; //flag to indicate first read of RAPI status
// -------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------
void
setup() {
  delay(2000);
  Serial.begin(115200);
  pinMode(0, INPUT);

  DEBUG_BEGIN(115200);

  DBUGLN();
  DBUG("OpenEVSE WiFI ");
  DBUGLN(ESP.getChipId());
  DBUGLN("Firmware: " + currentfirmware);

  DBUGF("Free: %d", ESP.getFreeHeap());

  // Read saved settings from the config
  config_load_settings();
  DBUGF("After config_load_settings: %d", ESP.getFreeHeap());

  // Initialise the WiFi
  wifi_setup();
  DBUGF("After wifi_setup: %d", ESP.getFreeHeap());

  // Bring up the web server
  web_server_setup();
  DBUGF("After web_server_setup: %d", ESP.getFreeHeap());

#ifdef ENABLE_OTA
  ota_setup();
  DBUGF("After ota_setup: %d", ESP.getFreeHeap());
#endif
} // end setup

// -------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------
void
loop() {
  Profile_Start(loop);

  web_server_loop();
  wifi_loop();
#ifdef ENABLE_OTA
  ota_loop();
#endif

  // Gives OpenEVSE time to finish self test on cold start
  if ( (millis() > 5000) && (rapi_read==0) ) {
    DBUGLN("first read RAPI values");
    handleRapiRead(); //Read all RAPI values
    rapi_read=1;
  }
  // -------------------------------------------------------------------
  // Do these things once every 2s
  // -------------------------------------------------------------------
  if ((millis() - Timer3) >= 2000) {
    DBUGF("Free: %d", ESP.getFreeHeap());
    update_rapi_values();
    Timer3 = millis();
  }

  if (wifi_mode==WIFI_MODE_STA || wifi_mode==WIFI_MODE_AP_AND_STA) {

    if (config_mqtt_enabled()) {
      mqtt_loop();
    }

    // -------------------------------------------------------------------
    // Do these things once every Minute
    // -------------------------------------------------------------------
    if ((millis() - Timer2) >= 60000) {
      DBUGLN("Time2");
      if(config_ohm_enabled()) {
        ohm_loop();
      }
      divert_current_loop();
      Timer2 = millis();
    }
    // -------------------------------------------------------------------
    // Do these things once every 30 seconds
    // -------------------------------------------------------------------
    if ((millis() - Timer1) >= 30000) {
      DBUGLN("Time1");

      create_rapi_json(); // create JSON Strings for EmonCMS and MQTT
      if (config_emoncms_enabled()) {
        emoncms_publish(url);
      }
      if (config_mqtt_enabled()) {
        mqtt_publish(data);
      }
      Timer1 = millis();
    }

  } // end WiFi connected

  Profile_End(loop, 10);
} // end loop
