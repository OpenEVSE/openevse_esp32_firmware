#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_MQTT)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "mqtt.h"
#include "app_config.h"
#include "divert.h"
#include "input.h"
#include "espal.h"
#include "net_manager.h"

#include "openevse.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MongooseMqttClient.h>

MongooseMqttClient mqttclient;

static long nextMqttReconnectAttempt = 0;
static unsigned long mqttRestartTime = 0;
static bool connecting = false;

String lastWill = "";

#ifndef MQTT_CONNECT_TIMEOUT
#define MQTT_CONNECT_TIMEOUT (5 * 1000)
#endif // !MQTT_CONNECT_TIMEOUT

// -------------------------------------------------------------------
// MQTT msg Received callback function:
// Function to be called when msg is received on MQTT subscribed topic
// Used to receive RAPI commands via MQTT
// //e.g to set current to 13A: <base-topic>/rapi/$SC 13
// -------------------------------------------------------------------
void mqttmsg_callback(MongooseString topic, MongooseString payload) {

  String topic_string = topic.toString();
  // print received MQTT to debug

  DBUGLN("MQTT received:");
  DBUGLN("Topic: " + topic_string);

  String payload_str = payload.toString();
  DBUGLN("Payload: " + payload_str);

  // If MQTT message is solar PV
  if (topic_string == mqtt_solar){
    solar = payload_str.toInt();
    DBUGF("solar:%dW", solar);
    divert_update_state();
  }
  else if (topic_string == mqtt_grid_ie){
    grid_ie = payload_str.toInt();
    DBUGF("grid:%dW", grid_ie);
    divert_update_state();
  }
  else if (topic_string == mqtt_vrms){
    // TODO: The voltage is no longer a global, need to do something so we don't have 
    //       to read back from the EVSE
    double volts = payload_str.toFloat();
    if (volts >= 60.0 && volts <= 300.0) {
      // voltage = volts;
      DBUGF("voltage:%.1f", volts);
      OpenEVSE.setVoltage(volts, [](int ret) {
        // Only gives better power calculations so not critical if this fails
      });
    } else {
      DBUGF("voltage:%.1f (ignoring, out of range)", volts);
    }
  }
  // If MQTT message to set divert mode is received
  else if (topic_string == mqtt_topic + "/divertmode/set"){
    byte newdivert = payload_str.toInt();
    if ((newdivert==1) || (newdivert==2)){
      divertmode_update(newdivert);
    }
  }
  else
  {
    // If MQTT message is RAPI command
    // Detect if MQTT message is a RAPI command e.g to set 13A <base-topic>/rapi/$SC 13
    // Locate '$' character in the MQTT message to identify RAPI command
    int rapi_character_index = topic_string.indexOf('$');
    DBUGVAR(rapi_character_index);
    if (rapi_character_index > 1) {
      DBUGF("Processing as RAPI");
      // Print RAPI command from mqtt-sub topic e.g $SC
      // ASSUME RAPI COMMANDS ARE ALWAYS PREFIX BY $ AND TWO CHARACTERS LONG)
      String cmd = topic_string.substring(rapi_character_index);
      if (payload.length() > 0)
      {
        // If MQTT msg contains a payload e.g $SC 13. Not all rapi commands have a payload e.g. $GC
        cmd += " "+payload_str;
      }

      rapiSender.sendCmd(cmd, [](int ret)
      {
        if (RAPI_RESPONSE_OK == ret || RAPI_RESPONSE_NK == ret)
        {
          String rapiString = rapiSender.getResponse();
          String mqtt_data = rapiString;
          String mqtt_sub_topic = mqtt_topic + "/rapi/out";
          mqttclient.publish(mqtt_sub_topic, mqtt_data);
        }
      });
    }
  }
} //end call back

// -------------------------------------------------------------------
// MQTT Connect
// -------------------------------------------------------------------
boolean
mqtt_connect()
{
  if(connecting) {
    return false;
  }
  connecting = true;

  mqttclient.onMessage(mqttmsg_callback); //function to be called when mqtt msg is received on subscribed topic
  mqttclient.onError([](int err) {
    DBUGF("MQTT error %d", err);
    connecting = false;
  });

  String mqtt_host = mqtt_server + ":" + String(mqtt_port);

  DBUGF("MQTT Connecting to... %s://%s", MQTT_MQTT == config_mqtt_protocol() ? "mqtt" : "mqtts", mqtt_host.c_str());

  // Build the last will message
  DynamicJsonDocument willDoc(JSON_OBJECT_SIZE(3) + 60);

  willDoc["state"] = "disconnected";
  willDoc["id"] = ESPAL.getLongId();
  willDoc["name"] = esp_hostname;

  lastWill = "";
  serializeJson(willDoc, lastWill);
  DBUGVAR(lastWill);

  if(!config_mqtt_reject_unauthorized()) {
    DEBUG.println("WARNING: Certificate verification disabled");
  }

  mqttclient.setCredentials(mqtt_user, mqtt_pass);
  mqttclient.setLastWillAndTestimment(mqtt_announce_topic, lastWill, true);
  mqttclient.setRejectUnauthorized(config_mqtt_reject_unauthorized());
  connecting = mqttclient.connect((MongooseMqttProtocol)config_mqtt_protocol(), mqtt_host, esp_hostname, []()
  {
    DBUGLN("MQTT connected");

    DynamicJsonDocument doc(JSON_OBJECT_SIZE(5) + 200);

    doc["state"] = "connected";
    doc["id"] = ESPAL.getLongId();
    doc["name"] = esp_hostname;
    doc["mqtt"] = mqtt_topic;
    doc["http"] = "http://"+ipaddress+"/";

    // Once connected, publish an announcement..
    String announce = "";
    serializeJson(doc, announce);
    DBUGVAR(announce);
    mqttclient.publish(mqtt_announce_topic, announce, true);

    // MQTT Topic to subscribe to receive RAPI commands via MQTT
    String mqtt_sub_topic = mqtt_topic + "/rapi/in/#";

    // e.g to set current to 13A: <base-topic>/rapi/in/$SC 13
    mqttclient.subscribe(mqtt_sub_topic);

    // subscribe to solar PV / grid_ie MQTT feeds
    if(config_divert_enabled())
    {
      if (mqtt_solar!="") {
        mqttclient.subscribe(mqtt_solar);
      }
      if (mqtt_grid_ie!="") {
        mqttclient.subscribe(mqtt_grid_ie);
      }
    }
    if (mqtt_vrms!="") {
      mqttclient.subscribe(mqtt_vrms);
    }

    mqtt_sub_topic = mqtt_topic + "/divertmode/set";      // MQTT Topic to change divert mode
    mqttclient.subscribe(mqtt_sub_topic);

    connecting = false;
  });

  return true;
}



// -------------------------------------------------------------------
// Publish status to MQTT
// -------------------------------------------------------------------
void
mqtt_publish(JsonDocument &data) {
  Profile_Start(mqtt_publish);

  if(!config_mqtt_enabled() || !mqttclient.connected()) {
    return;
  }

  JsonObject root = data.as<JsonObject>();
  for (JsonPair kv : root) {
    String topic = mqtt_topic + "/";
    topic += kv.key().c_str();
    String val = kv.value().as<String>();
    mqttclient.publish(topic, val);
    topic = mqtt_topic + "/";
  }

  Profile_End(mqtt_publish, 5);
}

// -------------------------------------------------------------------
// MQTT state management
//
// Call every time around loop() if connected to the WiFi
// -------------------------------------------------------------------
void
mqtt_loop() {
  Profile_Start(mqtt_loop);

  // Do we need to restart MQTT?
  if(mqttRestartTime > 0 && millis() > mqttRestartTime) 
  {
    mqttRestartTime = 0;
    if (mqttclient.connected()) {
      DBUGF("Disconnecting MQTT");
      mqttclient.disconnect();
    }
    nextMqttReconnectAttempt = 0;
  }

  if (config_mqtt_enabled() && !mqttclient.connected()) {
    long now = millis();
    // try and reconnect every x seconds
    if (now > nextMqttReconnectAttempt) {
      nextMqttReconnectAttempt = now + MQTT_CONNECT_TIMEOUT;
      mqtt_connect(); // Attempt to reconnect
    }
  }

  Profile_End(mqtt_loop, 5);
}

void
mqtt_restart() {
  // If connected disconnect MQTT to trigger re-connect with new details
  mqttRestartTime = millis();
}

boolean
mqtt_connected() {
  return mqttclient.connected();
}