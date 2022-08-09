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

uint32_t config_version = 0;

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
  doc["evse_serial"] = evse.getSerial();
  doc["wifi_serial"] = serial;

  // Static supported protocols
  JsonArray mqtt_supported_protocols = doc.createNestedArray("mqtt_supported_protocols");
  mqtt_supported_protocols.add("mqtt");
  mqtt_supported_protocols.add("mqtts");

  JsonArray http_supported_protocols = doc.createNestedArray("http_supported_protocols");
  http_supported_protocols.add("http");
  http_supported_protocols.add("https");

  // OpenEVSE module config
  doc["diode_check"] = evse.isDiodeCheckEnabled();
  doc["gfci_check"] = evse.isGfiTestEnabled();
  doc["ground_check"] = evse.isGroundCheckEnabled();
  doc["relay_check"] = evse.isStuckRelayCheckEnabled();
  doc["vent_check"] = evse.isVentRequiredEnabled();
  doc["temp_check"] = evse.isTemperatureCheckEnabled();
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
    bool config_modified = false;

    // Update WiFi module config
    if(config_deserialize(doc)) {
      config_commit();
      config_modified = true;
      DBUGLN("Config updated");
    }

    // Update EVSE config
    // Update the EVSE setting flags, a little low level, may move later
    if(doc.containsKey("diode_check"))
    {
      bool enable = doc["diode_check"];
      if(enable != evse.isDiodeCheckEnabled()) {
        evse.enableDiodeCheck(enable);
        config_modified = true;
        DBUGLN("diode_check changed");
      }
    }
    if(doc.containsKey("gfci_check"))
    {
      bool enable = doc["gfci_check"];
      if(enable != evse.isGfiTestEnabled()) {
        evse.enableGfiTestCheck(enable);
        config_modified = true;
        DBUGLN("gfci_check changed");
      }
    }
    if(doc.containsKey("ground_check"))
    {
      bool enable = doc["ground_check"];
      if(enable != evse.isGroundCheckEnabled()) {
        evse.enableGroundCheck(enable);
        config_modified = true;
        DBUGLN("ground_check changed");
      }
    }
    if(doc.containsKey("relay_check"))
    {
      bool enable = doc["relay_check"];
      if(enable != evse.isStuckRelayCheckEnabled()) {
        evse.enableStuckRelayCheck(enable);
        config_modified = true;
        DBUGLN("relay_check changed");
      }
    }
    if(doc.containsKey("vent_check"))
    {
      bool enable = doc["vent_check"];
      if(enable != evse.isVentRequiredEnabled()) {
        evse.enableVentRequired(enable);
        config_modified = true;
        DBUGLN("vent_check changed");
      }
    }
    if(doc.containsKey("temp_check"))
    {
      bool enable = doc["temp_check"];
      if(enable != evse.isTemperatureCheckEnabled()) {
        evse.enableTemperatureCheck(enable);
        config_modified = true;
        DBUGLN("temp_check changed");
      }
    }
    if(doc.containsKey("service"))
    {
      EvseMonitor::ServiceLevel service = static_cast<EvseMonitor::ServiceLevel>(doc["service"].as<uint8_t>());
      if(service != evse.getServiceLevel()) {
        evse.setServiceLevel(service);
        config_modified = true;
        DBUGLN("service changed");
      }
    }
    if(doc.containsKey("max_current_soft"))
    {
      long current = doc["max_current_soft"];
      if(current != evse.getMaxConfiguredCurrent()) {
        evse.setMaxConfiguredCurrent(current);
        config_modified = true;
        DBUGLN("max_current_soft changed");
      }
    }
    if(doc.containsKey("scale") && doc.containsKey("offset"))
    {
      long scale = doc["scale"];
      long offset = doc["offset"];
      if(scale != evse.getCurrentSensorScale() || offset != evse.getCurrentSensorOffset()) {
        evse.configureCurrentSensorScale(doc["scale"], doc["offset"]);
        config_modified = true;
        DBUGLN("scale changed");
      }
    }

    StaticJsonDocument<128> doc;

    if(config_modified)
    {
      // HACK: force a flush of the RAPI command queue to make sure the config
      //       is updated before we send the response
      DBUG("Flushing RAPI command queue ...");
      rapiSender.flush();
      DBUGLN(" Done");
      config_version++;
      DBUGVAR(config_version);
    }

    doc["config_version"] = config_version;

    if(config_modified) {
      event_send(doc);
    }

    doc["msg"] = config_modified ? "done" : "no change";

    response->setCode(200);
    serializeJson(doc, *response);
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
