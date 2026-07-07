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
  const size_t capacity = JSON_OBJECT_SIZE(128) + 1024;
  DynamicJsonDocument doc(capacity);

  config_serialize(doc, true, false, true);

  response->setCode(200);
  serializeJson(doc, *response);
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
    // If this device is a member, check if this is a controller config push
    // or a local request trying to change load sharing fields
    if (loadSharingGroupState.isMember()) {
      bool isControllerPush = doc.containsKey("loadsharing_role") &&
                              doc["loadsharing_role"].as<String>() == "member";
      if (!isControllerPush) {
        // Strip out any loadsharing fields from the request - members can't change them locally
        JsonObject obj = doc.as<JsonObject>();
        std::vector<String> keysToRemove;
        for (JsonObject::iterator it = obj.begin(); it != obj.end(); ++it) {
          String key = it->key().c_str();
          if (key.startsWith("loadsharing_")) {
            keysToRemove.push_back(key);
          }
        }
        for (const auto& key : keysToRemove) {
          obj.remove(key);
        }
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

    // Role transitions are applied after validation so that a rejected
    // request does not mutate group-membership state as a side effect.
    if (doc.containsKey("loadsharing_role") &&
        doc["loadsharing_role"].as<String>() == "member" &&
        doc.containsKey("loadsharing_controller_host")) {
      String controllerHost = doc["loadsharing_controller_host"].as<String>();
      loadSharingGroupState.becomeMember(controllerHost);
    }
    if (doc.containsKey("loadsharing_role") &&
        doc["loadsharing_role"].as<String>() == "" &&
        loadSharingGroupState.isMember()) {
      loadSharingGroupState.resetRole();
    }

    // Update WiFi module config
    MongooseString storage = request->headers("X-Storage");
    if(storage.equals("factory") && config_factory_write_lock())
    {
      response->setCode(423);
      response->print("{\"msg\":\"Factory settings locked\"}");
      return;
    }

    bool config_modified = web_server_config_deserialise(doc, storage.equals("factory"));

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
