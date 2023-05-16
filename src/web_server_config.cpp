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
