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

// -------------------------------------------------------------------
// Returns OpenEVSE Config json
// url: /config
// -------------------------------------------------------------------
void
handleConfigGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  const size_t capacity = JSON_OBJECT_SIZE(43) + 1024;
  DynamicJsonDocument doc(capacity);

  // EVSE Config
  doc["firmware"] = evse.getFirmwareVersion();
  doc["protocol"] = "-";
  doc["espflash"] = ESPAL.getFlashChipSize();
  doc["espinfo"] = ESPAL.getChipInfo();
  doc["buildenv"] = buildenv;
  doc["version"] = currentfirmware;
  doc["diodet"] = evse.isDiodeCheckDisabled() ? 1 : 0;
  doc["gfcit"] = evse.isGfiTestDisabled() ? 1 : 0;
  doc["groundt"] = evse.isGroundCheckDisabled() ? 1 : 0;
  doc["relayt"] = evse.isStuckRelayCheckDisabled() ? 1 : 0;
  doc["ventt"] = evse.isVentRequiredDisabled() ? 1 : 0;
  doc["tempt"] = evse.isTemperatureCheckDisabled() ? 1 : 0;
  doc["service"] = static_cast<uint8_t>(evse.getServiceLevel());
  doc["scale"] = current_scale;
  doc["offset"] = current_offset;

  // Static supported protocols
  JsonArray mqtt_supported_protocols = doc.createNestedArray("mqtt_supported_protocols");
  mqtt_supported_protocols.add("mqtt");
  mqtt_supported_protocols.add("mqtts");

  JsonArray http_supported_protocols = doc.createNestedArray("http_supported_protocols");
  http_supported_protocols.add("http");
  http_supported_protocols.add("https");

  config_serialize(doc, true, false, true);

  response->setCode(200);
  serializeJson(doc, *response);
}

void
handleConfigPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();
  if(config_deserialize(body)) {
    config_commit();
    response->setCode(200);
    response->print("{\"msg\":\"done\"}");
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"Could not parse JSON\"}");
  }
}

void
handleConfig(MongooseHttpServerRequest *request)
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
