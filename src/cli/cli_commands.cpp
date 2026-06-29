#include "cli_commands.h"
#include "cli_runningconfig.h"
#include "../app_config.h"
#include "../input.h"     // extern EvseManager evse;
#include "../manual.h"    // extern ManualOverride manual;
#include "../net_manager.h" // extern NetManagerTask net;
#include "../scheduler.h" // extern Scheduler scheduler;
#include "../mqtt.h"      // extern Mqtt mqtt;
#include "../time_man.h"  // extern TimeManager timeManager; time_format_time()
#include "../temp_throttle.h" // extern TempThrottleTask tempThrottle;
#include "../http_update.h"   // http_update_from_url()
#include "../debug.h"     // DEBUG_PORT
#include "../emonesp.h"   // currentfirmware, buildenv, restart_system()
#include "../energy_logger.h" // extern EnergyLogger energyLogger;
#include "../limit.h"     // extern Limit limit;
#include "../rfid.h"      // extern RfidTask rfid;
#include "../tesla_client.h" // extern TeslaClient teslaClient;
#include <espal.h>        // ESPAL
#include <WiFi.h>         // WiFi.RSSI(), WiFi.scanNetworks()
#include <ArduinoJson.h>
#include <StreamSpy.h>    // SerialEvse's type
#include <time.h>         // mktime()

#ifndef ENABLE_TSDB
extern EnergyLogger energyLogger;
#endif

// Declared (and instantiated) in debug.cpp: SerialEvse wraps RAPI_PORT and
// already captures all RAPI traffic into a 2KB ring buffer for the existing
// /evse web debug endpoint. We intentionally only ever call printBuffer() on
// it (a non-destructive snapshot read) — never onRead()/onWrite(), since
// those are single-subscriber callback slots already claimed by the
// /evse/console websocket (web_server.cpp); taking them over here would
// silently break that feature instead of giving us a live tail.
extern StreamSpy SerialEvse;

static bool argIs(const char *s, const char *word)
{
  return s && 0 == strcasecmp(s, word);
}

// Friendly names for EvseClient_OpenEVSE_* constants — used by "show claims"
// and as the lookup for "set claims release <name>".
struct ClaimClientName { const char *name; EvseClient client; };
static const ClaimClientName claimClientNames[] = {
  { "manual",       EvseClient_OpenEVSE_Manual },
  { "divert",       EvseClient_OpenEVSE_Divert },
  { "boost",        EvseClient_OpenEVSE_Boost },
  { "schedule",     EvseClient_OpenEVSE_Schedule },
  { "limit",        EvseClient_OpenEVSE_Limit },
  { "error",        EvseClient_OpenEVSE_Error },
  { "ohm",          EvseClient_OpenEVSE_Ohm },
  { "ocpp",         EvseClient_OpenEVSE_OCPP },
  { "rfid",         EvseClient_OpenEVSE_RFID },
  { "mqtt",         EvseClient_OpenEVSE_MQTT },
  { "shaper",       EvseClient_OpenEVSE_Shaper },
  { "tempthrottle", EvseClient_OpenEVSE_TempThrottle },
};
static const uint8_t claimClientNameCount = sizeof(claimClientNames) / sizeof(claimClientNames[0]);

// Shared by "show sensors" and "show temperature".
static const char *temperatureSensorNames[EVSE_MONITOR_TEMP_COUNT] = {
  "monitor", "max", "evse-ds3232", "evse-mcp9808", "evse-tmp007", "esp-mcp9808"
};

static const char *claimClientToName(uint32_t client)
{
  for(uint8_t i = 0; i < claimClientNameCount; i++) {
    if(claimClientNames[i].client == client) return claimClientNames[i].name;
  }
  return nullptr;
}

void cmd_show_running_config(CliOutput &out, int argc, const char *argv[])
{
  buildRunningConfig(out);
}

void cmd_show_version(CliOutput &out, int argc, const char *argv[])
{
  out.printf("OpenEVSE WiFi firmware %s\r\n", currentfirmware.c_str());
  out.printf("Build env  : %s\r\n", buildenv.c_str());
  out.printf("Hostname   : %s\r\n", esp_hostname.c_str());
  out.printf("Free heap  : %u bytes\r\n", (unsigned)ESPAL.getFreeHeap());
}

void cmd_show_status(CliOutput &out, int argc, const char *argv[])
{
  out.printf("EVSE state: %s\r\n", evse.getState().toString());
  out.printf("Service level: %s\r\n",
    EvseMonitor::ServiceLevel::L1 == evse.getServiceLevel() ? "L1" :
    EvseMonitor::ServiceLevel::L2 == evse.getServiceLevel() ? "L2" : "Auto");
  out.printf("Current capacity: %ld A\r\n", evse.getMaxConfiguredCurrent());
  out.printf("WiFi: %s, ip %s\r\n",
    net.isWifiClientConnected() ? "connected" : "disconnected",
    net.getIp().c_str());
}

void cmd_show_faults(CliOutput &out, int argc, const char *argv[])
{
  out.printf("GFCI trips: %ld\r\n", evse.getFaultCountGFCI());
  out.printf("No-ground trips: %ld\r\n", evse.getFaultCountNoGround());
  out.printf("Stuck-relay trips: %ld\r\n", evse.getFaultCountStuckRelay());
}

void cmd_show_schedule(CliOutput &out, int argc, const char *argv[])
{
  const size_t capacity = JSON_ARRAY_SIZE(SCHEDULER_MAX_EVENTS) + SCHEDULER_MAX_EVENTS * JSON_OBJECT_SIZE(4) + 1024;
  DynamicJsonDocument doc(capacity);
  scheduler.serialize(doc);

  JsonArray events = doc.as<JsonArray>();
  if(0 == events.size()) {
    out.println("% No schedule events configured");
    return;
  }

  for(JsonObject event : events) {
    String days;
    for(JsonVariant d : event["days"].as<JsonArray>()) {
      if(days.length()) days += ",";
      days += d.as<const char*>();
    }
    out.printf("event %lu: %s at %s on %s\r\n",
      (unsigned long)event["id"].as<uint32_t>(),
      event["state"].as<const char*>(),
      event["time"].as<const char*>(),
      days.c_str());
  }
}

void cmd_show_claims(CliOutput &out, int argc, const char *argv[])
{
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument doc(capacity);
  if(!evse.serializeClaims(doc) || 0 == doc.as<JsonArray>().size()) {
    out.println("% No active claims");
    return;
  }

  for(JsonObject claim : doc.as<JsonArray>()) {
    uint32_t client = claim["client"].as<uint32_t>();
    const char *name = claimClientToName(client);
    out.printf("claim %s (0x%08lX):", name ? name : "unknown", (unsigned long)client);
    if(claim.containsKey("state")) out.printf(" state=%s", claim["state"].as<const char*>());
    if(claim.containsKey("charge_current")) out.printf(" charge_current=%lu", (unsigned long)claim["charge_current"].as<uint32_t>());
    if(claim.containsKey("max_current")) out.printf(" max_current=%lu", (unsigned long)claim["max_current"].as<uint32_t>());
    out.printf(" auto_release=%s\r\n", claim["auto_release"].as<bool>() ? "true" : "false");
  }
}

void cmd_show_ntp(CliOutput &out, int argc, const char *argv[])
{
  out.printf("NTP: %s\r\n", timeManager.isSntpEnabled() ? "enabled" : "disabled");
  out.printf("Server: %s\r\n", sntp_hostname.c_str());
  out.printf("Status: %s\r\n", timeManager.getNtpStatus());
  out.printf("Resolved IP: %s\r\n", timeManager.getResolvedIp());
  time_t last = timeManager.getLastSyncTime();
  out.printf("Last sync: %s\r\n", last ? time_format_time(last).c_str() : "never");
}

void cmd_show_mqtt(CliOutput &out, int argc, const char *argv[])
{
  out.printf("MQTT: %s\r\n", config_mqtt_enabled() ? "enabled" : "disabled");
  out.printf("Server: %s:%u\r\n", mqtt_server.c_str(), (unsigned)mqtt_port);
  out.printf("Status: %s\r\n", mqtt.getMqttStatus());
  out.printf("Broker IP: %s\r\n", mqtt.getBrokerIp());
  out.printf("Broker version: %s\r\n", mqtt.getBrokerVersion());
  if(mqtt.isConnected()) {
    out.printf("Connected since: %s\r\n", time_format_time(mqtt.getConnectedSince()).c_str());
    out.printf("Last message: %s\r\n", time_format_time(mqtt.getLastRxTime()).c_str());
  } else if(strlen(mqtt.getErrorCategory()) > 0) {
    out.printf("Last error: %s (%s)\r\n", mqtt.getErrorCategory(), mqtt.getErrorDetail());
  }
}

void cmd_show_interface_wifi0(CliOutput &out, int argc, const char *argv[])
{
  out.printf("wifi0 is %s\r\n", net.isWifiClientConnected() ? "up" : "down");
  out.printf("  configured ssid: %s\r\n", esid.c_str());
  out.printf("  ip address: %s\r\n", net.getIp().c_str());
  out.printf("  mac address: %s\r\n", net.getMac().c_str());
  if(net.isWifiClientConnected()) {
    out.printf("  signal strength: %d dBm\r\n", (int)WiFi.RSSI());
  }
}

// Mimics Cisco IOS "show vlan" formatting. This hardware has no real VLAN
// database — there's exactly one interface (the WiFi station) and no
// tagging — so we report it as a single static entry in VLAN 0 (the
// untagged/native placeholder) rather than Cisco's usual VLAN 1.
void cmd_show_vlan(CliOutput &out, int argc, const char *argv[])
{
  out.println("VLAN Name                             Status    Ports");
  out.println("---- -------------------------------- --------- -------------------------------");
  out.printf("%-4d %-32s %-9s %s\r\n",
    0, "default",
    net.isWifiClientConnected() ? "active" : "down",
    "Wifi0");
}

void cmd_show_hardware(CliOutput &out, int argc, const char *argv[])
{
  out.printf("Chip: %s\r\n", ESPAL.getChipInfo().c_str());
  out.printf("Flash size: %lu bytes\r\n", (unsigned long)ESPAL.getFlashChipSize());
  out.printf("Free heap: %lu bytes\r\n", (unsigned long)ESPAL.getFreeHeap());
  out.printf("Device id: %s (%s)\r\n", ESPAL.getShortId().c_str(), ESPAL.getLongId().c_str());
  out.printf("EVSE firmware: %s\r\n", evse.getFirmwareVersion());
  out.printf("EVSE serial: %s\r\n", evse.getSerial());
  out.printf("Ammeter scale/offset: %ld / %ld\r\n", evse.getCurrentSensorScale(), evse.getCurrentSensorOffset());
  out.printf("Relays: DC1=%s DC2=%s AC=%s\r\n",
    evse.isDC1RelayEnabled() ? "enabled" : "disabled",
    evse.isDC2RelayEnabled() ? "enabled" : "disabled",
    evse.isACRelayEnabled() ? "enabled" : "disabled");
}

void cmd_show_sensors(CliOutput &out, int argc, const char *argv[])
{
  out.printf("Voltage: %.1f V\r\n", evse.getVoltage());
  uint32_t freq = evse.getFrequency();
  if(freq) {
    out.printf("Frequency: %.2f Hz\r\n", freq / 100.0);
  } else {
    out.println("Frequency: unknown");
  }

  bool any = false;
  for(uint8_t i = 0; i < EVSE_MONITOR_TEMP_COUNT; i++) {
    if(evse.isTemperatureValid(i)) {
      out.printf("Temperature (%s): %.1f C\r\n", temperatureSensorNames[i], evse.getTemperature(i));
      any = true;
    }
  }
  if(!any) {
    out.println("Temperature: no valid sensors reporting");
  }
}

void cmd_show_temperature(CliOutput &out, int argc, const char *argv[])
{
  bool any = false;
  for(uint8_t i = 0; i < EVSE_MONITOR_TEMP_COUNT; i++) {
    if(evse.isTemperatureValid(i)) {
      out.printf("Temperature (%s): %.1f C\r\n", temperatureSensorNames[i], evse.getTemperature(i));
      any = true;
    }
  }
  if(!any) {
    out.println("Temperature: no valid sensors reporting");
  }

  out.printf("Shutdown setpoint: %lu C\r\n", (unsigned long)evse.getPanicTemperature());
  out.printf("Throttle: %s, setpoint %lu C, currently %s\r\n",
    config_temp_throttle_enabled() ? "enabled" : "disabled",
    (unsigned long)temp_throttle_setpoint,
    tempThrottle.isThrottling() ? "throttling" : "not throttling");
}

void cmd_show_energy(CliOutput &out, int argc, const char *argv[])
{
  out.printf("Session: %.0f Wh\r\n", evse.getSessionEnergy());
  out.printf("Today: %.3f kWh\r\n", evse.getTotalDay());
  out.printf("Week: %.3f kWh\r\n", evse.getTotalWeek());
  out.printf("Month: %.3f kWh\r\n", evse.getTotalMonth());
  out.printf("Year: %.3f kWh\r\n", evse.getTotalYear());
  out.printf("Total: %.3f kWh\r\n", evse.getTotalEnergy());
}

void cmd_show_mqtt_topics(CliOutput &out, int argc, const char *argv[])
{
  const char *base = mqtt_topic.c_str();
  out.printf("Base topic: %s\r\n", base);
  out.println("Published (retained):");
  out.printf("  %s/config\r\n", base);
  out.printf("  %s/config_version\r\n", base);
  out.printf("  %s/claim\r\n", base);
  out.printf("  %s/override\r\n", base);
  out.printf("  %s/schedule\r\n", base);
  out.printf("  %s/limit\r\n", base);
  out.printf("Published (event data, one topic per key): %s/<key>\r\n", base);
  out.println("Subscribed (control):");
  out.printf("  %s/divertmode/set\r\n", base);
  out.printf("  %s/shaper/set\r\n", base);
  out.printf("  %s/override/set\r\n", base);
  out.printf("  %s/claim/set\r\n", base);
  out.printf("  %s/schedule/set\r\n", base);
  out.printf("  %s/schedule/clear\r\n", base);
  out.printf("  %s/limit/set\r\n", base);
  out.printf("  %s/config/set\r\n", base);
  out.printf("  %s/restart\r\n", base);

  out.println("Configured external input topics:");
  if(mqtt_solar.length())               out.printf("  solar: %s\r\n", mqtt_solar.c_str());
  if(mqtt_grid_ie.length())             out.printf("  grid import/export: %s\r\n", mqtt_grid_ie.c_str());
  if(mqtt_vrms.length())                out.printf("  vrms: %s\r\n", mqtt_vrms.c_str());
  if(mqtt_live_pwr.length())            out.printf("  live power: %s\r\n", mqtt_live_pwr.c_str());
  if(mqtt_vehicle_soc.length())         out.printf("  vehicle soc: %s\r\n", mqtt_vehicle_soc.c_str());
  if(mqtt_vehicle_range.length())       out.printf("  vehicle range: %s\r\n", mqtt_vehicle_range.c_str());
  if(mqtt_vehicle_eta.length())         out.printf("  vehicle eta: %s\r\n", mqtt_vehicle_eta.c_str());
  if(mqtt_vehicle_charge_limit.length())out.printf("  vehicle charge limit: %s\r\n", mqtt_vehicle_charge_limit.c_str());
  if(mqtt_home_battery_soc.length())    out.printf("  home battery soc: %s\r\n", mqtt_home_battery_soc.c_str());
  if(mqtt_home_battery_power.length())  out.printf("  home battery power: %s\r\n", mqtt_home_battery_power.c_str());
  if(mqtt_announce_topic.length())      out.printf("  announce: %s\r\n", mqtt_announce_topic.c_str());
}

namespace {
  class CliBufferCapture : public Print {
    public:
      String buf;
      size_t write(uint8_t c) override { buf += (char)c; return 1; }
      size_t write(const uint8_t *b, size_t n) override { buf.concat((const char*)b, n); return n; }
  };
}

void cmd_debug_rapi(CliOutput &out, int argc, const char *argv[])
{
  CliBufferCapture cap;
  SerialEvse.printBuffer(cap);
  if(0 == cap.buf.length()) {
    out.println("% No RAPI traffic captured yet");
    return;
  }

  out.println("% Most recent RAPI traffic (raw transcript; a command line is followed by its response):");
  int start = 0;
  while(start < (int)cap.buf.length()) {
    int end = cap.buf.indexOf('\r', start);
    if(end < 0) end = cap.buf.length();
    String line = cap.buf.substring(start, end);
    line.trim();
    if(line.length()) out.printf("%s\r\n", line.c_str());
    start = end + 1;
  }
}

void cmd_debug_heartbeat(CliOutput &out, int argc, const char *argv[])
{
  CliBufferCapture cap;
  SerialEvse.printBuffer(cap);
  if(0 == cap.buf.length()) {
    out.println("% No RAPI traffic captured yet");
    return;
  }

  // $SY's response doesn't itself echo "SY", so pairing command->response
  // is only possible by position in this raw transcript: collect lines
  // first, then print every "$SY..." line together with the line right
  // after it.
  const uint8_t maxLines = 64;
  String lines[maxLines];
  uint8_t n = 0;
  int start = 0;
  while(start < (int)cap.buf.length() && n < maxLines) {
    int end = cap.buf.indexOf('\r', start);
    if(end < 0) end = cap.buf.length();
    String line = cap.buf.substring(start, end);
    line.trim();
    if(line.length()) lines[n++] = line;
    start = end + 1;
  }

  bool found = false;
  for(uint8_t i = 0; i < n; i++) {
    if(lines[i].startsWith("$SY")) {
      out.printf("%s\r\n", lines[i].c_str());
      if(i + 1 < n) out.printf("%s\r\n", lines[i + 1].c_str());
      found = true;
    }
  }
  if(!found) {
    out.println("% No heartbeat ($SY) traffic in the recent buffer");
  }
}

// Sentinel bodies — never actually invoked. The engine intercepts these by
// function-pointer identity before reaching the CliExecutor, since mode
// transitions are per-session local state with nothing to marshal.
void cmd_enter_config_mode(CliOutput &out, int argc, const char *argv[]) { }
void cmd_exit_mode(CliOutput &out, int argc, const char *argv[]) { }
void cmd_exit_session(CliOutput &out, int argc, const char *argv[]) { }
void cmd_enter_exec_mode(CliOutput &out, int argc, const char *argv[]) { }
void cmd_exit_to_unpriv(CliOutput &out, int argc, const char *argv[]) { }

void cmd_enable(CliOutput &out, int argc, const char *argv[])
{
  out.println("% Already at the highest privilege level for this session");
}

void cmd_write_memory(CliOutput &out, int argc, const char *argv[])
{
  config_commit(false);
  out.println("[OK]");
}

// "charge enable" (bare), or "charge enable current <amps>" and/or
// "charge enable auto-release" in either order — parsed manually here rather
// than as separate tree children, since the engine's tree-walk can't express
// two independently-optional flags on one command line.
void cmd_start_charging(CliOutput &out, int argc, const char *argv[])
{
  EvseProperties props(EvseState::Active);
  for(int i = 1; i < argc; i++) {
    if(argIs(argv[i], "current") && i + 1 < argc) {
      props.setChargeCurrent((uint32_t)atol(argv[++i]));
    } else if(argIs(argv[i], "auto-release")) {
      props.setAutoRelease(true);
    } else {
      out.printf("%% Unrecognized option: \"%s\"\r\n", argv[i]);
      return;
    }
  }
  manual.claim(props);
  out.println("% Charging started (manual override)");
}

void cmd_disable_charging(CliOutput &out, int argc, const char *argv[])
{
  EvseProperties props(EvseState::Disabled);
  manual.claim(props);
  out.println("% EVSE disabled (manual override)");
}

void cmd_release_claim(CliOutput &out, int argc, const char *argv[])
{
  manual.release();
  out.println("% Manual override released");
}

void cmd_clear_faults(CliOutput &out, int argc, const char *argv[])
{
  evse.resetFaultCounters();
  out.println("% Fault counters cleared");
}

void cmd_set_hostname(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing hostname");
    return;
  }
  config_set("hostname", String(argv[1]));
  out.println("[OK]");
}

void cmd_set_current_capacity(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing current value (A)");
    return;
  }
  long amps = atol(argv[1]);
  if(amps < evse.getMinCurrent() || amps > evse.getMaxHardwareCurrent()) {
    out.printf("%% Current must be between %ld and %ld A\r\n", evse.getMinCurrent(), evse.getMaxHardwareCurrent());
    return;
  }
  evse.setMaxConfiguredCurrent(amps);
  out.println("[OK]");
}

void cmd_set_service_level(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing service level (1|2|A)");
    return;
  }
  EvseMonitor::ServiceLevel level;
  if(argIs(argv[1], "1")) {
    level = EvseMonitor::ServiceLevel::L1;
  } else if(argIs(argv[1], "2")) {
    level = EvseMonitor::ServiceLevel::L2;
  } else if(argIs(argv[1], "a") || argIs(argv[1], "auto")) {
    level = EvseMonitor::ServiceLevel::Auto;
  } else {
    out.println("% Service level must be 1, 2 or auto");
    return;
  }
  evse.setServiceLevel(level);
  out.println("[OK]");
}

void cmd_feature_toggle(CliOutput &out, int argc, const char *argv[])
{
  // argv[0] is the feature node name (e.g. "gfi-test"); argv[1], if present, is
  // "enable"/"disable" as typed under "feature ...". The "no feature ..." path
  // never supplies argv[1], so it defaults to disable below.
  bool enable = (argc >= 2) && argIs(argv[1], "enable");

  if(argIs(argv[0], "gfi-test")) {
    evse.enableGfiTestCheck(enable);
  } else if(argIs(argv[0], "diode-check")) {
    evse.enableDiodeCheck(enable);
  } else if(argIs(argv[0], "ground-check")) {
    evse.enableGroundCheck(enable);
  } else if(argIs(argv[0], "stuck-relay-check")) {
    evse.enableStuckRelayCheck(enable);
  } else if(argIs(argv[0], "vent-required")) {
    evse.enableVentRequired(enable);
  } else if(argIs(argv[0], "temperature-check")) {
    evse.enableTemperatureCheck(enable);
  } else if(argIs(argv[0], "front-button")) {
    evse.enableFrontButton(enable);
  } else if(argIs(argv[0], "boot-lock")) {
    evse.enableBootLock(enable);
  } else {
    out.println("% Unknown feature");
    return;
  }

  // Fire-and-forget, matching the existing config_deserialize() EVSE-flag
  // convention (app_config.cpp) — these are async RAPI calls; we don't block
  // the CLI on the round-trip confirmation.
  out.println("[OK]");
}

void cmd_service_toggle(CliOutput &out, int argc, const char *argv[])
{
  bool enable = (argc >= 2) && argIs(argv[1], "enable");
  String key = String(argv[0]) + "_enabled";
  config_set(key.c_str(), enable);
  out.println("[OK]");
}

void cmd_set_ssh_username(CliOutput &out, int argc, const char *argv[])
{
#ifdef ENABLE_SSH_CLI
  if(argc < 2) {
    out.println("% Missing username");
    return;
  }
  config_set("ssh_username", String(argv[1]));
  out.println("[OK]");
#else
  out.println("% SSH CLI not built into this firmware");
#endif
}

void cmd_set_ssh_password(CliOutput &out, int argc, const char *argv[])
{
#ifdef ENABLE_SSH_CLI
  if(argc < 2) {
    out.println("% Missing password");
    return;
  }
  config_set("ssh_password", String(argv[1]));
  out.println("[OK]");
#else
  out.println("% SSH CLI not built into this firmware");
#endif
}

void cmd_set_schedule_clear(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing event id (see \"show schedule\")");
    return;
  }
  if(scheduler.removeEvent((uint32_t)atol(argv[1]))) {
    out.println("[OK]");
  } else {
    out.println("% No such event id");
  }
}

void cmd_set_claims_release(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing client name (see \"show claims\")");
    return;
  }
  for(uint8_t i = 0; i < claimClientNameCount; i++) {
    if(argIs(argv[1], claimClientNames[i].name)) {
      evse.release(claimClientNames[i].client);
      out.println("[OK]");
      return;
    }
  }
  out.println("% Unknown client name");
}

void cmd_set_ntp(CliOutput &out, int argc, const char *argv[])
{
  if(argIs(argv[0], "enable")) {
    config_set("sntp_enabled", true);
    out.println("[OK]");
  } else if(argIs(argv[0], "disable")) {
    config_set("sntp_enabled", false);
    out.println("[OK]");
  } else if(argIs(argv[0], "server")) {
    if(argc < 2) {
      out.println("% Missing server hostname");
      return;
    }
    config_set("sntp_hostname", String(argv[1]));
    out.println("[OK]");
  }
}

void cmd_set_mqtt(CliOutput &out, int argc, const char *argv[])
{
  if(argIs(argv[0], "enable")) {
    config_set("mqtt_enabled", true);
  } else if(argIs(argv[0], "disable")) {
    config_set("mqtt_enabled", false);
  } else if(argc < 2) {
    out.println("% Missing value");
    return;
  } else if(argIs(argv[0], "server")) {
    config_set("mqtt_server", String(argv[1]));
  } else if(argIs(argv[0], "port")) {
    config_set("mqtt_port", (uint32_t)atol(argv[1]));
  } else if(argIs(argv[0], "password")) {
    config_set("mqtt_pass", String(argv[1]));
  } else {
    out.println("% Unknown mqtt setting");
    return;
  }
  out.println("[OK]");
}

void cmd_set_interface_wifi0(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing value");
    return;
  }
  if(argIs(argv[0], "ssid")) {
    config_set("ssid", String(argv[1]));
  } else if(argIs(argv[0], "password")) {
    config_set("pass", String(argv[1]));
  } else {
    out.println("% Unknown interface setting");
    return;
  }
  out.println("[OK]");
}

void cmd_set_ammeter(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing value");
    return;
  }
  long scale  = evse.getCurrentSensorScale();
  long offset = evse.getCurrentSensorOffset();
  if(argIs(argv[0], "scale")) {
    scale = atol(argv[1]);
  } else if(argIs(argv[0], "offset")) {
    offset = atol(argv[1]);
  } else {
    out.println("% Unknown ammeter setting");
    return;
  }
  // Both axes are set together — RAPI ($SA) has no single-value form — so the
  // axis not being changed is read back from the device first, above.
  evse.configureCurrentSensorScale(scale, offset);
  out.println("[OK]");
}

void cmd_set_voltage(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing voltage (V)");
    return;
  }
  double volts = atof(argv[1]);
  // voltage_cfg (centivolts) is the EEPROM-persisted value read back by
  // "show running-config"/"write memory" — config_deserialize() updates it
  // alongside the live evse.setVoltage() call for the same reason (see
  // app_config.cpp's "voltage" key handler); mirror that here so this value
  // survives a reboot instead of silently reverting to whatever was last
  // saved via the web UI/REST API.
  voltage_cfg = (uint32_t)(volts * 100);
  evse.setVoltage(volts);
  out.println("[OK]");
}

void cmd_set_heartbeat(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing value");
    return;
  }
  uint32_t interval = evse.getHeartbeatInterval();
  uint32_t current  = evse.getHeartbeatCurrent();
  if(argIs(argv[0], "interval")) {
    interval = (uint32_t)atol(argv[1]);
  } else if(argIs(argv[0], "current")) {
    current = (uint32_t)atol(argv[1]);
  } else {
    out.println("% Unknown heartbeat setting");
    return;
  }
  // Both axes are set together (RAPI $SY takes interval and current in one
  // call) — the axis not being changed is read back from the device first.
  // heartbeat_interval_cfg/heartbeat_current_cfg are the EEPROM-persisted
  // values — config_deserialize() updates them alongside the live
  // evse.setHeartbeatSupervision() call for the same reason; mirror that
  // here so this survives a reboot instead of reverting to the last value
  // saved via the web UI/REST API.
  heartbeat_interval_cfg = interval;
  heartbeat_current_cfg  = current;
  evse.setHeartbeatSupervision(interval, current);
  out.println("[OK]");
}

void cmd_set_relay(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing value (enable|disable)");
    return;
  }
  // Relay indices match the existing app_config.cpp / RAPI ($SR) convention:
  // 1=DC1, 2=DC2, 3=AC.
  int relay;
  if(argIs(argv[0], "dc1")) {
    relay = 1;
  } else if(argIs(argv[0], "dc2")) {
    relay = 2;
  } else if(argIs(argv[0], "ac")) {
    relay = 3;
  } else {
    out.println("% Unknown relay");
    return;
  }
  bool enable = argIs(argv[1], "enable");
  if(!enable && !argIs(argv[1], "disable")) {
    out.println("% Value must be enable or disable");
    return;
  }
  evse.setRelayEnable(relay, enable);
  out.println("[OK]");
}

// GitHub release asset naming convention (see .github/workflows/build.yaml's
// "Upload release assets" job): "<buildenv>.bin", attached to the repo's
// "latest" (non-prerelease) release. This well-known URL form redirects
// straight to the asset without needing to call the GitHub API or parse JSON
// on-device — http_update_from_url() already follows redirects.
#define FIRMWARE_GITHUB_REPO "https://github.com/OpenEVSE/ESP32_WiFi_V4.x"

static void installFromUrl(CliOutput &out, const String &url)
{
  // Check up front rather than letting the download start only to fail deep
  // inside Update.begin() — e.g. an open SSH session (~20-40KB) plus an
  // HTTPS connection's own buffers can leave too little heap for the OTA
  // partition write to start safely. See http_update.h.
  if(!http_update_has_sufficient_heap()) {
    out.println("% Not enough free memory to start a firmware update right now — close any other active connections (e.g. an SSH session) and try again");
    return;
  }
  out.printf("%% Starting firmware update from %s\r\n", url.c_str());
  out.println("% The device will reboot automatically if the update succeeds.");
  http_update_from_url(url,
    [](size_t complete, size_t total) { },
    [](int) { },
    [](int errorCode) {
      DEBUG_PORT.printf("HTTP OTA failed: %d\n", errorCode);
    });
}

void cmd_install(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing URL");
    return;
  }
  String url(argv[1]);
  if(!url.startsWith("http://") && !url.startsWith("https://")) {
    out.println("% URL must start with http:// or https://");
    return;
  }
  installFromUrl(out, url);
}

void cmd_install_github_latest(CliOutput &out, int argc, const char *argv[])
{
  String url = String(F(FIRMWARE_GITHUB_REPO)) + "/releases/latest/download/" + buildenv + ".bin";
  installFromUrl(out, url);
}

// ---- show tech-support ----
// Cisco IOS-style: run a battery of show commands and print them with section
// headers. Matches what the web UI's "Download Diagnostics" (Dev.svelte exportData())
// assembles from /status, /config, /claims, /override, /schedule, /limit, etc.

static void techSupportSection(CliOutput &out, const char *title,
                                CliHandler fn, int argc, const char *argv[])
{
  out.println("");
  out.println("----------------------------------------");
  out.printf("--- %s\r\n", title);
  out.println("----------------------------------------");
  fn(out, argc, argv);
}

void cmd_show_tech_support(CliOutput &out, int argc, const char *argv[])
{
  // argv stub — handlers that need argv[0] as their node name get a placeholder
  const char *noArgs[] = { "tech-support" };

  techSupportSection(out, "show version",         cmd_show_version,        1, noArgs);
  techSupportSection(out, "show hardware",         cmd_show_hardware,       1, noArgs);
  techSupportSection(out, "show status",           cmd_show_status,         1, noArgs);
  techSupportSection(out, "show sensors",          cmd_show_sensors,        1, noArgs);
  techSupportSection(out, "show temperature",      cmd_show_temperature,    1, noArgs);
  techSupportSection(out, "show interface wifi0",  cmd_show_interface_wifi0,1, noArgs);
  techSupportSection(out, "show ntp",              cmd_show_ntp,            1, noArgs);
  techSupportSection(out, "show mqtt",             cmd_show_mqtt,           1, noArgs);
  techSupportSection(out, "show claims",           cmd_show_claims,         1, noArgs);
  techSupportSection(out, "show schedule",         cmd_show_schedule,       1, noArgs);
  techSupportSection(out, "show limit",            cmd_show_limit,          1, noArgs);
  techSupportSection(out, "show faults",           cmd_show_faults,         1, noArgs);
  techSupportSection(out, "show running-config",   cmd_show_running_config, 1, noArgs);
  techSupportSection(out, "debug rapi",            cmd_debug_rapi,          1, noArgs);
  techSupportSection(out, "debug heartbeat",       cmd_debug_heartbeat,     1, noArgs);
  out.println("");
  out.println("--- end ---");
}

// ---- show energy log daily/monthly/annual (not available when ENABLE_TSDB is set) ----
#ifndef ENABLE_TSDB

void cmd_show_energy_log_daily(CliOutput &out, int argc, const char *argv[])
{
  DynamicJsonDocument doc(4096);
  energyLogger.getDailyMetrics(doc);
  JsonArray arr = doc["daily"];
  if(arr.isNull() || arr.size() == 0) {
    out.println("% No daily energy data recorded yet");
    return;
  }
  out.println("Date        Energy(Wh)  Peak(C)  Min(C)");
  out.println("----------- ----------  -------  ------");
  for(JsonObject entry : arr) {
    out.printf("%-11s %10.0f  %7.1f  %6.1f\r\n",
      entry["dt"].as<const char *>() ? entry["dt"].as<const char *>() : "?",
      (double)entry["en"],
      (double)entry["pk"],
      (double)entry["mn"]);
  }
}

void cmd_show_energy_log_monthly(CliOutput &out, int argc, const char *argv[])
{
  DynamicJsonDocument doc(2048);
  energyLogger.getMonthlyMetrics(doc);
  JsonArray arr = doc["monthly"];
  if(arr.isNull() || arr.size() == 0) {
    out.println("% No monthly energy data recorded yet");
    return;
  }
  out.println("Month       Energy(kWh) Peak(C)  Min(C)");
  out.println("----------- ----------  -------  ------");
  for(JsonObject entry : arr) {
    out.printf("%-11s %10.2f  %7.1f  %6.1f\r\n",
      entry["mo"].as<const char *>() ? entry["mo"].as<const char *>() : "?",
      (double)entry["en"],
      (double)entry["pk"],
      (double)entry["mn"]);
  }
}

void cmd_show_energy_log_annual(CliOutput &out, int argc, const char *argv[])
{
  DynamicJsonDocument doc(1024);
  energyLogger.getAnnualMetrics(doc);
  JsonArray arr = doc["annual"];
  if(arr.isNull() || arr.size() == 0) {
    out.println("% No annual energy data recorded yet");
    return;
  }
  out.println("Year  Energy(kWh) Peak(C)  Min(C)");
  out.println("----- ----------  -------  ------");
  for(JsonObject entry : arr) {
    out.printf("%-5s %10.2f  %7.1f  %6.1f\r\n",
      entry["yr"].as<const char *>() ? entry["yr"].as<const char *>() : "?",
      (double)entry["en"],
      (double)entry["pk"],
      (double)entry["mn"]);
  }
}

#endif // ENABLE_TSDB

// ---- show logging / show logging last <n> ----

void cmd_show_logging_summary(CliOutput &out, int argc, const char *argv[])
{
  uint32_t minIdx = eventLog.getMinIndex();
  uint32_t maxIdx = eventLog.getMaxIndex();
  if(maxIdx < minIdx) {
    out.println("% Event log is empty");
    return;
  }
  uint32_t count = maxIdx - minIdx + 1;
  out.printf("Event log: %u entr%s (index %u to %u)\r\n",
    count, count == 1 ? "y" : "ies", minIdx, maxIdx);
  out.println("Use \"show logging last <n>\" to view recent entries.");
}

void cmd_show_logging_block(CliOutput &out, int argc, const char *argv[])
{
  uint32_t n = (argc >= 2) ? (uint32_t)atol(argv[1]) : 20;
  if(n == 0 || n > 100) n = 20;

  uint32_t minIdx = eventLog.getMinIndex();
  uint32_t maxIdx = eventLog.getMaxIndex();
  if(maxIdx < minIdx) {
    out.println("% Event log is empty");
    return;
  }
  uint32_t start = (maxIdx + 1 >= n) ? (maxIdx + 1 - n) : minIdx;
  if(start < minIdx) start = minIdx;

  out.println("Time                 Type         Event");
  out.println("-------------------- ------------ -----------------------------------------------");
  eventLog.enumerate(start,
    [&out](String time, EventType type, const String &logEntry,
           EvseState managerState, uint8_t evseState, uint32_t evseFlags,
           uint32_t pilot, double energy, uint32_t elapsed,
           double temperature, double temperatureMax,
           uint8_t divertMode, uint8_t shaper) {
      out.printf("%-20s %-12s %s\r\n",
        time.c_str(), type.toString(), logEntry.c_str());
    });
}

// ---- show limit ----

void cmd_show_limit(CliOutput &out, int argc, const char *argv[])
{
  if(!limit.hasLimit()) {
    out.println("No charge limit active.");
    return;
  }
  LimitProperties lp = limit.get();
  LimitType lt = lp.getType();
  out.printf("Type        : %s\r\n", lt.toString());
  out.printf("Value       : %u\r\n", lp.getValue());
  out.printf("Auto-release: %s\r\n", lp.getAutoRelease() ? "yes" : "no");
}

// ---- set limit {time|energy|soc|range} <value> / set limit clear ----

void cmd_set_limit(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2) {
    out.println("% Missing value");
    return;
  }
  if(!limit.setDefaultLimit(argv[0], (uint32_t)atol(argv[1]))) {
    out.println("% Failed to set limit");
    return;
  }
  out.println("[OK]");
}

void cmd_set_limit_clear(CliOutput &out, int argc, const char *argv[])
{
  limit.clear();
  out.println("[OK]");
}

// ---- rfid enroll ----

void cmd_rfid_enroll(CliOutput &out, int argc, const char *argv[])
{
#ifdef ENABLE_RFID
  if(!config_rfid_enabled()) {
    out.println("% RFID is not enabled (see \"feature rfid enable\")");
    return;
  }
  rfid.waitForTag();
  out.println("% Ready — tap the card/fob to enroll it. Check the web UI to confirm.");
#else
  out.println("% RFID not built into this firmware");
#endif
}

// ---- show interface wifi0 scan ----

void cmd_show_interface_wifi0_scan(CliOutput &out, int argc, const char *argv[])
{
  out.println("% Scanning for WiFi networks (this may take a few seconds)...");
  int n = WiFi.scanNetworks();
  if(n <= 0) {
    out.println("% No networks found");
    return;
  }
  out.println("  # SSID                             RSSI  Ch  Security");
  out.println("--- -------------------------------- ----- --- ----------");
  for(int i = 0; i < n; i++) {
    out.printf("%3d %-32s %5d %3d  %s\r\n",
      i + 1,
      WiFi.SSID(i).c_str(),
      WiFi.RSSI(i),
      WiFi.channel(i),
      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
  }
  WiFi.scanDelete();
}

// ---- show tesla vehicles ----

void cmd_show_tesla_vehicles(CliOutput &out, int argc, const char *argv[])
{
  int cnt = teslaClient.getVehicleCnt();
  if(cnt <= 0) {
    out.println("% No Tesla vehicles linked (configure Tesla credentials in the web UI)");
    return;
  }
  out.printf("%-4s  %-20s  %s\r\n", "Idx", "Display Name", "Vehicle ID");
  out.printf("%-4s  %-20s  %s\r\n", "---", "--------------------", "----------");
  for(int i = 0; i < cnt; i++) {
    out.printf("%-4d  %-20s  %s\r\n", i,
      teslaClient.getVehicleDisplayName(i).c_str(),
      teslaClient.getVehicleId(i).c_str());
  }
}

// ---- clear mqtt ----

void cmd_clear_mqtt(CliOutput &out, int argc, const char *argv[])
{
  mqtt.restartConnection();
  out.println("% MQTT reconnect triggered");
}

// ---- set clock <HH:MM:SS> <day> <month> <year> ----
// Accepts: set clock 14:30:00 29 6 2026

void cmd_set_clock(CliOutput &out, int argc, const char *argv[])
{
  // argv[0]="clock", argv[1]=HH:MM:SS, argv[2]=day, argv[3]=month, argv[4]=year
  if(argc < 5) {
    out.println("% Usage: set clock <HH:MM:SS> <day> <month> <year>");
    return;
  }
  int hh = 0, mm = 0, ss = 0;
  if(3 != sscanf(argv[1], "%d:%d:%d", &hh, &mm, &ss)) {
    out.println("% Time must be HH:MM:SS");
    return;
  }
  int day   = atoi(argv[2]);
  int month = atoi(argv[3]);
  int year  = atoi(argv[4]);

  if(hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59 ||
     day < 1 || day > 31 || month < 1 || month > 12 || year < 2000) {
    out.println("% Invalid date/time value");
    return;
  }

  struct tm t = {};
  t.tm_hour = hh;
  t.tm_min  = mm;
  t.tm_sec  = ss;
  t.tm_mday = day;
  t.tm_mon  = month - 1;
  t.tm_year = year - 1900;
  t.tm_isdst = -1;

  time_t epoch = mktime(&t);
  if(epoch == (time_t)-1) {
    out.println("% Failed to convert date/time");
    return;
  }

  struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
  timeManager.setTime(tv, "cli");
  out.println("[OK]");
}

// ---- reset factory-default confirm ----

void cmd_reset_factory_default(CliOutput &out, int argc, const char *argv[])
{
  if(argc < 2 || 0 != strcasecmp(argv[1], "confirm")) {
    out.println("% This will erase ALL configuration and reboot to factory defaults.");
    out.println("% Type:  reset factory-default confirm");
    return;
  }
  out.println("% Erasing configuration and rebooting...");
  config_reset();
  ESPAL.eraseConfig();
  restart_system();
}

// ---- reload ----

void cmd_reload(CliOutput &out, int argc, const char *argv[])
{
  out.println("% Rebooting...");
  restart_system();
}
