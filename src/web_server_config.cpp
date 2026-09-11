#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

typedef const __FlashStringHelper *fstr_t;

#include "emonesp.h"
#include "web_server.h"
#include "app_config.h"
#include "espal.h"
#include "input.h"
#include "event.h"
#include "loadsharing_peer_poller.h"
#include "loadsharing_types.h"
#include <vector>

extern bool isPositive(MongooseHttpServerRequest *request, const char *param);
extern bool web_server_config_deserialise(DynamicJsonDocument &doc, bool factory);

// -------------------------------------------------------------------
// Returns OpenEVSE Config json
// url: /config
// -------------------------------------------------------------------
void
handleConfigGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  // Allocated once and reused -- same reasoning as handleStatus. Measured on
  // hardware, sustained polling of /config drove the largest allocatable block
  // from 53,236 down to 32,756 and it did not recover, while total free heap
  // stayed above 70KB. Safe as a static because handlers run to completion on
  // the single task that polls Mongoose.
  //
  // Capacity headroom: JSON_OBJECT_SIZE(128) is a sizing hint, not a hard
  // member cap -- ArduinoJson only cares about total bytes, and a live TFT
  // unit already serves ~135 members (~446 bytes of string pool) within this
  // budget. The relay_health block added here (relay_life_pct and ~10
  // siblings) still fits, but there isn't much room left for the next
  // addition -- worth rechecking on hardware (or just bumping the constant)
  // before adding more.
  static DynamicJsonDocument doc(JSON_OBJECT_SIZE(128) + 1024);
  doc.clear();

  config_serialize(doc, true, false, true);

  response->setCode(200);
  // One exact-sized buffer and a single write -- see handleStatus for why
  // incremental writes into the response stream fragment the heap.
  String json;
  json.reserve(measureJson(doc) + 1);
  serializeJson(doc, json);
  response->write((const uint8_t *)json.c_str(), json.length());
}

void
handleConfigPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  MongooseString body = request->body();

  // Deserialize the JSON document
  const size_t capacity = JSON_OBJECT_SIZE(128) + 1024;
  DynamicJsonDocument doc(capacity);
  DeserializationError error = deserializeJson(doc, body.c_str(), body.length());
  if(!error)
  {
    bool loadsharingConfigRequest = false;
    for (JsonPairConst field : doc.as<JsonObjectConst>()) {
      if (String(field.key().c_str()).startsWith("loadsharing_")) {
        loadsharingConfigRequest = true;
        break;
      }
    }

    // If this device is a member, check if this is a controller config push
    // or a local request trying to change load sharing fields
    if (loadSharingGroupState.isMember()) {
      bool isControllerPush = doc.containsKey("loadsharing_role") &&
                              (doc["loadsharing_role"].as<String>() == "member" ||
                               doc["loadsharing_role"].as<String>() == "");
      if (loadsharingConfigRequest && !isControllerPush) {
        response->setCode(403);
        response->print("{\"msg\":\"Load sharing configuration is read-only on members\"}");
        return;
      }
    }

    // Validate load sharing config ranges
    if (doc.containsKey("loadsharing_group_max_current")) {
      double val = doc["loadsharing_group_max_current"].as<double>();
      if (val < 0) {
        response->setCode(400);
        response->print("{\"msg\":\"loadsharing_group_max_current must be >= 0\"}");
        return;
      }
    }
    if (doc.containsKey("loadsharing_safety_factor")) {
      double val = doc["loadsharing_safety_factor"].as<double>();
      if (val < 0.0 || val > 1.0) {
        response->setCode(400);
        response->print("{\"msg\":\"loadsharing_safety_factor must be between 0.0 and 1.0\"}");
        return;
      }
    }
    if (doc.containsKey("loadsharing_heartbeat_timeout")) {
      uint32_t val = doc["loadsharing_heartbeat_timeout"].as<uint32_t>();
      if (val < 5 || val > 600) {
        response->setCode(400);
        response->print("{\"msg\":\"loadsharing_heartbeat_timeout must be between 5 and 600 seconds\"}");
        return;
      }
    }
    if (doc.containsKey("loadsharing_failsafe_safe_current")) {
      double val = doc["loadsharing_failsafe_safe_current"].as<double>();
      if (val < 0 || val > 80) {
        response->setCode(400);
        response->print("{\"msg\":\"loadsharing_failsafe_safe_current must be between 0 and 80 amps\"}");
        return;
      }
    }
    if (doc.containsKey("loadsharing_failsafe_peer_assumed_current")) {
      double val = doc["loadsharing_failsafe_peer_assumed_current"].as<double>();
      if (val < 0 || val > 80) {
        response->setCode(400);
        response->print("{\"msg\":\"loadsharing_failsafe_peer_assumed_current must be between 0 and 80 amps\"}");
        return;
      }
    }
    if (doc.containsKey("loadsharing_failsafe_mode")) {
      String val = doc["loadsharing_failsafe_mode"].as<String>();
      if (val != "safe_current" && val != "disable") {
        response->setCode(400);
        response->print("{\"msg\":\"loadsharing_failsafe_mode must be 'safe_current' or 'disable'\"}");
        return;
      }
    }
    // Cross-field: a member's failsafe current must fit inside the group
    // budget, otherwise a single islanded member can exceed the group max
    // on its own. Use incoming values when present, stored values otherwise.
    {
      double failsafe = doc.containsKey("loadsharing_failsafe_safe_current")
          ? doc["loadsharing_failsafe_safe_current"].as<double>()
          : loadsharing_failsafe_safe_current;
      double groupMax = doc.containsKey("loadsharing_group_max_current")
          ? doc["loadsharing_group_max_current"].as<double>()
          : loadsharing_group_max_current;
      if (groupMax > 0 && failsafe > groupMax) {
        response->setCode(400);
        response->print("{\"msg\":\"loadsharing_failsafe_safe_current must not exceed loadsharing_group_max_current\"}");
        return;
      }
    }

    // Update WiFi module config
    MongooseString storage = request->headers("X-Storage");
    if(storage.equals("factory") && config_factory_write_lock())
    {
      response->setCode(423);
      response->print("{\"msg\":\"Factory settings locked\"}");
      return;
    }

    // Role transitions are applied after every check that can reject the
    // request -- both the validation above and the factory write lock -- so a
    // rejected request never mutates group-membership state as a side effect.
    // resetRole() in particular also drops the controller peer and rewrites the
    // persisted peer list, which a 423 response must not leave behind.
    if (doc.containsKey("loadsharing_role") &&
        doc["loadsharing_role"].as<String>() == "member" &&
        doc.containsKey("loadsharing_controller_host")) {
      String controllerHost = doc["loadsharing_controller_host"].as<String>();
      loadSharingGroupState.becomeMember(controllerHost);
    }
    if (doc.containsKey("loadsharing_role") &&
        doc["loadsharing_role"].as<String>() == "" &&
        loadSharingGroupState.isMember()) {
      // Drop the controller entry structurally rather than by
      // loadsharing_controller_host: discovery may have re-keyed it under the
      // mDNS hostname the controller advertises, in which case removing it by
      // the configured spelling silently does nothing and the stale peer is
      // left behind.
      loadSharingGroupState.removeSoleRemoteGroupPeer();
      loadSharingGroupState.resetRole();
    }

    bool config_modified = web_server_config_deserialise(doc, storage.equals("factory"));
    if (config_modified && loadsharingConfigRequest &&
        loadSharingGroupState.isController()) {
      loadSharingPeerPoller.pushConfigToAllPeers();
    }

    StaticJsonDocument<128> reply;
    reply["config_version"] = config_version();
    reply["msg"] = config_modified ? "done" : "no change";

    response->setCode(200);
    serializeJson(reply, *response);
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"Could not parse JSON\"}");
  }
}

void handleConfig(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {
    handleConfigGet(request, response);
  } else if(HTTP_POST == request->method()) {
    handleConfigPost(request, response);
  } else if(HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

bool web_server_config_deserialise(DynamicJsonDocument &doc, bool factory)
{
  bool config_modified = config_deserialize(doc);

  if(config_modified)
  {
    config_commit(factory);
    DBUGLN("Config updated");
  }

  return config_modified;
}
