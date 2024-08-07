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
#include <LittleFS.h>

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
#include "limit.h"

#if defined(ENABLE_PN532)
#include "pn532.h"
#endif

#include "LedManagerTask.h"
#include "event_log.h"
#include "evse_man.h"
#include "scheduler.h"

#include "legacy_support.h"
#include "certificates.h"

EventLog eventLog;
CertificateStore certs;

EvseManager evse(RAPI_PORT, eventLog);
Scheduler scheduler(evse);
ManualOverride manual(evse);
DivertTask divert(evse);

NetManagerTask net(lcd, ledManager, timeManager);

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

OcppTask ocpp = OcppTask();


static void hardware_setup();
static void handle_serial();

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
  DEBUG.printf("Git Hash: " ESCAPEQUOTE(BUILD_HASH) "\n");
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
  DBUGF("After eventLog.begin: %d", ESPAL.getFreeHeap());

  timeManager.begin();
  DBUGF("After timeManager.begin: %d", ESPAL.getFreeHeap());

  evse.begin();
  DBUGF("After evse.begin: %d", ESPAL.getFreeHeap());

  scheduler.begin();
  DBUGF("After scheduler.begin: %d", ESPAL.getFreeHeap());

  divert.begin();
  DBUGF("After divert.begin: %d", ESPAL.getFreeHeap());

  limit.begin(evse);
  DBUGF("After limit.begin: %d", ESPAL.getFreeHeap());

  lcd.begin(evse, scheduler, manual);
  DBUGF("After lcd.begin: %d", ESPAL.getFreeHeap());

#if defined(ENABLE_PN532)
  pn532.begin();
  rfid.begin(evse, pn532);
#else
  rfid.begin(evse, rfidNullDevice);
#endif
  DBUGF("After rfid.begin: %d", ESPAL.getFreeHeap());

  ledManager.begin(evse);
  DBUGF("After ledManager.begin: %d", ESPAL.getFreeHeap());

  // Initialise the WiFi
  net.begin();
  certs.begin();
  DBUGF("After net_setup: %d", ESPAL.getFreeHeap());

  // Initialise Mongoose networking library
  Mongoose.begin();
  Mongoose.setRootCaCallback([]() -> const char *{
    return certs.getRootCa();
  });
  DBUGF("After Mongoose.begin: %d", ESPAL.getFreeHeap());

  // Bring up the web server
  web_server_setup();
  DBUGF("After web_server_setup: %d", ESPAL.getFreeHeap());

#ifdef ENABLE_OTA
  ota_setup();
  DBUGF("After ota_setup: %d", ESPAL.getFreeHeap());
#endif

  input_setup();

  ocpp.begin(evse, lcd, eventLog, rfid);
  DBUGF("After ocpp.begin: %d", ESPAL.getFreeHeap());

  shaper.begin(evse);
  DBUGF("After shaper.begin: %d", ESPAL.getFreeHeap());

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

  uptimeMillis();

  Profile_Start(Mongoose);
  Mongoose.poll(0);
  Profile_End(Mongoose, 10);

  web_server_loop();
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
    }
  }

  if(net.isConnected())
  {
    if (vehicle_data_src == VEHICLE_DATA_SRC_TESLA) {
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
      const size_t capacity = JSON_OBJECT_SIZE(33) + 1024;
      DynamicJsonDocument data(capacity);
      create_rapi_json(data);
      emoncms_publish(data);
      emoncms_updated = false;
    }
  } // end WiFi connected

  if(DEBUG_PORT.available()) {
    handle_serial();
  }

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
      evse.saveEnergyMeter();
      net.wifiStop();
      ESPAL.reset();
    }
} systemRestartAlarm;

void restart_system()
{
  systemRestartAlarm.Set(1000, false);
}

void handle_serial()
{
  String line = DEBUG_PORT.readStringUntil('\n');
  int command_separator = line.indexOf(':');
  if(command_separator > 0)
  {
    String command = line.substring(0, command_separator);
    command.trim();
    String json = line.substring(command_separator + 1);
    json.trim();

    DBUGVAR(command);
    DBUGVAR(json);

    const size_t capacity = JSON_OBJECT_SIZE(50) + 1024;
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, json);
    if(error) {
      DEBUG_PORT.println("{\"code\":400,\"msg\":\"Could not parse JSON\"}");
      return;
    }

    if(command == "factory" || command == "config")
    {
      if(command.equals("factory") && config_factory_write_lock()) {
        DEBUG_PORT.println("{\"code\":423,\"msg\":\"Factory settings locked\"}");
        return;
      }

      bool config_modified = false;
      if(config_deserialize(doc)) {
        config_commit(command == "factory");
        config_modified = true;
        DBUGLN("Config updated");
      }
      DEBUG_PORT.printf("{\"code\":200,\"msg\":\"%s\"}\n", config_modified ? "done" : "no change");
    }
  }
}

// inspired from https://www.snad.cz/en/2018/12/21/uptime-and-esp8266/
uint64_t uptimeMillis()
{
    static uint32_t low32, high32;
    uint32_t new_low32 = millis();
    if (new_low32 < low32) high32++;
    low32 = new_low32;
    return (uint64_t) high32 << 32 | low32;
}
