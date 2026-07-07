#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_MQTT)
#undef ENABLE_DEBUG
#endif

#include "mqtt.h"
#include "app_config.h"
#ifdef EPOXY_DUINO
#include <netdb.h>
#else
#include <lwip/netdb.h>
#endif
#include "openevse.h"
#include "divert.h"
#include "input.h"
#include "espal.h"
#include "net_manager.h"
#include "web_server.h"
#include "manual.h"
#include "scheduler.h"
#include "current_shaper.h"
#include "home_battery.h"

Mqtt mqtt(evse); // global instance

Mqtt::Mqtt(EvseManager &evseManager) :
  MicroTasks::Task(),
  _evse(&evseManager),
  _connectedSince(0),
  _lastRxTime(0)
{
  _brokerIp[0]      = '\0';
  _brokerVersion[0] = '\0';
  _errorCategory[0] = '\0';
  _errorDetail[0]   = '\0';
}

Mqtt::~Mqtt() {
  if (_mqttclient.connected()) {
    _mqttclient.disconnect();
  }
}

void Mqtt::setError(const char *category, const char *detail) {
  strncpy(_errorCategory, category ? category : "", sizeof(_errorCategory) - 1);
  _errorCategory[sizeof(_errorCategory) - 1] = '\0';
  strncpy(_errorDetail, detail ? detail : "", sizeof(_errorDetail) - 1);
  _errorDetail[sizeof(_errorDetail) - 1] = '\0';
}

unsigned long Mqtt::retryDelay() {
  // Same table as TimeManager::retryDelay() (time_man.cpp) — see mqtt.h.
  static const unsigned long delays[] = {
    10 * 1000UL,
    30 * 1000UL,
    90 * 1000UL,
     5 * 60 * 1000UL,
    30 * 60 * 1000UL,
  };
  uint8_t idx = _retryCount < sizeof(delays) / sizeof(delays[0])
                  ? _retryCount
                  : (uint8_t)(sizeof(delays) / sizeof(delays[0]) - 1);
  return delays[idx];
}

void Mqtt::scheduleReconnect() {
  _nextMqttReconnectAttempt = millis() + retryDelay();
  if (_retryCount < 255) _retryCount++;
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
     _retryCount = 0; // user explicitly asked to retry now — start the backoff fresh
     _connecting = false; // Reset connecting flag
  }

  // If a connection attempt has been in progress too long with no callback, reset and retry.
  // This handles the case where the TCP stack hangs without firing onError or onClose.
  if (_connecting && (millis() - _connectStartTime) > (MQTT_CONNECT_TIMEOUT * 2)) {
    DBUGLN("MQTT connection attempt timed out, will retry");
    _connecting = false;
    scheduleReconnect();
    setError("timeout", "Server not responding");
    StaticJsonDocument<160> doc;
    doc["mqtt_connected"]    = 0;
    doc["mqtt_status"]       = "disconnected";
    doc["mqtt_error"]        = _errorCategory;
    doc["mqtt_error_detail"] = _errorDetail;
    web_server_event(doc);
  }

  // Manage connection state. _nextMqttReconnectAttempt is advanced by
  // scheduleReconnect() (exponential back-off) once a result is known —
  // not preemptively here — since _connecting already prevents this block
  // from re-entering while an attempt is in flight.
  if (net.isConnected() && config_mqtt_enabled() && !_mqttclient.connected() && !_connecting) {
    if (millis() > _nextMqttReconnectAttempt) {
      attemptConnection();
    }
  }

  // If connected, perform periodic checks and safe deferred DNS lookup
  if (_mqttclient.connected()) {
    if (millis() - _loop_timer > MQTT_LOOP_INTERVAL) {
      _loop_timer = millis();
      checkAndPublishUpdates();
    }

    // DNS lookup deferred from onMqttConnect (safe to block here, not in callback)
    if (_needsDnsLookup) {
      _needsDnsLookup = false;
      struct in_addr addr4; struct in6_addr addr6;
      bool isIp = (inet_pton(AF_INET,  mqtt_server.c_str(), &addr4) == 1) ||
                  (inet_pton(AF_INET6, mqtt_server.c_str(), &addr6) == 1);
      if (isIp) {
        strncpy(_brokerIp, mqtt_server.c_str(), sizeof(_brokerIp) - 1);
      } else if (mqtt_server.length() > 0) {
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        if (getaddrinfo(mqtt_server.c_str(), nullptr, &hints, &res) == 0 && res) {
          void *addr = res->ai_family == AF_INET
            ? (void *)&((struct sockaddr_in  *)res->ai_addr)->sin_addr
            : (void *)&((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
          inet_ntop(res->ai_family, addr, _brokerIp, sizeof(_brokerIp) - 1);
          freeaddrinfo(res);
        } else {
          strncpy(_brokerIp, "failed", sizeof(_brokerIp) - 1);
        }
        _brokerIp[sizeof(_brokerIp) - 1] = '\0';
      }
      if (_brokerIp[0] != '\0') {
        // WebSocket only — this is a UI status field, not broker data
        StaticJsonDocument<128> dns_event;
        dns_event["mqtt_broker_ip"] = _brokerIp;
        web_server_event(dns_event);
      }
    }
  }

  // Periodic status push so GUI always reflects the real connection state
  // (covers cases where the page loads between connect/disconnect events).
  // WebSocket only — do not re-publish status fields back to the broker.
  if (millis() - _lastStatusPush > 15000) {
    _lastStatusPush = millis();
    StaticJsonDocument<128> status_event;
    status_event["mqtt_connected"] = (int)_mqttclient.connected();
    status_event["mqtt_status"]    = getMqttStatus();
    web_server_event(status_event);
  }

  Profile_End(Mqtt_loop, 5);
  return MQTT_LOOP_INTERVAL;
}

void Mqtt::attemptConnection() {
  if (!config_mqtt_enabled() || _connecting || _mqttclient.connected()) {
    return;
  }
  _connecting = true;
  _connectStartTime = millis();
  _brokerIp[0] = '\0';   // clear stale DNS badge while new attempt is in flight
  DBUGF("MQTT attempting connection... (%s)\n", net.isConnected() ? "connected" : "not connected");

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

  _mqttclient.setCredentials("", mqtt_pass.c_str());
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
  _nextMqttReconnectAttempt = 0;
  _retryCount = 0;
  _connectedSince  = time(NULL);
  _brokerVersion[0] = '\0';   // fresh — will arrive via $SYS/broker/version
  _brokerIp[0]     = '\0';   // cleared; DNS lookup scheduled for loop() below
  _errorCategory[0] = '\0';  // clear any prior failure reason
  _errorDetail[0]   = '\0';

  // Do NOT call getaddrinfo() here — this is a Mongoose callback and getaddrinfo()
  // uses LwIP's resolver (separate cache from Mongoose's), so it can block the
  // event loop for hundreds of milliseconds, causing the broker to drop the TCP
  // connection. Schedule it for loop() instead.
  _needsDnsLookup = true;
  MicroTask.wakeTask(this);

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
  doc["mqtt_connected"]      = 1;
  doc["mqtt_status"]         = "connected";
  doc["mqtt_connected_since"] = (uint32_t)_connectedSince;
  if (_brokerIp[0] != '\0') doc["mqtt_broker_ip"] = _brokerIp;
  doc["mqtt_broker_version"] = "";   // reset; will be updated when $SYS reply arrives
  event_send(doc);

  subscribeTopics();
  publishInitialState();
}

void Mqtt::onMqttDisconnect(int err, const char *reason) {
  DBUGLN("MQTT disconnected");
  _connecting = false;
  scheduleReconnect(); // exponential back-off — see Mqtt::retryDelay()
  // Do NOT call getaddrinfo() here — this is a Mongoose callback and will
  // block the entire event loop, delaying the reconnect attempt by the full
  // DNS round-trip time. DNS is handled safely in loop() after reconnect.
  MicroTask.wakeTask(this);

  // Classify the failure so the UI can show an actionable reason. The CONNACK
  // codes (1-5) arrive via the broker's connection-acknowledgement; anything
  // else is a transport/network level close.
  const char *category;
  switch (err) {
    case MG_EV_MQTT_CONNACK_BAD_AUTH:
    case MG_EV_MQTT_CONNACK_NOT_AUTHORIZED:    category = "auth";        break;
    case MG_EV_MQTT_CONNACK_SERVER_UNAVAILABLE: category = "unavailable"; break;
    case MG_EV_MQTT_CONNACK_IDENTIFIER_REJECTED: category = "id_rejected"; break;
    case MG_EV_MQTT_CONNACK_UNACCEPTABLE_VERSION: category = "version";    break;
    default:                                   category = "network";     break;
  }
  setError(category, reason);

  DynamicJsonDocument doc(JSON_OBJECT_SIZE(6) + 160);
  doc["mqtt_connected"]    = 0;
  doc["mqtt_status"]       = "disconnected";
  doc["mqtt_close_code"]   = err;
  doc["mqtt_close_reason"] = reason;
  doc["mqtt_error"]        = _errorCategory;
  doc["mqtt_error_detail"] = _errorDetail;
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
  if (mqtt_vehicle_charge_limit != "") { _mqttclient.subscribe(mqtt_vehicle_charge_limit); yield(); }
  if (mqtt_home_battery_soc != "") { _mqttclient.subscribe(mqtt_home_battery_soc); yield(); }
  if (mqtt_home_battery_power != "") { _mqttclient.subscribe(mqtt_home_battery_power); yield(); }
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

  // Broker metadata — most brokers publish this as a retained message
  _mqttclient.subscribe("$SYS/broker/version"); yield();

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

  // Record last receive time silently. Do NOT event_send() here — event_send()
  // re-publishes to the broker, which on a busy broker (subscribed to 1 Hz topics
  // like emon/emonpi/power1) would create a publish storm. The GUI reads this via
  // its periodic GET /mqtt poll instead.
  _lastRxTime = time(NULL);

  // Broker version advertised by the broker itself
  if (topic_string == "$SYS/broker/version") {
    strncpy(_brokerVersion, payload_str.c_str(), sizeof(_brokerVersion) - 1);
    _brokerVersion[sizeof(_brokerVersion) - 1] = '\0';
    // WebSocket only — do not echo broker metadata back to the broker
    StaticJsonDocument<128> ver_event;
    ver_event["mqtt_broker_version"] = _brokerVersion;
    web_server_event(ver_event);
    return;
  }

  // Logic from old mqttmsg_callback
  if (topic_string == mqtt_solar){
    divert.setSolar(payload_str.toInt());
    DBUGF("solar:%dW", divert.getSolar());
    divert.update_state();
    if (shaper.getState()) {
      shaper.shapeCurrent();
    }
  }
  else if (topic_string == mqtt_grid_ie) {
    divert.setGridIe(payload_str.toInt());
    DBUGF("grid:%dW", divert.getGridIe());
    divert.update_state();
    if (mqtt_live_pwr == mqtt_grid_ie) {
      shaper.setLivePwr(divert.getGridIe());
    }
  }
  else if (topic_string == mqtt_live_pwr) {
      shaper.setLivePwr(payload_str.toInt());
      DBUGF("shaper: Live Pwr:%dW", shaper.getLivePwr());
  }
  else if (topic_string == mqtt_vrms) {
    double volts = payload_str.toFloat();
    DBUGF("voltage:%.1f", volts);
    _evse->setMqttVoltage(volts);
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
  else if (topic_string == mqtt_vehicle_charge_limit && vehicle_data_src == VEHICLE_DATA_SRC_MQTT) {
    int vehicle_charge_limit = payload_str.toInt();
    _evse->setVehicleChargeLimit(vehicle_charge_limit);
    StaticJsonDocument<128> event; event["vehicle_charge_limit"] = vehicle_charge_limit; event_send(event);
  }
  // Home/powerwall battery is display-only with no source arbitration (mirrors its
  // POST /status path), so it is gated only on the topic being configured.
  else if (mqtt_home_battery_soc != "" && topic_string == mqtt_home_battery_soc) {
    int soc = payload_str.toInt();
    home_battery_set_soc(soc);
    StaticJsonDocument<128> event; event["home_battery_soc"] = soc; event_send(event);
  }
  else if (mqtt_home_battery_power != "" && topic_string == mqtt_home_battery_power) {
    int power = payload_str.toInt();
    home_battery_set_power(power);
    StaticJsonDocument<128> event; event["home_battery_power"] = power; event_send(event);
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

const char *Mqtt::getMqttStatus() {
  if (!config_mqtt_enabled()) return "disabled";
  if (_mqttclient.connected())  return "connected";
  if (_connecting)              return "connecting";
  return "disconnected";
}

void Mqtt::restartConnection() {
  DBUGLN("MQTT restart requested");

  // Tear down immediately rather than waiting for a scheduled restart
  if (_mqttclient.connected()) {
    _mqttclient.disconnect();
  }
  _connecting = false;
  _needsDnsLookup = false;
  _brokerIp[0] = '\0';
  _errorCategory[0] = '\0';   // clear stale failure reason on manual restart
  _errorDetail[0]   = '\0';
  _nextMqttReconnectAttempt = 0; // reconnect on the very next loop iteration
  _retryCount = 0; // explicit restart — start the backoff fresh

  // Push "connecting" status immediately so the UI reacts
  {
    StaticJsonDocument<160> doc;
    doc["mqtt_connected"]    = 0;
    doc["mqtt_status"]       = "connecting";
    doc["mqtt_error"]        = "";
    doc["mqtt_error_detail"] = "";
    web_server_event(doc);
  }

  MicroTask.wakeTask(this);
}

void Mqtt::publishData(JsonDocument &data) {
  if (!config_mqtt_enabled() || !_mqttclient.connected()) {
    return;
  }
  JsonObject root = data.as<JsonObject>();
  bool published = false;
  for (JsonPair kv : root) {
    String topic = mqtt_topic + "/" + kv.key().c_str();
    String val = kv.value().as<String>(); // Consider non-string values too if needed
    _mqttclient.publish(topic, val, config_mqtt_retained());
    published = true;
  }
  // "Last message" tracks any broker traffic — reset on send as well as receive
  if (published) {
    _lastRxTime = time(NULL);
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
