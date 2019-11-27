#include "emonesp.h"
#include "mqtt.h"
#include "app_config.h"
#include "divert.h"
#include "input.h"
#include "hal.h"
#include "net_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MongooseMqttClient.h>

MongooseMqttClient mqttclient;

long lastMqttReconnectAttempt = 0;
int clientTimeout = 0;
int i = 0;
String payload_str = "";
bool connecting = false;

String lastWill = "";

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

  payload_str = payload.toString();
  DBUGLN("Payload: " + payload_str);

  // If MQTT message is solar PV
  if (topic_string == mqtt_solar){
    solar = payload_str.toInt();
    DBUGF("solar:%dW", solar);
    divert_update_state();
  }
  else if (topic_string == mqtt_grid_ie){
    grid_ie = payload_str.toInt();
    DBUGF("grid:%dW", solar);
    divert_update_state();
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
  mqttclient.onError([](uint8_t err) {
    DBUGF("MQTT error %u", err);
    connecting = false;
  });

  DBUG("MQTT Connecting to...");
  DBUGLN(mqtt_server);

  // Build the last will message
  DynamicJsonDocument willDoc(JSON_OBJECT_SIZE(3) + 60);

  willDoc["state"] = "disconnected";
  willDoc["id"] = HAL.getLongId();
  willDoc["name"] = esp_hostname;

  lastWill = "";
  serializeJson(willDoc, lastWill);
  DBUGVAR(lastWill);

  mqttclient.setCredentials(mqtt_user, mqtt_pass);
  mqttclient.setLastWillAndTestimment(mqtt_announce_topic, lastWill, true);
  mqttclient.connect(mqtt_server + ":1883", esp_hostname, []()
  {
    DBUGLN("MQTT connected");

    DynamicJsonDocument doc(JSON_OBJECT_SIZE(5) + 200);

    doc["state"] = "connected";
    doc["id"] = HAL.getLongId();
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
    if (mqtt_solar!="") {
      mqttclient.subscribe(mqtt_solar);
    }
    if (mqtt_grid_ie!="") {
      mqttclient.subscribe(mqtt_grid_ie);
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
mqtt_publish(String data) {
  Profile_Start(mqtt_publish);

  if(!mqttclient.connected()) {
    return;
  }

  String mqtt_data = "";
  String topic = mqtt_topic + "/";

  int i = 0;
  if(data[i] == '{') {
    i++;
  }
  while (int (data[i]) != 0) {
    // Construct MQTT topic e.g. <base_topic>/<status> data
    while (data[i] != ':') {
      if(data[i] != '"') {
        topic += data[i];
      }
      i++;
      if (int (data[i]) == 0) {
        break;
      }
    }
    i++;
    // Construct data string to publish to above topic
    while (data[i] != ',') {
      if(data[i] != '}') {
        mqtt_data += data[i];
      }
      i++;
      if (int (data[i]) == 0) {
        break;
      }
    }
    // send data via mqtt
    //delay(100);
    DBUGF("%s = %s", topic.c_str(), mqtt_data.c_str());
    mqttclient.publish(topic, mqtt_data);
    topic = mqtt_topic + "/";
    mqtt_data = "";
    i++;
    if (int (data[i]) == 0)
      break;
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

  if (!mqttclient.connected()) {
    long now = millis();
    // try and reconnect continuously for first 5s then try again once every 10s
    if ((now < 50000) || ((now - lastMqttReconnectAttempt) > 100000)) {
      lastMqttReconnectAttempt = now;
      mqtt_connect(); // Attempt to reconnect
    }
  }

  Profile_End(mqtt_loop, 5);
}

void
mqtt_restart() {
// TODO
//  if (mqttclient.connected()) {
//    mqttclient.disconnect();
//  }
//  lastMqttReconnectAttempt = 0;
}

boolean
mqtt_connected() {
  return mqttclient.connected();
}
