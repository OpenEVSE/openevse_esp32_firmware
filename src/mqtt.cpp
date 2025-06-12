#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_MQTT)
#undef ENABLE_DEBUG
#endif

#include "mqtt.h"
#include "app_config.h"
#include "openevse.h"
#include "divert.h"
#include "input.h"
#include "espal.h"
#include "net_manager.h"
#include "web_server.h"
#include "manual.h"
#include "scheduler.h"
#include "current_shaper.h"

Mqtt mqtt(evse); // global instance

Mqtt::Mqtt(EvseManager &evseManager) : 
  MicroTasks::Task(),
  _evse(&evseManager)
{
}

Mqtt::~Mqtt() {
  if (_mqttclient.connected()) {
    _mqttclient.disconnect();
  }
}

void Mqtt::begin() {
  MicroTask.startTask(this);
}

void Mqtt::setup() {
  DBUGLN("Mqtt::setup called");
  // Initialization that needs to run once when the task is set up.

  _configVersion = INITIAL_CONFIG_VERSION -1; // Force initial publish
  // Initialize versions to force publish on first connect
  _claimsVersion = evse.getClaimsVersion() == 0 ? 1 : evse.getClaimsVersion() -1; // ensure different
  _overrideVersion = manual.getVersion() == 0 ? 1 : manual.getVersion() -1;
  _scheduleVersion = scheduler.getVersion() == 0 ? 1 : scheduler.getVersion() -1;
  _limitVersion = limit.getVersion() == 0 ? 1 : limit.getVersion() -1;

  // Setup MQTT client callbacks
  _mqttclient.onMessage([this](MongooseString topic, MongooseString payload) {
    this->handleMqttMessage(topic, payload);
  });

  _mqttclient.onError([this](int err) {
    DBUGF("MQTT error %d", err);
    this->onMqttDisconnect(err,
      MG_EV_MQTT_CONNACK_UNACCEPTABLE_VERSION == err ? "CONNACK_UNACCEPTABLE_VERSION" :
      MG_EV_MQTT_CONNACK_IDENTIFIER_REJECTED == err ? "CONNACK_IDENTIFIER_REJECTED" :
      MG_EV_MQTT_CONNACK_SERVER_UNAVAILABLE == err ? "CONNACK_SERVER_UNAVAILABLE" :
      MG_EV_MQTT_CONNACK_BAD_AUTH == err ? "CONNACK_BAD_AUTH" :
      MG_EV_MQTT_CONNACK_NOT_AUTHORIZED == err ? "CONNACK_NOT_AUTHORIZED" :
      strerror(err)
    );
    _error_time = millis();
  });

  _mqttclient.onClose([this]() {
    DBUGLN("MQTT connection closed");
    if (_error_time + 100 < millis()) { // Avoid double event if error just occurred
        this->onMqttDisconnect(-1, "CLOSED");
    }
  });
}

unsigned long Mqtt::loop(MicroTasks::WakeReason reason) {
  Profile_Start(Mqtt_loop);

  // Handle MQTT restart requests
  if (_mqttRestartTime > 0 && millis() > _mqttRestartTime) {
    _mqttRestartTime = 0;
    if (_mqttclient.connected()) {
      DBUGLN("Disconnecting MQTT for restart");
      _mqttclient.disconnect(); // This should trigger onClose and then onMqttDisconnect
    }
     _nextMqttReconnectAttempt = 0; // Force immediate reconnect attempt
     _connecting = false; // Reset connecting flag
  }

  // Manage connection state
  if (config_mqtt_enabled() && !_mqttclient.connected() && !_connecting) {
    long now = millis();
    if (now > _nextMqttReconnectAttempt) {
      _nextMqttReconnectAttempt = now + MQTT_CONNECT_TIMEOUT;
      attemptConnection();
    }
  }

  // If connected, perform periodic checks
  if (_mqttclient.connected()) {
    if (millis() - _loop_timer > MQTT_LOOP_INTERVAL) {
      _loop_timer = millis();
      checkAndPublishUpdates();
    }
  }
  

  Profile_End(Mqtt_loop, 5);
  return MQTT_LOOP_INTERVAL;
}

void Mqtt::attemptConnection() {
  if (!config_mqtt_enabled() || _connecting || _mqttclient.connected()) {
    return;
  }
  _connecting = true;
  DBUGLN("Mqtt attempting connection...");

  String mqtt_host = mqtt_server + ":" + String(mqtt_port);
  DBUGF("MQTT Connecting to... %s://%s", MQTT_MQTT == config_mqtt_protocol() ? "mqtt" : "mqtts", mqtt_host.c_str());

  DynamicJsonDocument willDoc(JSON_OBJECT_SIZE(3) + 60);
  willDoc["state"] = "disconnected";
  willDoc["id"] = ESPAL.getLongId();
  willDoc["name"] = esp_hostname;
  _lastWill = "";
  serializeJson(willDoc, _lastWill);

  if (!config_mqtt_reject_unauthorized()) {
    DEBUG.println("WARNING: Certificate verification disabled");
  }

  _mqttclient.setCredentials(mqtt_user, mqtt_pass);
  _mqttclient.setLastWillAndTestimment(mqtt_announce_topic, _lastWill, true);
  _mqttclient.setRejectUnauthorized(config_mqtt_reject_unauthorized());

  if (mqtt_certificate_id != "") {
    uint64_t cert_id = std::stoull(mqtt_certificate_id.c_str(), nullptr, 16);
    const char *cert = certs.getCertificate(cert_id);
    const char *key = certs.getKey(cert_id);
    if (NULL != cert && NULL != key) {
      _mqttclient.setCertificate(cert, key);
    }
  }

  _connecting = _mqttclient.connect((MongooseMqttProtocol)config_mqtt_protocol(), mqtt_host, esp_hostname, [this]() {
    this->onMqttConnect();
  });

  if(!_connecting) { // if connect call itself failed immediately
      DBUGLN("MQTT immediate connection attempt failed.");
      // onError or onClose should handle the detailed reason if it gets to that stage.
      // If connect() returns false, it means it couldn't even start the attempt.
      onMqttDisconnect(-100, "Initial connection failed"); // Custom error
  }
}

void Mqtt::onMqttConnect() {
  DBUGLN("MQTT connected");
  _connecting = false;
  _nextMqttReconnectAttempt = 0; // Reset reconnect timer

  DynamicJsonDocument doc(JSON_OBJECT_SIZE(5) + 200);
  doc["state"] = "connected";
  doc["id"] = ESPAL.getLongId();
  doc["name"] = esp_hostname;
  doc["mqtt"] = mqtt_topic;
  doc["http"] = "http://" + net.getIp() + "/";

  String announce = "";
  serializeJson(doc, announce);
  _mqttclient.publish(mqtt_announce_topic, announce, true);

  doc.clear();
  doc["mqtt_connected"] = 1;
  event_send(doc);

  subscribeTopics();
  publishInitialState(); // Publish current states like config, claims, etc.
}

void Mqtt::onMqttDisconnect(int err, const char *reason) {
  DBUGLN("MQTT disconnected");
  _connecting = false;
  // _nextMqttReconnectAttempt is handled by the main loop to retry.

  DynamicJsonDocument doc(JSON_OBJECT_SIZE(3) + 70);
  doc["mqtt_connected"] = 0;
  doc["mqtt_close_code"] = err;
  doc["mqtt_close_reason"] = reason;
  event_send(doc);
}

void Mqtt::subscribeTopics() {
  String mqtt_sub_topic;

  // RAPI commands
  mqtt_sub_topic = mqtt_topic + "/rapi/in/#";
  _mqttclient.subscribe(mqtt_sub_topic);
  yield();

  // Divert mode related
  if (config_divert_enabled()) {
    if (divert_type == DIVERT_TYPE_SOLAR && mqtt_solar != "") {
      _mqttclient.subscribe(mqtt_solar); yield();
    }
    if (divert_type == DIVERT_TYPE_GRID && mqtt_grid_ie != "") {
      _mqttclient.subscribe(mqtt_grid_ie); yield();
    }
  }

  // Current shaper related
  if (config_current_shaper_enabled()) {
    if (mqtt_live_pwr != "" && mqtt_live_pwr != mqtt_grid_ie) {
      _mqttclient.subscribe(mqtt_live_pwr); yield();
    }
  }
  
  // Vehicle data
  if (mqtt_vehicle_soc != "") { _mqttclient.subscribe(mqtt_vehicle_soc); yield(); }
  if (mqtt_vehicle_range != "") { _mqttclient.subscribe(mqtt_vehicle_range); yield(); }
  if (mqtt_vehicle_eta != "") { _mqttclient.subscribe(mqtt_vehicle_eta); yield(); }
  if (mqtt_vrms != "") { _mqttclient.subscribe(mqtt_vrms); yield(); }

  // Settable topics
  _mqttclient.subscribe(mqtt_topic + "/divertmode/set"); yield();
  _mqttclient.subscribe(mqtt_topic + "/shaper/set"); yield();
  _mqttclient.subscribe(mqtt_topic + "/override/set"); yield();
  _mqttclient.subscribe(mqtt_topic + "/claim/set"); yield();
  _mqttclient.subscribe(mqtt_topic + "/schedule/set"); yield();
  _mqttclient.subscribe(mqtt_topic + "/schedule/clear"); yield();
  _mqttclient.subscribe(mqtt_topic + "/limit/set"); yield();
  _mqttclient.subscribe(mqtt_topic + "/config/set"); yield();
  _mqttclient.subscribe(mqtt_topic + "/restart"); yield();

  DBUGLN("MQTT Subscriptions complete");
}

void Mqtt::publishInitialState() {
    DBUGLN("MQTT Publishing initial state");
    // Force publish everything on new connection
    _configVersion = INITIAL_CONFIG_VERSION -1;
    _claimsVersion = evse.getClaimsVersion() == 0 ? 1 : evse.getClaimsVersion() -1;
    _overrideVersion = manual.getVersion() == 0 ? 1 : manual.getVersion() -1;
    _scheduleVersion = scheduler.getVersion() == 0 ? 1 : scheduler.getVersion() -1;
    _limitVersion = limit.getVersion() == 0 ? 1 : limit.getVersion() -1;
    
    checkAndPublishUpdates(); // This will now publish everything
}


void Mqtt::checkAndPublishUpdates() {
  if (!isConnected()) return;

  if (_claimsVersion != _evse->getClaimsVersion()) {
    publishClaim();
    DBUGLN("Claims has changed, publishing to MQTT");
    _claimsVersion = _evse->getClaimsVersion();
  }

  if (_overrideVersion != manual.getVersion()) {
    publishOverride();
    DBUGLN("Override has changed, publishing to MQTT");
    _overrideVersion = manual.getVersion();
  }

  if (_scheduleVersion != scheduler.getVersion()) {
    publishSchedule();
    DBUGLN("Schedule has changed, publishing to MQTT");
    _scheduleVersion = scheduler.getVersion();
  }
  if (_limitVersion != limit.getVersion()) {
    publishLimit();
    DBUGLN("Limit has changed, publishing to MQTT");
    _limitVersion = limit.getVersion();
  }

  if (_configVersion != config_version()) {
    publishConfig(); // publishConfig now returns void
    DBUGLN("Config has changed, publishing to MQTT");
    _configVersion = config_version();
  }
}

void Mqtt::handleMqttMessage(MongooseString topic, MongooseString payload) {
  String topic_string = topic.toString();
  String payload_str = payload.toString();

  DBUGLN("Mqtt received:");
  DBUGLN("Topic: " + topic_string);
  DBUGLN("Payload: " + payload_str);

  // Logic from old mqttmsg_callback
  if (topic_string == mqtt_solar){
    solar = payload_str.toInt();
    DBUGF("solar:%dW", solar);
    divert.update_state();
    if (shaper.getState()) {
      shaper.shapeCurrent();
    }
  }
  else if (topic_string == mqtt_grid_ie) {
    grid_ie = payload_str.toInt();
    DBUGF("grid:%dW", grid_ie);
    divert.update_state();
    if (mqtt_live_pwr == mqtt_grid_ie) {
      shaper.setLivePwr(grid_ie);
    }
  }
  else if (topic_string == mqtt_live_pwr) {
      shaper.setLivePwr(payload_str.toInt());
      DBUGF("shaper: Live Pwr:%dW", shaper.getLivePwr());
  }
  else if (topic_string == mqtt_vrms) {
    double volts = payload_str.toFloat();
    DBUGF("voltage:%.1f", volts);
    _evse->setVoltage(volts);
  }
  else if (topic_string == mqtt_vehicle_soc && vehicle_data_src == VEHICLE_DATA_SRC_MQTT) {
    int vehicle_soc = payload_str.toInt();
    _evse->setVehicleStateOfCharge(vehicle_soc);
    StaticJsonDocument<128> event; event["battery_level"] = vehicle_soc; event_send(event);
  }
  else if (topic_string == mqtt_vehicle_range && vehicle_data_src == VEHICLE_DATA_SRC_MQTT) {
    int vehicle_range = payload_str.toInt();
    _evse->setVehicleRange(vehicle_range);
    StaticJsonDocument<128> event; event["battery_range"] = vehicle_range; event_send(event);
  }
  else if (topic_string == mqtt_vehicle_eta && vehicle_data_src == VEHICLE_DATA_SRC_MQTT) {
    int vehicle_eta = payload_str.toInt();
    _evse->setVehicleEta(vehicle_eta);
    StaticJsonDocument<128> event; event["time_to_full_charge"] = vehicle_eta; event_send(event);
  }
  else if (topic_string == mqtt_topic + "/divertmode/set") {
    byte newdivert = payload_str.toInt();
    if ((newdivert==1) || (newdivert==2)) {
      divert.setMode((DivertMode)newdivert);
    }
  }
  else if (topic_string == mqtt_topic + "/shaper/set") {
    byte newshaper = payload_str.toInt();
    if (newshaper==0) shaper.setState(false); else if (newshaper==1) shaper.setState(true);
  }
  else if (topic_string == mqtt_topic + "/override/set") {
    if (payload_str.equals("clear")) {
      if (manual.release()) {
        _override_props.clear();
        publishOverride();
      }
    } else if (payload_str.equals("toggle")) {
      if (manual.toggle()) publishOverride();
    } else if (_override_props.deserialize(payload_str)) {
      setClaim(true, _override_props);
    }
  }
  else if (topic_string == mqtt_topic + "/claim/set") {
    if (payload_str.equals("release")) {
      if (_evse->release(EvseClient_OpenEVSE_MQTT)) {
        _claim_props.clear();
        publishClaim();
      }
    } else if (_claim_props.deserialize(payload_str)) {
      setClaim(false, _claim_props);
    }
  }
  else if (topic_string == mqtt_topic + "/schedule/set") {
    setSchedule(payload_str); // Calls scheduler.deserialize and publishSchedule
  }
  else if (topic_string == mqtt_topic + "/schedule/clear") {
    clearSchedule(payload_str.toInt()); // Calls scheduler.removeEvent and publishSchedule
  }
  else if (topic_string == mqtt_topic + "/limit/set") {
    if (payload_str.equals("clear")) {
      limit.clear();
      publishLimit(); // Need to ensure limit publishes its state.
    } else if (_limit_props.deserialize(payload_str)) {
      setLimit(_limit_props);
    }
  }
  else if (topic_string == mqtt_topic + "/config/set") {
    DynamicJsonDocument doc(4096); // Sufficiently large buffer
    DeserializationError error = deserializeJson(doc, payload_str);
    if(!error) {
      if(config_deserialize(doc)) {
        config_commit(false);
        DBUGLN("Config updated via MQTT");
        // publishConfig() will be called by checkAndPublishUpdates due to configVersion change
      }
    }
  }
  else if (topic_string == mqtt_topic + "/restart") {
    // This logic can reuse the existing mqtt_restart_device logic by making it a static helper or part of this class
    const size_t capacity = JSON_OBJECT_SIZE(1) + 16;
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, payload_str);
    if(!error && doc.containsKey("device")){
        if (strcmp(doc["device"], "gateway") == 0 ) restart_system();
        else if (strcmp(doc["device"], "evse") == 0) _evse->restartEvse();
    }
  }
  else { // RAPI commands
    int rapi_character_index = topic_string.indexOf('$');
    if (rapi_character_index > 1) {
      String cmd = topic_string.substring(rapi_character_index);
      if (payload.length() > 0) cmd += " " + payload_str;

      if(!_evse->isRapiCommandBlocked(cmd)) { // Use EvseManager instance
        rapiSender.sendCmd(cmd, [this](int ret) { // Capture 'this' only
          if (RAPI_RESPONSE_OK == ret || RAPI_RESPONSE_NK == ret) {
            String rapiString = rapiSender.getResponse();
            String mqtt_sub_topic = mqtt_topic + "/rapi/out";
            _mqttclient.publish(mqtt_sub_topic, rapiString);
          }
        });
      }
    }
  }
}

// --- Public Interface Methods ---
bool Mqtt::isConnected() {
  return _mqttclient.connected();
}

void Mqtt::restartConnection() {
  DBUGLN("MQTT restart requested");
  _mqttRestartTime = millis() + 50; // Schedule restart in the near future in loop
}

void Mqtt::publishData(JsonDocument &data) {
  if (!config_mqtt_enabled() || !_mqttclient.connected()) {
    return;
  }
  JsonObject root = data.as<JsonObject>();
  for (JsonPair kv : root) {
    String topic = mqtt_topic + "/" + kv.key().c_str();
    String val = kv.value().as<String>(); // Consider non-string values too if needed
    _mqttclient.publish(topic, val, config_mqtt_retained());
  }
}

// Specific publish methods
void Mqtt::publishConfig() {
  if (!isConnected() || _evse->getEvseState() == OPENEVSE_STATE_STARTING) {
    return;
  }
  const size_t capacity = JSON_OBJECT_SIZE(128) + 1024;
  DynamicJsonDocument doc(capacity);
  config_serialize(doc, true, false, true);

  String fulltopic = mqtt_topic + "/config";
  String payload;
  serializeJson(doc, payload);
  _mqttclient.publish(fulltopic, payload, true); // Config usually retained

  if(config_version() == INITIAL_CONFIG_VERSION) {
      String versionTopic = mqtt_topic + "/config_version";
      String versionPayload = String(config_version());
      _mqttclient.publish(versionTopic, versionPayload, true);
  }
}

void Mqtt::setClaim(bool override, EvseProperties &props) {
  if (override) {
    if (manual.claim(props)) {
      publishOverride();
    }
  } else {
    if (_evse->claim(EvseClient_OpenEVSE_MQTT, EvseManager_Priority_MQTT, props)) {
      publishClaim();
    }
  }
}

void Mqtt::publishClaim() {
  if (!isConnected()) return;
  const size_t capacity = JSON_OBJECT_SIZE(7) + 1024;
  DynamicJsonDocument claimdata(capacity);
  if (_evse->clientHasClaim(EvseClient_OpenEVSE_MQTT)) {
    _evse->serializeClaim(claimdata, EvseClient_OpenEVSE_MQTT);
  } else {
    claimdata["state"] = "null";
  }
  String fulltopic = mqtt_topic + "/claim";
  String payload;
  serializeJson(claimdata, payload);
  _mqttclient.publish(fulltopic, payload, true); // Claims usually retained
}

void Mqtt::publishOverride() {
  if (!isConnected()) return;
  const size_t capacity = JSON_OBJECT_SIZE(7) + 1024;
  DynamicJsonDocument override_data(capacity);
  if (_evse->clientHasClaim(EvseClient_OpenEVSE_Manual) || manual.isActive()) {
    EvseProperties props = _evse->getClaimProperties(EvseClient_OpenEVSE_Manual);
    props.serialize(override_data);
  } else {
    override_data["state"] = "null";
  }
  String fulltopic = mqtt_topic + "/override";
  String payload;
  serializeJson(override_data, payload);
  _mqttclient.publish(fulltopic, payload, true); // Overrides usually retained
}

void Mqtt::setSchedule(String schedulePayload) {
  scheduler.deserialize(schedulePayload);
  publishSchedule();
}

void Mqtt::clearSchedule(uint32_t eventId) {
  scheduler.removeEvent(eventId);
  publishSchedule();
}

void Mqtt::publishSchedule() {
  if (!isConnected()) return;
  const size_t capacity = JSON_OBJECT_SIZE(40) + 2048;
  DynamicJsonDocument schedule_data(capacity);
  if (scheduler.serialize(schedule_data)) {
    String fulltopic = mqtt_topic + "/schedule";
    String payload;
    serializeJson(schedule_data, payload);
    _mqttclient.publish(fulltopic, payload, true); // Schedule usually retained
  }
}

void Mqtt::setLimit(LimitProperties &limitProps) {
  limit.set(limitProps);
  publishLimit(); // This will also update the version
}

void Mqtt::publishLimit() {
  if (!isConnected()) return;
  LimitProperties currentLimitProps = limit.get();
  const size_t capacity = JSON_OBJECT_SIZE(3) + 512;
  DynamicJsonDocument limit_data(capacity);
  if (currentLimitProps.serialize(limit_data)) {
    String fulltopic = mqtt_topic + "/limit";
    String payload;
    serializeJson(limit_data, payload);
    _mqttclient.publish(fulltopic, payload, true); // Limits usually retained
  }
}

// --- Notification methods ---
void Mqtt::notifyEvseClaimChanged() {
    if (_claimsVersion != _evse->getClaimsVersion()) {
        _claimsVersion = _evse->getClaimsVersion(); // Update internal version
        if (isConnected()) publishClaim();
    }
}

void Mqtt::notifyManualOverrideChanged() {
     if (_overrideVersion != manual.getVersion()) {
        _overrideVersion = manual.getVersion();
        if (isConnected()) publishOverride();
    }
}

void Mqtt::notifyScheduleChanged() {
    if (_scheduleVersion != scheduler.getVersion()) {
        _scheduleVersion = scheduler.getVersion();
        if (isConnected()) publishSchedule();
    }
}

void Mqtt::notifyLimitChanged() {
    if (_limitVersion != limit.getVersion()) {
        _limitVersion = limit.getVersion();
        if (isConnected()) publishLimit();
    }
}

void Mqtt::notifyConfigChanged() {
    if (_configVersion != config_version()) {
        _configVersion = config_version();
        if (isConnected()) publishConfig();
    }
}