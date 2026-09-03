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
