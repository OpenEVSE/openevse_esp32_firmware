#include "cli_runningconfig.h"
#include "../app_config.h"
#include "../input.h" // extern EvseManager evse;
#include <ArduinoJson.h>

static void featureLine(CliOutput &out, const char *name, bool enabled)
{
  out.printf("feature %s %s\r\n", name, enabled ? "enabled" : "disabled");
}

void buildRunningConfig(CliOutput &out)
{
  // Same capacity/call shape as the existing GET /config REST handler
  // (web_server_config.cpp:handleConfigGet) so this stays consistent with
  // whatever that endpoint already exposes (and hides).
  const size_t capacity = JSON_OBJECT_SIZE(128) + 1024;
  DynamicJsonDocument doc(capacity);
  config_serialize(doc, true, false, true /* hideSecrets */);

  out.println("!");
  if(doc.containsKey("hostname")) {
    out.printf("hostname %s\r\n", doc["hostname"].as<const char*>());
  }
  out.println("!");
  out.println("interface wifi");
  if(doc.containsKey("ssid")) {
    out.printf(" ssid \"%s\"\r\n", doc["ssid"].as<const char*>());
  }
  out.println("!");

  if(doc["mqtt_enabled"].as<bool>()) {
    out.println("service mqtt enabled");
    if(doc.containsKey("mqtt_server")) {
      out.printf(" mqtt server %s\r\n", doc["mqtt_server"].as<const char*>());
    }
    if(doc.containsKey("mqtt_port")) {
      out.printf(" mqtt port %u\r\n", doc["mqtt_port"].as<unsigned>());
    }
  } else {
    out.println("service mqtt disabled");
  }
  out.println("!");

  out.printf("service ocpp %s\r\n", doc["ocpp_enabled"].as<bool>() ? "enabled" : "disabled");
  out.println("!");

#ifdef ENABLE_SSH_CLI
  out.printf("service ssh %s\r\n", doc["ssh_enabled"].as<bool>() ? "enabled" : "disabled");
  if(doc.containsKey("ssh_username")) {
    out.printf(" ssh username %s\r\n", doc["ssh_username"].as<const char*>());
  }
  out.println("!");
#endif

  // EVSE-side settings — already-cached local state from the continuous
  // background RAPI polling loop, so no fresh synchronous RAPI round-trip
  // is issued here.
  out.printf("service-level %s\r\n",
    EvseMonitor::ServiceLevel::L1 == evse.getServiceLevel() ? "1" :
    EvseMonitor::ServiceLevel::L2 == evse.getServiceLevel() ? "2" : "auto");
  out.printf("current-capacity %ld\r\n", evse.getMaxConfiguredCurrent());
  out.println("!");

  featureLine(out, "gfi-test", evse.isGfiTestEnabled());
  featureLine(out, "diode-check", evse.isDiodeCheckEnabled());
  featureLine(out, "ground-check", evse.isGroundCheckEnabled());
  featureLine(out, "stuck-relay-check", evse.isStuckRelayCheckEnabled());
  featureLine(out, "vent-required", evse.isVentRequiredEnabled());
  featureLine(out, "temperature-check", evse.isTemperatureCheckEnabled());
  featureLine(out, "front-button", evse.isFrontButtonEnabled());
  featureLine(out, "boot-lock", evse.isBootLockEnabled());
  out.println("end");
}
