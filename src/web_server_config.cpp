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

  // Read only information
  doc["firmware"] = evse.getFirmwareVersion();
  doc["protocol"] = "-";
  doc["espflash"] = ESPAL.getFlashChipSize();
  doc["espinfo"] = ESPAL.getChipInfo();
  doc["buildenv"] = buildenv;
  doc["version"] = currentfirmware;

  // Static supported protocols
  JsonArray mqtt_supported_protocols = doc.createNestedArray("mqtt_supported_protocols");
  mqtt_supported_protocols.add("mqtt");
  mqtt_supported_protocols.add("mqtts");

  JsonArray http_supported_protocols = doc.createNestedArray("http_supported_protocols");
  http_supported_protocols.add("http");
  http_supported_protocols.add("https");

  // OpenEVSE module config
  doc["diode_check_disabled"] = evse.isDiodeCheckDisabled();
  doc["gfci_check_disabled"] = evse.isGfiTestDisabled();
  doc["ground_check_disabled"] = evse.isGroundCheckDisabled();
  doc["relay_check_disabled"] = evse.isStuckRelayCheckDisabled();
  doc["vent_check_disabled"] = evse.isVentRequiredDisabled();
  doc["temp_check_disabled"] = evse.isTemperatureCheckDisabled();
  doc["service"] = static_cast<uint8_t>(evse.getServiceLevel());
  doc["scale"] = evse.getCurrentSensorScale();
  doc["offset"] = evse.getCurrentSensorOffset();
  doc["max_current_soft"] = evse.getMaxConfiguredCurrent();

  doc["min_current_hard"] = evse.getMinCurrent();
  doc["max_current_hard"] = evse.getMaxHardwareCurrent();

  // WiFi module config
  config_serialize(doc, true, false, true);

  response->setCode(200);
  serializeJson(doc, *response);
}

void
handleConfigPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();

  // Deserialize the JSON document
  const size_t capacity = JSON_OBJECT_SIZE(50) + 1024;
  DynamicJsonDocument doc(capacity);
  DeserializationError error = deserializeJson(doc, body);
  if(!error)
  {
    // Update WiFi module config
    if(config_deserialize(body)) {
      config_commit();
    }

    // Update EVSE config
    // Update the EVSE setting flags, a little low level, may move later
    if(doc.containsKey("diode_check_disabled")) {
      evse.enableDiodeCheck(doc["diode_check_disabled"]);
    }
    if(doc.containsKey("gfci_check_disabled")) {
      evse.enableGfiTestCheck(doc["gfci_check_disabled"]);
    }
    if(doc.containsKey("ground_check_disabled")) {
      evse.enableGroundCheck(doc["ground_check_disabled"]);
    }
    if(doc.containsKey("relay_check_disabled")) {
      evse.enableStuckRelayCheck(doc["relay_check_disabled"]);
    }
    if(doc.containsKey("vent_check_disabled")) {
      evse.enableVentRequired(doc["vent_check_disabled"]);
    }
    if(doc.containsKey("temp_check_disabled")) {
      evse.enableTemperatureCheck(doc["temp_check_disabled"]);
    }
    if(doc.containsKey("service"))
    {
      int service = doc["service"];
      evse.setServiceLevel(static_cast<EvseMonitor::ServiceLevel>(service));
    }
    if(doc.containsKey("scale") && doc.containsKey("offset")) {
      evse.configureCurrentSensorScale(doc["scale"], doc["offset"]);
    }
    if(doc.containsKey("max_current_soft")) {
      evse.setMaxConfiguredCurrent(doc["max_current_soft"]);
    }

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
