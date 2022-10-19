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
#include "web_server.h"
#include "event.h"
#include "manual.h"
#include "scheduler.h"


#include "openevse.h"
#include "current_shaper.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MongooseMqttClient.h>

MongooseMqttClient mqttclient;
EvseProperties claim_props;
EvseProperties override_props;
DynamicJsonDocument mqtt_doc(4096);

static long nextMqttReconnectAttempt = 0;
static unsigned long mqttRestartTime = 0;
static bool connecting = false;
static bool mqttRetained = false;

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
    divert.update_state();
  }
  else if (topic_string == mqtt_grid_ie)
  {
    grid_ie = payload_str.toInt();
    DBUGF("grid:%dW", grid_ie);
    divert.update_state();
  }
  else if (topic_string == mqtt_live_pwr)
  {
      shaper.setLivePwr(payload_str.toInt());
      DBUGF("shaper: available power:%dW", shaper.getAvlPwr());
  }
  else if (topic_string == mqtt_vrms)
  {
    // TODO: The voltage is no longer a global, need to do something so we don't have
    //       to read back from the EVSE
    double volts = payload_str.toFloat();
    DBUGF("voltage:%.1f", volts);
    evse.setVoltage(volts);
  }
  else if (topic_string == mqtt_vehicle_soc)
  {
    int vehicle_soc = payload_str.toInt();
    DBUGF("vehicle_soc:%d%%", vehicle_soc);
    evse.setVehicleStateOfCharge(vehicle_soc);

    StaticJsonDocument<128> event;
    event["battery_level"] = vehicle_soc;
    event["vehicle_state_update"] = 0;
    event_send(event);
  }
  else if (topic_string == mqtt_vehicle_range)
  {
    int vehicle_range = payload_str.toInt();
    DBUGF("vehicle_range:%dKM", vehicle_range);
    evse.setVehicleRange(vehicle_range);

    StaticJsonDocument<128> event;
    event["battery_range"] = vehicle_range;
    event["vehicle_state_update"] = 0;
    event_send(event);
  }
  else if (topic_string == mqtt_vehicle_eta)
  {
    int vehicle_eta = payload_str.toInt();
    DBUGF("vehicle_eta:%d", vehicle_eta);
    evse.setVehicleEta(vehicle_eta);

    StaticJsonDocument<128> event;
    event["time_to_full_charge"] = vehicle_eta;
    event["vehicle_state_update"] = 0;
    event_send(event);
  }
  // Divert Mode
  else if (topic_string == mqtt_topic + "/divertmode/set")
  {
    byte newdivert = payload_str.toInt();
    if ((newdivert==1) || (newdivert==2)) {
      divert.setMode((DivertMode)newdivert);
    }
  }
  else if (topic_string == mqtt_topic + "/shaper/set")
  {
    byte newshaper = payload_str.toInt();
    if (newshaper==0) {
      shaper.setState(0);
    } else if (newshaper==1) {
      shaper.setState(1);
    }
  }
  // Manual Override
  else if (topic_string == mqtt_topic + "/override/set") {
    if (payload_str.equals("clear")) {
      if (manual.release()) {
        override_props.clear();
        mqtt_publish_override();
      }
    }
    else if (payload_str.equals("toggle")) {
      if (manual.toggle()) {
        mqtt_publish_override();
      }
    }
    else if (override_props.deserialize(payload_str)) {
      mqtt_set_claim(true, override_props);
      }
  }
  
  // Claim
  else if (topic_string == mqtt_topic + "/claim/set") {
    if (payload_str.equals("release")) {
      if(evse.release(EvseClient_OpenEVSE_MQTT)) {
        claim_props.clear();
        mqtt_publish_claim();
      }
    }
    else if (claim_props.deserialize(payload_str)) {
      mqtt_set_claim(false, claim_props);
    }
  }
  
  //Schedule
  else if (topic_string == mqtt_topic + "/schedule/set") {
    mqtt_set_schedule(payload_str);
  }
  else if (topic_string == mqtt_topic + "/schedule/clear") {
    mqtt_clear_schedule(payload_str.toInt());
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

      if(!evse.isRapiCommandBlocked(cmd))
      {
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

    // Publish MQTT override/claim
    mqtt_publish_override();
    mqtt_publish_claim();
    mqtt_publish_schedule();

    // MQTT Topic to subscribe to receive RAPI commands via MQTT
    String mqtt_sub_topic = mqtt_topic + "/rapi/in/#";

    // e.g to set current to 13A: <base-topic>/rapi/in/$SC 13
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    // subscribe to solar PV / grid_ie MQTT feeds
    if(config_divert_enabled())
    {
      if (mqtt_solar != "") {
        mqttclient.subscribe(mqtt_solar);
        yield();
      }
      if (mqtt_grid_ie != "") {
        mqttclient.subscribe(mqtt_grid_ie);
        yield();
      }
    }
    // subscribe to current shaper MQTT feeds
    if(config_current_shaper_enabled())
    {
      if (mqtt_live_pwr != "") {
        mqttclient.subscribe(mqtt_live_pwr);
        yield();
      }
    }
    // subscribe to vehicle information from MQTT if we are configured for it
    if (mqtt_vehicle_soc != "") {
        mqttclient.subscribe(mqtt_vehicle_soc);
        yield();
    }
    if (mqtt_vehicle_range != "") {
        mqttclient.subscribe(mqtt_vehicle_range);
        yield();
    }
    if (mqtt_vehicle_eta != "") {
        mqttclient.subscribe(mqtt_vehicle_eta);
        yield();
    }

    if (mqtt_vrms!="") {
      mqttclient.subscribe(mqtt_vrms);
      yield();
    }
    // settable mqtt topics
    mqtt_sub_topic = mqtt_topic + "/divertmode/set";
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    mqtt_sub_topic = mqtt_topic + "/shaper/set";
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    mqtt_sub_topic = mqtt_topic + "/override/set";        
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    mqtt_sub_topic = mqtt_topic + "/claim/set";        
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    mqtt_sub_topic = mqtt_topic + "/schedule/set";        
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    mqtt_sub_topic = mqtt_topic + "/schedule/clear";        
    mqttclient.subscribe(mqtt_sub_topic);
    yield();
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
    mqttclient.publish(topic, val, config_mqtt_retained());
    topic = mqtt_topic + "/";
  }

  Profile_End(mqtt_publish, 5);
}

void
mqtt_set_claim(bool override, EvseProperties &props) {
  Profile_Start(mqtt_set_claim);
  //0: claim , 1: manual override
  if (override) {
    if (manual.claim(props)) {
      mqtt_publish_override();
    }
  }
  else {
    if (evse.claim(EvseClient_OpenEVSE_MQTT, EvseManager_Priority_MQTT, props)) {
      mqtt_publish_claim();
    }
  }
  
  Profile_End(mqtt_set_claim, 5);
}

void
mqtt_publish_claim() {
  if(!config_mqtt_enabled() || !mqttclient.connected()) {
    return;
  }
  bool hasclaim = evse.clientHasClaim(EvseClient_OpenEVSE_MQTT);
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument claimdata(capacity);
  if(hasclaim) {  
    evse.serializeClaim(claimdata, EvseClient_OpenEVSE_MQTT);
    
  } 
  else {
    claimdata["state"] = "null";
  }
  mqtt_publish_json(claimdata, "/claim");
}

void
mqtt_publish_override() {
  if(!config_mqtt_enabled() || !mqttclient.connected()) {
    return;
  }
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument override_data(capacity);
  EvseProperties props;
  //check if there an override claim
  if (evse.clientHasClaim(EvseClient_OpenEVSE_Manual)) {
    props = evse.getClaimProperties(EvseClient_OpenEVSE_Manual);
    //check if there's state property in override
    if(props.getState() != 0) {
      props.serialize(override_data); 
    }
    else {
      override_data["state"] = "null";
    }
  }
  else override_data["state"] = "null";
  mqtt_publish_json(override_data, "/override");

  
  
}

void mqtt_set_schedule(String schedule) {
  Profile_Start(mqtt_set_schedule);
  scheduler.deserialize(schedule);
  mqtt_publish_schedule();
  Profile_End(mqtt_set_schedule, 5); 
}

void
mqtt_clear_schedule(uint32_t event) {
  Profile_Start(mqtt_clear_schedule);
  scheduler.removeEvent(event);
  Profile_End(mqtt_clear_schedule, 5); 
  mqtt_publish_schedule();
}

void
mqtt_publish_schedule() {
  if(!config_mqtt_enabled() || !mqttclient.connected()) {
    return;
  }
  const size_t capacity = JSON_OBJECT_SIZE(40) + 2048;
  DynamicJsonDocument schedule_data(capacity);
  EvseProperties props;
  bool success = scheduler.serialize(schedule_data);
  if (success) {
    mqtt_publish_json(schedule_data, "/schedule");
  }
}

void 
mqtt_publish_json(JsonDocument &data, const char* topic) {
  Profile_Start(mqtt_publish_json);
  if(!config_mqtt_enabled() || !mqttclient.connected()) {
      return;
      }
  
  String fulltopic = mqtt_topic + topic;
  String doc;
  serializeJson(data, doc);
  mqttclient.publish(fulltopic,doc, true); // claims are always published as retained as they are not updated regularly
  Profile_End(mqtt_publish_json, 5);
  
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
