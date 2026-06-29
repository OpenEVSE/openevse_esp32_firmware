#include "cli_tree.h"
#include "cli_commands.h"

// All arrays below are `const` at file scope, which on ESP32/Xtensa lands in
// flash-mapped .rodata (not RAM) — no PROGMEM macros needed, unlike AVR.

// ---- unprivileged EXEC ("host>") — only these three shows, plus "enable" ----
static const CliNode unprivShowChildren[] = {
  { "version", "Show firmware version and free heap",  nullptr, 0, cmd_show_version, CLI_MODE_UNPRIV, CLI_ARG_NONE },
  { "status",  "Show live charging status",            nullptr, 0, cmd_show_status,  CLI_MODE_UNPRIV, CLI_ARG_NONE },
  { "faults",  "Show fault/error counters",            nullptr, 0, cmd_show_faults,  CLI_MODE_UNPRIV, CLI_ARG_NONE },
};
const CliNode cliUnprivRoot[] = {
  { "show",   "Show running system information", unprivShowChildren, 3, nullptr,              CLI_MODE_UNPRIV, CLI_ARG_NONE },
  { "enable", "Enter privileged mode",            nullptr,            0, cmd_enter_exec_mode,  CLI_MODE_UNPRIV, CLI_ARG_NONE },
  { "exit",   "Close the SSH session",            nullptr,            0, cmd_exit_session,     CLI_MODE_UNPRIV, CLI_ARG_NONE },
};
const uint8_t cliUnprivRootCount = sizeof(cliUnprivRoot) / sizeof(cliUnprivRoot[0]);

// ---- privileged EXEC ("host#") ----
// ---- show interface wifi0 [scan] ----
static const CliNode showWifi0ScanChildren[] = {
  { "scan", "Scan for nearby WiFi networks", nullptr, 0, cmd_show_interface_wifi0_scan, CLI_MODE_EXEC, CLI_ARG_NONE },
};
static const CliNode showInterfaceWifi0Children[] = {
  { "wifi0", "Show WiFi station interface status (or \"wifi0 scan\")", showWifi0ScanChildren, 1, cmd_show_interface_wifi0, CLI_MODE_EXEC, CLI_ARG_NONE },
};

// ---- show mqtt topics ----
// The "mqtt" node below is both a terminator (bare "show mqtt" -> connection
// status) AND a parent (these children) — the engine supports a node being
// both, since dispatch only descends into children when more tokens remain.
static const CliNode showMqttChildren[] = {
  { "topics", "List MQTT topic paths and configured topic overrides", nullptr, 0, cmd_show_mqtt_topics, CLI_MODE_EXEC, CLI_ARG_NONE },
};

// ---- debug ----
static const CliNode debugChildren[] = {
  { "rapi",      "Show recent RAPI commands and responses",          nullptr, 0, cmd_debug_rapi,      CLI_MODE_EXEC, CLI_ARG_NONE },
  { "heartbeat", "Show recent heartbeat ($SY) commands and responses", nullptr, 0, cmd_debug_heartbeat, CLI_MODE_EXEC, CLI_ARG_NONE },
};

// ---- show energy [log {daily|monthly|annual}] — absent when ENABLE_TSDB is set ----
#ifndef ENABLE_TSDB
static const CliNode showEnergyLogChildren[] = {
  { "daily",   "Show daily energy log",   nullptr, 0, cmd_show_energy_log_daily,   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "monthly", "Show monthly energy log", nullptr, 0, cmd_show_energy_log_monthly, CLI_MODE_EXEC, CLI_ARG_NONE },
  { "annual",  "Show annual energy log",  nullptr, 0, cmd_show_energy_log_annual,  CLI_MODE_EXEC, CLI_ARG_NONE },
};
static const CliNode showEnergyChildren[] = {
  { "log", "Show time-series energy log", showEnergyLogChildren, 3, nullptr, CLI_MODE_EXEC, CLI_ARG_NONE },
};
#endif

// ---- show logging [last <n>] ----
static const CliNode showLoggingChildren[] = {
  { "last", "Show the last N event log entries", nullptr, 0, cmd_show_logging_block, CLI_MODE_EXEC, CLI_ARG_NUMBER },
};

// ---- show tesla vehicles ----
static const CliNode showTeslaChildren[] = {
  { "vehicles", "Show Tesla vehicles linked to this account", nullptr, 0, cmd_show_tesla_vehicles, CLI_MODE_EXEC, CLI_ARG_NONE },
};

// ---- show ----
static const CliNode showChildren[] = {
  { "running-config", "Show current operating configuration",                      nullptr,                    0, cmd_show_running_config,   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "version",        "Show firmware version and free heap",                        nullptr,                    0, cmd_show_version,          CLI_MODE_EXEC, CLI_ARG_NONE },
  { "status",         "Show live charging status",                                  nullptr,                    0, cmd_show_status,           CLI_MODE_EXEC, CLI_ARG_NONE },
  { "faults",         "Show fault/error counters",                                  nullptr,                    0, cmd_show_faults,           CLI_MODE_EXEC, CLI_ARG_NONE },
  { "schedule",       "Show configured schedule events",                            nullptr,                    0, cmd_show_schedule,         CLI_MODE_EXEC, CLI_ARG_NONE },
  { "claims",         "Show active EVSE manager claims",                            nullptr,                    0, cmd_show_claims,           CLI_MODE_EXEC, CLI_ARG_NONE },
  { "ntp",            "Show NTP time sync status",                                  nullptr,                    0, cmd_show_ntp,              CLI_MODE_EXEC, CLI_ARG_NONE },
  { "mqtt",           "Show MQTT connection status (or \"mqtt topics\")",           showMqttChildren,           1, cmd_show_mqtt,             CLI_MODE_EXEC, CLI_ARG_NONE },
  { "interface",      "Show a network interface",                                   showInterfaceWifi0Children, 1, nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "vlan",           "Show VLAN-to-interface mapping",                             nullptr,                    0, cmd_show_vlan,             CLI_MODE_EXEC, CLI_ARG_NONE },
  { "hardware",       "Show hardware/chip information",                             nullptr,                    0, cmd_show_hardware,         CLI_MODE_EXEC, CLI_ARG_NONE },
  { "sensors",        "Show voltage, frequency and temperature sensors",            nullptr,                    0, cmd_show_sensors,          CLI_MODE_EXEC, CLI_ARG_NONE },
  { "temperature",    "Show temperature sensors and shutdown/throttle setpoints",   nullptr,                    0, cmd_show_temperature,      CLI_MODE_EXEC, CLI_ARG_NONE },
#ifndef ENABLE_TSDB
  { "energy",         "Show session energy (or \"energy log {daily|monthly|annual}\")", showEnergyChildren,    1, cmd_show_energy,           CLI_MODE_EXEC, CLI_ARG_NONE },
#else
  { "energy",         "Show session/today/week/month/year/total energy",                nullptr,               0, cmd_show_energy,           CLI_MODE_EXEC, CLI_ARG_NONE },
#endif
  { "logging",        "Show event log summary (or \"logging last <n>\")",           showLoggingChildren,        1, cmd_show_logging_summary,  CLI_MODE_EXEC, CLI_ARG_NONE },
  { "limit",          "Show the active charge limit",                               nullptr,                    0, cmd_show_limit,            CLI_MODE_EXEC, CLI_ARG_NONE },
  { "tesla",          "Tesla vehicle integration",                                  showTeslaChildren,          1, nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "tech-support",   "Run all show commands (mirrors web Download Diagnostics)",   nullptr,                    0, cmd_show_tech_support,     CLI_MODE_EXEC, CLI_ARG_NONE },
};
static const uint8_t showChildrenCount = sizeof(showChildren) / sizeof(showChildren[0]);

// ---- configure terminal ----
static const CliNode configureChildren[] = {
  { "terminal", "Enter configuration mode", nullptr, 0, cmd_enter_config_mode, CLI_MODE_EXEC, CLI_ARG_NONE },
};

// ---- charge enable|disable|release ----
static const CliNode chargeChildren[] = {
  { "enable",  "Manually start charging [current <A>] [auto-release]", nullptr, 0, cmd_start_charging,   CLI_MODE_EXEC, CLI_ARG_WORD },
  { "disable", "Manually disable the EVSE",                             nullptr, 0, cmd_disable_charging, CLI_MODE_EXEC, CLI_ARG_NONE },
  { "release", "Release the manual override claim",                     nullptr, 0, cmd_release_claim,    CLI_MODE_EXEC, CLI_ARG_NONE },
};
static const uint8_t chargeChildrenCount = sizeof(chargeChildren) / sizeof(chargeChildren[0]);

// ---- clear faults / clear mqtt ----
static const CliNode clearChildren[] = {
  { "faults", "Clear GFCI/no-ground/stuck-relay fault counters", nullptr, 0, cmd_clear_faults, CLI_MODE_EXEC, CLI_ARG_NONE },
  { "mqtt",   "Force an MQTT reconnect",                         nullptr, 0, cmd_clear_mqtt,   CLI_MODE_EXEC, CLI_ARG_NONE },
};
static const uint8_t clearChildrenCount = sizeof(clearChildren) / sizeof(clearChildren[0]);

// ---- install <url> / install github latest ----
// "install" is both a terminator (takes a literal URL as its argument) AND a
// parent (the "github" child) — same dual-role pattern as "show mqtt"/"show
// mqtt topics": the engine falls back to free-form-argument handling for
// "install <url>" as soon as the next token doesn't match any child name.
static const CliNode installGithubChildren[] = {
  { "latest", "Install the latest GitHub release for this build", nullptr, 0, cmd_install_github_latest, CLI_MODE_EXEC, CLI_ARG_NONE },
};
static const CliNode installChildren[] = {
  { "github", "Install from a GitHub release", installGithubChildren, 1, nullptr, CLI_MODE_EXEC, CLI_ARG_NONE },
};

// ---- rfid enroll ----
static const CliNode rfidChildren[] = {
  { "enroll", "Arm the reader to enroll the next card tap", nullptr, 0, cmd_rfid_enroll, CLI_MODE_EXEC, CLI_ARG_NONE },
};

// ---- reset factory-default confirm ----
static const CliNode resetChildren[] = {
  { "factory-default", "Erase config and reboot to factory defaults (type \"confirm\" to proceed)", nullptr, 0, cmd_reset_factory_default, CLI_MODE_EXEC, CLI_ARG_WORD },
};

const CliNode cliExecRoot[] = {
  { "show",      "Show running system information",               showChildren,      showChildrenCount,   nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "debug",     "Show recent RAPI traffic for troubleshooting",  debugChildren,     2,                   nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "configure", "Enter configuration mode",                      configureChildren, 1,                   nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "write",     "Write the running configuration",               nullptr,           0,                   cmd_write_memory,          CLI_MODE_EXEC, CLI_ARG_NONE },
  { "charge",    "Manually control charging",                     chargeChildren,    chargeChildrenCount, nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "clear",     "Clear counters or force reconnects",            clearChildren,     clearChildrenCount,  nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "rfid",      "RFID tag operations",                           rfidChildren,      1,                   nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "reset",     "Factory-reset the device",                      resetChildren,     1,                   nullptr,                   CLI_MODE_EXEC, CLI_ARG_NONE },
  { "reload",    "Reboot the device",                             nullptr,           0,                   cmd_reload,                CLI_MODE_EXEC, CLI_ARG_NONE },
  { "install",   "Install firmware from a URL or GitHub release", installChildren,   1,                   cmd_install,               CLI_MODE_EXEC, CLI_ARG_WORD },
  { "enable",    "Enter privileged mode",                         nullptr,           0,                   cmd_enable,                CLI_MODE_EXEC, CLI_ARG_NONE },
  { "exit",      "Exit to unprivileged EXEC mode",                nullptr,           0,                   cmd_exit_to_unpriv,        CLI_MODE_EXEC, CLI_ARG_NONE },
};
const uint8_t cliExecRootCount = sizeof(cliExecRoot) / sizeof(cliExecRoot[0]);

// ---- configure-mode: set ... ----
static const CliNode setSshChildren[] = {
  { "username", "Set the SSH CLI login username", nullptr, 0, cmd_set_ssh_username, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "password", "Set the SSH CLI login password", nullptr, 0, cmd_set_ssh_password, CLI_MODE_CONFIG, CLI_ARG_WORD },
};

// "set schedule clear <id>" — see "show schedule" for ids. Adding events via
// CLI is out of scope (day-mask/time entry isn't a good fit for a
// line-oriented CLI) — use the web UI or REST API for that.
static const CliNode setScheduleChildren[] = {
  { "clear", "Remove a schedule event by id", nullptr, 0, cmd_set_schedule_clear, CLI_MODE_CONFIG, CLI_ARG_NUMBER },
};

// "set claims release <client-name>" — see "show claims" for client names.
static const CliNode setClaimsChildren[] = {
  { "release", "Release an EVSE manager claim", nullptr, 0, cmd_set_claims_release, CLI_MODE_CONFIG, CLI_ARG_WORD },
};

static const CliNode setNtpChildren[] = {
  { "enable",  "Enable NTP time sync",  nullptr, 0, cmd_set_ntp, CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "disable", "Disable NTP time sync", nullptr, 0, cmd_set_ntp, CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "server",  "Set the NTP server hostname", nullptr, 0, cmd_set_ntp, CLI_MODE_CONFIG, CLI_ARG_WORD },
};
static const uint8_t setNtpChildrenCount = sizeof(setNtpChildren) / sizeof(setNtpChildren[0]);

static const CliNode setMqttChildren[] = {
  { "enable",   "Enable the MQTT service",       nullptr, 0, cmd_set_mqtt, CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "disable",  "Disable the MQTT service",       nullptr, 0, cmd_set_mqtt, CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "server",   "Set the MQTT broker hostname",   nullptr, 0, cmd_set_mqtt, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "port",     "Set the MQTT broker port",       nullptr, 0, cmd_set_mqtt, CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "password", "Set the MQTT password",          nullptr, 0, cmd_set_mqtt, CLI_MODE_CONFIG, CLI_ARG_WORD },
};
static const uint8_t setMqttChildrenCount = sizeof(setMqttChildren) / sizeof(setMqttChildren[0]);

static const CliNode setInterfaceWifi0Children[] = {
  { "ssid",     "Set the WiFi network name",     nullptr, 0, cmd_set_interface_wifi0, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "password", "Set the WiFi network password", nullptr, 0, cmd_set_interface_wifi0, CLI_MODE_CONFIG, CLI_ARG_WORD },
};
static const CliNode setInterfaceChildren[] = {
  { "wifi0", "WiFi station interface settings", setInterfaceWifi0Children, 2, nullptr, CLI_MODE_CONFIG, CLI_ARG_NONE },
};

static const CliNode setAmmeterChildren[] = {
  { "scale",  "Set the ammeter scale factor",  nullptr, 0, cmd_set_ammeter, CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "offset", "Set the ammeter zero offset",   nullptr, 0, cmd_set_ammeter, CLI_MODE_CONFIG, CLI_ARG_NUMBER },
};

static const CliNode setHeartbeatChildren[] = {
  { "interval", "Set the heartbeat supervision interval (s)", nullptr, 0, cmd_set_heartbeat, CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "current",  "Set the heartbeat-fail fallback current (A)", nullptr, 0, cmd_set_heartbeat, CLI_MODE_CONFIG, CLI_ARG_NUMBER },
};

static const CliNode setRelayChildren[] = {
  { "dc1", "DC1 relay (EVSE NXT)", nullptr, 0, cmd_set_relay, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "dc2", "DC2 relay (EVSE NXT)", nullptr, 0, cmd_set_relay, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "ac",  "AC relay",             nullptr, 0, cmd_set_relay, CLI_MODE_CONFIG, CLI_ARG_WORD },
};

// "set limit {time|energy|soc|range} <value>" / "set limit clear"
static const CliNode setLimitChildren[] = {
  { "time",   "Set a time limit (minutes)",          nullptr, 0, cmd_set_limit,       CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "energy", "Set an energy limit (kWh)",            nullptr, 0, cmd_set_limit,       CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "soc",    "Set a state-of-charge limit (%)",      nullptr, 0, cmd_set_limit,       CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "range",  "Set a range limit (miles)",            nullptr, 0, cmd_set_limit,       CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "clear",  "Remove the active charge limit",       nullptr, 0, cmd_set_limit_clear, CLI_MODE_CONFIG, CLI_ARG_NONE  },
};
static const uint8_t setLimitChildrenCount = sizeof(setLimitChildren) / sizeof(setLimitChildren[0]);

static const CliNode setChildren[] = {
  { "hostname",         "Set the device hostname",                 nullptr,                0, cmd_set_hostname,         CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "current-capacity", "Set the EVSE charge current limit (A)",   nullptr,                0, cmd_set_current_capacity, CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "service-level",    "Set the EVSE service level (1|2|auto)",   nullptr,                0, cmd_set_service_level,    CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "ssh",              "SSH CLI login settings",                  setSshChildren,         2, nullptr,                  CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "schedule",         "Schedule event settings",                 setScheduleChildren,    1, nullptr,                  CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "claims",           "EVSE manager claim settings",              setClaimsChildren,      1, nullptr,                  CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "ntp",               "NTP time sync settings",                  setNtpChildren,         setNtpChildrenCount, nullptr, CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "mqtt",              "MQTT service settings",                   setMqttChildren,        setMqttChildrenCount, nullptr, CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "interface",         "Network interface settings",              setInterfaceChildren,   1, nullptr,                  CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "ammeter",           "Ammeter scale/offset calibration",        setAmmeterChildren,     2, nullptr,                  CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "voltage",           "Set the mains voltage calibration (V)",   nullptr,                0, cmd_set_voltage,          CLI_MODE_CONFIG, CLI_ARG_NUMBER },
  { "heartbeat",         "Heartbeat supervision settings",          setHeartbeatChildren,   2, nullptr,                  CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "relay",             "Per-relay enable/disable (DC1/DC2/AC)",              setRelayChildren,       3,                    nullptr,          CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "limit",            "Set or clear the active charge limit",                setLimitChildren,       setLimitChildrenCount, nullptr,          CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "clock",            "Set the system clock: set clock <HH:MM:SS> <d> <m> <y>", nullptr,            0,                    cmd_set_clock,    CLI_MODE_CONFIG, CLI_ARG_WORD },
};
static const uint8_t setChildrenCount = sizeof(setChildren) / sizeof(setChildren[0]);

// ---- configure-mode: feature .../ no feature ... ----
// "no feature X" reuses this exact same array (shared pointer, not duplicated
// data) — the handler defaults to "disable" when no trailing enable/disable
// word is supplied, which is always the case via the "no" path.
static const CliNode featureChildren[] = {
  { "gfi-test",          "GFCI self-test on connect",        nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "diode-check",       "J1772 diode check",                nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "ground-check",      "Ground (no-ground) check",         nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "stuck-relay-check", "Stuck relay check",                nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "vent-required",     "Ventilation-required fault",       nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "temperature-check", "Over-temperature throttling/fault",nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "front-button",      "Front panel button",               nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
  { "boot-lock",         "Require unlock before charging on boot", nullptr, 0, cmd_feature_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
};
static const uint8_t featureChildrenCount = sizeof(featureChildren) / sizeof(featureChildren[0]);

// ---- configure-mode: service ... ----
static const CliNode serviceChildren[] = {
  { "mqtt", "MQTT publish/subscribe service", nullptr, 0, cmd_service_toggle, CLI_MODE_CONFIG, CLI_ARG_WORD },
};

// ---- configure-mode: do show .../ do write — run an EXEC command without
// leaving config mode. "show" here reuses the exact same showChildren array
// as the EXEC-mode tree (shared pointer, not duplicated data).
static const CliNode doChildren[] = {
  { "show",  "Show running system information", showChildren, showChildrenCount, nullptr,          CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "write", "Write the running configuration",  nullptr,      0,                 cmd_write_memory, CLI_MODE_CONFIG, CLI_ARG_NONE },
};
static const uint8_t doChildrenCount = sizeof(doChildren) / sizeof(doChildren[0]);

const CliNode cliConfigRoot[] = {
  { "set",     "Set a configuration parameter",                   setChildren,     setChildrenCount,     nullptr,           CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "feature", "Enable/disable an EVSE safety/feature flag",       featureChildren, featureChildrenCount, nullptr,           CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "no",      "Negate a feature (shorthand for 'feature ... disable')", featureChildren, featureChildrenCount, nullptr,     CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "service", "Enable/disable an ESP32-side service",             serviceChildren, 1,                    nullptr,           CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "do",      "Run an EXEC-mode show/write command without leaving config mode", doChildren, doChildrenCount, nullptr,     CLI_MODE_CONFIG, CLI_ARG_NONE },
  { "exit",    "Exit configuration mode",                          nullptr,         0,                    cmd_exit_mode,     CLI_MODE_CONFIG, CLI_ARG_NONE },
};
const uint8_t cliConfigRootCount = sizeof(cliConfigRoot) / sizeof(cliConfigRoot[0]);
