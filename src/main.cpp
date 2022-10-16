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
#include <WiFiUdp.h>
#include <ArduinoOTA.h>               // local OTA update from Arduino IDE
#include <MongooseCore.h>
#include <MicroTasks.h>
#include <LITTLEFS.h>

#include "emonesp.h"
#include "app_config.h"
#include "net_manager.h"
#include "web_server.h"
#include "ohm.h"
#include "input.h"
#include "emoncms.h"
#include "mqtt.h"
#include "divert.h"
#include "ota.h"
#include "lcd.h"
#include "openevse.h"
#include "root_ca.h"
#include "espal.h"
#include "time_man.h"
#include "tesla_client.h"
#include "event.h"
#include "ocpp.h"
#include "rfid.h"
#include "current_shaper.h"

#if defined(ENABLE_PN532)
#include "pn532.h"
#endif

#include "LedManagerTask.h"
#include "event_log.h"
#include "evse_man.h"
#include "scheduler.h"

#include "legacy_support.h"

EventLog eventLog;
EvseManager evse(RAPI_PORT, eventLog);
Scheduler scheduler(evse);
ManualOverride manual(evse);
DivertTask divert(evse);

RapiSender &rapiSender = evse.getSender();

unsigned long Timer1; // Timer for events once every 30 seconds
unsigned long Timer3; // Timer for events once every 2 seconds

boolean rapi_read = 0; //flag to indicate first read of RAPI status

static uint32_t start_mem = 0;
static uint32_t last_mem = 0;

// Get running firmware version from build tag environment variable
#define TEXTIFY(A) #A
#define ESCAPEQUOTE(A) TEXTIFY(A)
String currentfirmware = ESCAPEQUOTE(BUILD_TAG);
String buildenv = ESCAPEQUOTE(BUILD_ENV_NAME);
String serial;

ArduinoOcppTask ocpp = ArduinoOcppTask();


static void hardware_setup();

// -------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------
void setup()
{
  hardware_setup();
  ESPAL.begin();

  DEBUG.println();
  DEBUG.printf("OpenEVSE WiFI %s\n", ESPAL.getShortId().c_str());
  DEBUG.printf("Firmware: %s\n", currentfirmware.c_str());
  DEBUG.printf("Build date: " __DATE__ " " __TIME__ "\n");
  DEBUG.printf("IDF version: %s\n", ESP.getSdkVersion());
  DEBUG.printf("Free: %d\n", ESPAL.getFreeHeap());

  serial = ESPAL.getLongId();
  serial.toUpperCase();

  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    DEBUG.println("LittleFS Mount Failed");
    return;
  }

  // Read saved settings from the config
  config_load_settings();

  DBUGF("After config_load_settings: %d", ESPAL.getFreeHeap());

  eventLog.begin();
  timeManager.begin();
  evse.begin();
  scheduler.begin();
  divert.begin();

  lcd.begin(evse, scheduler, manual);
#if defined(ENABLE_PN532)
  pn532.begin();
  rfid.begin(evse, pn532);
#else
  rfid.begin(evse, rfidNullDevice);
#endif
  ledManager.begin(evse);

  // Initialise the WiFi
  net_setup();
  DBUGF("After net_setup: %d", ESPAL.getFreeHeap());

  // Initialise Mongoose networking library
  Mongoose.begin();
  Mongoose.setRootCa(root_ca);

  // Bring up the web server
  web_server_setup();
  DBUGF("After web_server_setup: %d", ESPAL.getFreeHeap());

#ifdef ENABLE_OTA
  ota_setup();
  DBUGF("After ota_setup: %d", ESPAL.getFreeHeap());
#endif

  input_setup();

  ocpp.begin(evse, lcd, eventLog, rfid);

  shaper.begin(evse);

  lcd.display(F("OpenEVSE WiFI"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd.display(currentfirmware, 0, 1, 5 * 1000, LCD_CLEAR_LINE);

  start_mem = last_mem = ESPAL.getFreeHeap();
} // end setup

// -------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------
void
loop() {
  Profile_Start(loop);

  Profile_Start(Mongoose);
  Mongoose.poll(0);
  Profile_End(Mongoose, 10);

  web_server_loop();
  net_loop();
  ota_loop();
  rapiSender.loop();

  Profile_Start(MicroTask);
  MicroTask.update();
  Profile_End(MicroTask, 10);

  if(OpenEVSE.isConnected())
  {
    if(OPENEVSE_STATE_STARTING != evse.getEvseState())
    {
      // Read initial state from OpenEVSE
      if (rapi_read == 0)
      {
        DBUGLN("first read RAPI values");
        handleRapiRead(); //Read all RAPI values
        rapi_read=1;

        import_timers(&scheduler);
      }

      // -------------------------------------------------------------------
      // Do these things once every 2s
      // -------------------------------------------------------------------
#ifdef ENABLE_DEBUG_MEMORY_MONITOR
      if ((millis() - Timer3) >= 2000) {
        uint32_t current = ESPAL.getFreeHeap();
        int32_t diff = (int32_t)(last_mem - current);
        if(diff != 0) {
          DEBUG.printf("%s: Free memory %u - diff %d %d\n", time_format_time(time(NULL)).c_str(), current, diff, start_mem - current);
          last_mem = current;
        }
        Timer3 = millis();
      }
#endif
    }
  }

  if(net_is_connected())
  {
    if (config_tesla_enabled()) {
      teslaClient.loop();
    }

    mqtt_loop();

    // -------------------------------------------------------------------
    // Do these things once every 30 seconds
    // -------------------------------------------------------------------
    if ((millis() - Timer1) >= 30000)
    {
      if(!Update.isRunning())
      {
        if(config_ohm_enabled()) {
          ohm_loop();
        }
      }

      Timer1 = millis();
    }

    if(emoncms_updated)
    {
      // Send the current state to check the config
      DynamicJsonDocument data(4096);
      create_rapi_json(data);
      emoncms_publish(data);
      emoncms_updated = false;
    }
  } // end WiFi connected

  Profile_End(loop, 10);
} // end loop


void event_send(String &json)
{
  StaticJsonDocument<512> event;
  deserializeJson(event, json);
  event_send(event);
}

void event_send(JsonDocument &event)
{
  #ifdef ENABLE_DEBUG
  serializeJson(event, DEBUG_PORT);
  DBUGLN("");
  #endif
  web_server_event(event);
  yield();
  mqtt_publish(event);
  yield();
}

void hardware_setup()
{
  debug_setup();
  enableLoopWDT();
}

class SystemRestart : public MicroTasks::Alarm
{
  public:
    void Trigger()
    {
      DBUGLN("Restarting...");
      net_wifi_disconnect();
      ESPAL.reset();
    }
} systemRestartAlarm;

void restart_system()
{
  systemRestartAlarm.Set(1000, false);
}
