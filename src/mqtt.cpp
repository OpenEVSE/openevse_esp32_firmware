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
#include "certificates.h"

#include "openevse.h"
#include "current_shaper.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MongooseMqttClient.h>

#include <string>

MongooseMqttClient mqttclient;
EvseProperties claim_props;
EvseProperties override_props;
LimitProperties limit_props;
DynamicJsonDocument mqtt_doc(4096);

static long nextMqttReconnectAttempt = 0;
static unsigned long mqttRestartTime = 0;
static bool connecting = false;
uint8_t claimsVersion = 0;
uint8_t overrideVersion = 0;
uint8_t scheduleVersion = 0;
uint8_t limitVersion = 0;
uint32_t configVersion = 0;

String lastWill = "";

int loop_timer = 0;
unsigned long error_time = 0;

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
    //recalculate shaper
    if (shaper.getState()) {
      shaper.shapeCurrent();
    }
  }
  else if (topic_string == mqtt_grid_ie)
  {
    grid_ie = payload_str.toInt();
    DBUGF("grid:%dW", grid_ie);
    divert.update_state();

    // if shaper use the same topic as grid_ie
    if (mqtt_live_pwr == mqtt_grid_ie) {
      shaper.setLivePwr(grid_ie);
    }
  }
  else if (topic_string == mqtt_live_pwr)
  {
      shaper.setLivePwr(payload_str.toInt());
      DBUGF("shaper: Live Pwr:%dW", shaper.getLivePwr());
  }
  else if (topic_string == mqtt_vrms)
  {
    // TODO: The voltage is no longer a global, need to do something so we don't have
    //       to read back from the EVSE
    double volts = payload_str.toFloat();
    DBUGF("voltage:%.1f", volts);
    evse.setVoltage(volts);
  }
  else if (topic_string == mqtt_vehicle_soc && vehicle_data_src == VEHICLE_DATA_SRC_MQTT)
  {
    int vehicle_soc = payload_str.toInt();
    DBUGF("vehicle_soc:%d%%", vehicle_soc);
    evse.setVehicleStateOfCharge(vehicle_soc);

    StaticJsonDocument<128> event;
    event["battery_level"] = vehicle_soc;
    event["vehicle_state_update"] = 0;
    event_send(event);
  }
  else if (topic_string == mqtt_vehicle_range && vehicle_data_src == VEHICLE_DATA_SRC_MQTT)
  {
    int vehicle_range = payload_str.toInt();
    DBUGF("vehicle_range:%dKM", vehicle_range);
    evse.setVehicleRange(vehicle_range);

    StaticJsonDocument<128> event;
    event["battery_range"] = vehicle_range;
    event["vehicle_state_update"] = 0;
    event_send(event);
  }
  else if (topic_string == mqtt_vehicle_eta && vehicle_data_src == VEHICLE_DATA_SRC_MQTT)
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
      shaper.setState(false);
    } else if (newshaper==1) {
      shaper.setState(true);
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

  else if (topic_string == mqtt_topic + "/limit/set") {
    if (payload_str.equals("clear")) {
      DBUGLN("clearing limits");
      limit.clear();
    }
    else if (limit_props.deserialize(payload_str)) {
      mqtt_set_limit(limit_props);
    }
  }

  else if (topic_string == mqtt_topic + "/config/set") {
    const size_t capacity = JSON_OBJECT_SIZE(128) + 1024;
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, payload_str);
    if(!error)
    {
      bool config_modified = config_deserialize(doc);
      if(config_modified)
      {
        config_commit(false);
        DBUGLN("Config updated");
      }
    }
  }

  // Restart
  else if (topic_string == mqtt_topic + "/restart") {
    mqtt_restart_device(payload_str);
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

static void mqtt_disconnected(int err, const char *reason)
{
  connecting = false;
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(1) + 60);
  doc["mqtt_connected"] = 0;
  doc["mqtt_close_code"] = err;
  doc["mqtt_close_reason"] = reason;
  event_send(doc);
}

// -------------------------------------------------------------------
// MQTT Connect
// -------------------------------------------------------------------
boolean
mqtt_connect()
{
  DBUGVAR(mqttclient.connected());
  DBUGVAR(connecting);
  if(connecting) {
    return false;
  }
  connecting = true;

  mqttclient.onMessage(mqttmsg_callback); //function to be called when mqtt msg is received on subscribed topic
  mqttclient.onError([](int err)
  {
    DBUGF("MQTT error %d", err);
    // This is a bit of a hack to get around the fact that the Mongoose MQTT client
    // munges together two sets of error codes, one for the TCP connection and one
    // for the MQTT protocol.
    mqtt_disconnected(err,
      MG_EV_MQTT_CONNACK_UNACCEPTABLE_VERSION == err ? "CONNACK_UNACCEPTABLE_VERSION" :
      MG_EV_MQTT_CONNACK_IDENTIFIER_REJECTED == err ? "CONNACK_IDENTIFIER_REJECTED" :
      MG_EV_MQTT_CONNACK_SERVER_UNAVAILABLE == err ? "CONNACK_SERVER_UNAVAILABLE" :
      MG_EV_MQTT_CONNACK_BAD_AUTH == err ? "CONNACK_BAD_AUTH" :
      MG_EV_MQTT_CONNACK_NOT_AUTHORIZED == err ? "CONNACK_NOT_AUTHORIZED" :
      strerror(err)
    );
    error_time = millis();
  });

  mqttclient.onClose([]()
  {
    DBUGF("MQTT connection closed");
    // Don't send a disconnect event if we just had an error, as we already
    // sent one. This is a bit of a hack, but it's the easiest way to avoid
    // sending two events for the same disconnect.
    if(error_time + 100 < millis()) {
      mqtt_disconnected(-1, "CLOSED");
    }
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

  if(mqtt_certificate_id != "")
  {
    uint64_t cert_id = std::stoull(mqtt_certificate_id.c_str(), nullptr, 16);
    const char *cert = certs.getCertificate(cert_id);
    const char *key = certs.getKey(cert_id);
    if(NULL != cert && NULL != key) {
      mqttclient.setCertificate(cert, key);
    }
  }

  connecting = mqttclient.connect((MongooseMqttProtocol)config_mqtt_protocol(), mqtt_host, esp_hostname, []()
  {
    DBUGLN("MQTT connected");

    DynamicJsonDocument doc(JSON_OBJECT_SIZE(5) + 200);

    doc["state"] = "connected";
    doc["id"] = ESPAL.getLongId();
    doc["name"] = esp_hostname;
    doc["mqtt"] = mqtt_topic;
    doc["http"] = "http://"+net.getIp()+"/";

    // Once connected, publish an announcement..
    String announce = "";
    serializeJson(doc, announce);
    DBUGVAR(announce);
    mqttclient.publish(mqtt_announce_topic, announce, true);

    doc.clear();

    doc["mqtt_connected"] = 1;
    event_send(doc);

    // Publish MQTT override/claim
    mqtt_publish_config();
    mqtt_publish_override();
    mqtt_publish_claim();
    mqtt_publish_schedule();
    mqtt_publish_limit();

    // MQTT Topic to subscribe to receive RAPI commands via MQTT
    String mqtt_sub_topic = mqtt_topic + "/rapi/in/#";

    // e.g to set current to 13A: <base-topic>/rapi/in/$SC 13
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    // subscribe to solar PV / grid_ie MQTT feeds
    if(config_divert_enabled())
    {
      if (divert_type == DIVERT_TYPE_SOLAR && mqtt_solar != "")
      {
        mqttclient.subscribe(mqtt_solar);
        yield();
      }
      if (divert_type == DIVERT_TYPE_GRID && mqtt_grid_ie != "")
      {
        mqttclient.subscribe(mqtt_grid_ie);
        yield();
      }
    }
    // subscribe to current shaper MQTT feeds
    if(config_current_shaper_enabled())
    {
      if (mqtt_live_pwr != "") {
        if ( mqtt_live_pwr != mqtt_grid_ie ) {
          // only subscribe once
          mqttclient.subscribe(mqtt_live_pwr);
          yield();
        }
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

    mqtt_sub_topic = mqtt_topic + "/limit/set";
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    mqtt_sub_topic = mqtt_topic + "/config/set";
    mqttclient.subscribe(mqtt_sub_topic);
    yield();

    // ask for a system restart
    mqtt_sub_topic = mqtt_topic + "/restart";
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
  const size_t capacity = JSON_OBJECT_SIZE(7) + 1024;
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
  DBUGLN("MQTT publish_override()");
  if(!config_mqtt_enabled() || !mqttclient.connected()) {
    return;
  }
  const size_t capacity = JSON_OBJECT_SIZE(7) + 1024;
  DynamicJsonDocument override_data(capacity);
  EvseProperties props;
  //check if there an override claim
  if (evse.clientHasClaim(EvseClient_OpenEVSE_Manual) || manual.isActive()) {
    props = evse.getClaimProperties(EvseClient_OpenEVSE_Manual);
    //check if there's state property in override
    props.serialize(override_data);
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

bool
mqtt_publish_config() {
  if(!config_mqtt_enabled() || !mqttclient.connected() || evse.getEvseState() == OPENEVSE_STATE_STARTING) {
    return false;
  }
  const size_t capacity = JSON_OBJECT_SIZE(128) + 1024;
  DynamicJsonDocument doc(capacity);
  config_serialize(doc, true, false, true);
  mqtt_publish_json(doc, "/config");

  if(config_version() == INITIAL_CONFIG_VERSION) {
    String fulltopic = mqtt_topic + "/config_version";
    String payload = String(config_version());
    mqttclient.publish(fulltopic, payload, true);
  }

  return true;
}

void
mqtt_set_limit(LimitProperties &limitProps) {
  Profile_Start(mqtt_set_limit);
  limit.set(limitProps);
  mqtt_publish_limit();
  Profile_End(mqtt_set_limit, 5);
}

void
mqtt_publish_limit() {
  LimitProperties limitProps;
  const size_t capacity = JSON_OBJECT_SIZE(3) + 512;
  DynamicJsonDocument limit_data(capacity);
  limitProps = limit.get();
  bool success = limitProps.serialize(limit_data);
  mqtt_publish_json(limit_data, "/limit");
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

void
mqtt_restart_device(String payload_str) {
  const size_t capacity = JSON_OBJECT_SIZE(1) + 16;
  DynamicJsonDocument doc(capacity);
  DeserializationError error = deserializeJson(doc, payload_str);
  if(!error)
  {
    if(doc.containsKey("device")){
      if (strcmp(doc["device"], "gateway") == 0 ) {
        restart_system();
      }
      else if (strcmp(doc["device"], "evse") == 0) {
        evse.restartEvse();
      }
    }
  }
}
// -------------------------------------------------------------------
// MQTT state management
//
// Call every time around loop() if connected to the WiFi
// -------------------------------------------------------------------
void mqtt_loop()
{
  Profile_Start(mqtt_loop);

  // Do we need to restart MQTT?
  if(mqttRestartTime > 0 && millis() > mqttRestartTime)
  {
    mqttRestartTime = 0;
    if (mqttclient.connected()) {
      DBUGF("Disconnecting MQTT");
      mqttclient.disconnect();
      DynamicJsonDocument doc(JSON_OBJECT_SIZE(1) + 60);
      doc["mqtt_connected"] = 0;
      event_send(doc);
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

  // Temporise loop
  if (millis() - loop_timer > MQTT_LOOP) {
    loop_timer = millis();
    if (claimsVersion != evse.getClaimsVersion()) {
      mqtt_publish_claim();
      DBUGF("Claims has changed, publishing to MQTT");
      claimsVersion = evse.getClaimsVersion();
    }

    if (overrideVersion != manual.getVersion()) {
      mqtt_publish_override();
      DBUGF("Override has changed, publishing to MQTT");
      overrideVersion = manual.getVersion();
    }

    if (scheduleVersion != scheduler.getVersion()) {
      mqtt_publish_schedule();
      DBUGF("Schedule has changed, publishing to MQTT");
      scheduleVersion = scheduler.getVersion();
    }
    if (limitVersion != limit.getVersion()) {
      mqtt_publish_limit();
      DBUGF("Limit has changed, publishing to MQTT");
      limitVersion = limit.getVersion();
    }

    if(configVersion != config_version() && mqtt_publish_config()) {
      DBUGF("Config has changed, publishing to MQTT");
      configVersion = config_version();
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
