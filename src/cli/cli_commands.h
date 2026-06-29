#ifndef _OPENEVSE_CLI_COMMANDS_H
#define _OPENEVSE_CLI_COMMANDS_H

#include "cli_types.h"

// Convention used by every handler below: argv[0] is always the name of the
// matched terminator CliNode (set by the engine, not typed by the user —
// mirrors argv[0]==program name in a C main()); argv[1..] are the free-form
// tokens the user typed after it.

// "show ..."
void cmd_show_running_config(CliOutput &out, int argc, const char *argv[]);
void cmd_show_version(CliOutput &out, int argc, const char *argv[]);
void cmd_show_status(CliOutput &out, int argc, const char *argv[]);
void cmd_show_faults(CliOutput &out, int argc, const char *argv[]);
void cmd_show_schedule(CliOutput &out, int argc, const char *argv[]);
void cmd_show_claims(CliOutput &out, int argc, const char *argv[]);
void cmd_show_ntp(CliOutput &out, int argc, const char *argv[]);
void cmd_show_mqtt(CliOutput &out, int argc, const char *argv[]);
void cmd_show_interface_wifi0(CliOutput &out, int argc, const char *argv[]);
void cmd_show_vlan(CliOutput &out, int argc, const char *argv[]);
void cmd_show_hardware(CliOutput &out, int argc, const char *argv[]);
void cmd_show_sensors(CliOutput &out, int argc, const char *argv[]);
void cmd_show_energy(CliOutput &out, int argc, const char *argv[]);
// "show mqtt" (bare) -> connection status; "show mqtt topics" -> this leaf.
void cmd_show_mqtt_topics(CliOutput &out, int argc, const char *argv[]);

// "debug rapi"/"debug heartbeat" — both dump the existing SerialEvse
// StreamSpy ring buffer (already capturing all RAPI traffic for the
// /evse web debug endpoint), as a one-shot snapshot of the most recent
// ~2KB of traffic, not a live tail. See cli_commands.cpp for why: the
// StreamSpy onRead/onWrite callback slots are already claimed by the
// web UI's /evse/console websocket (single-subscriber, would silently
// break it if we took them over for a live stream instead).
void cmd_debug_rapi(CliOutput &out, int argc, const char *argv[]);
void cmd_debug_heartbeat(CliOutput &out, int argc, const char *argv[]);

// Mode-transition sentinels. The engine intercepts these by function-pointer
// identity *before* handing off to the CliExecutor — the bodies are never
// actually invoked, since switching CLI mode is per-session local state with
// nothing for the executor to marshal onto the main thread.
void cmd_enter_config_mode(CliOutput &out, int argc, const char *argv[]);
void cmd_exit_mode(CliOutput &out, int argc, const char *argv[]);
void cmd_exit_session(CliOutput &out, int argc, const char *argv[]);
// "enable" typed from unprivileged EXEC — promotes to privileged EXEC ("#").
void cmd_enter_exec_mode(CliOutput &out, int argc, const char *argv[]);
// "exit" typed from privileged EXEC ("#") — demotes to unprivileged EXEC
// (">"); only "exit" from unprivileged EXEC actually closes the session.
void cmd_exit_to_unpriv(CliOutput &out, int argc, const char *argv[]);

void cmd_enable(CliOutput &out, int argc, const char *argv[]);
void cmd_write_memory(CliOutput &out, int argc, const char *argv[]);

// EXEC-mode charging actions
void cmd_start_charging(CliOutput &out, int argc, const char *argv[]);
void cmd_disable_charging(CliOutput &out, int argc, const char *argv[]);
void cmd_release_claim(CliOutput &out, int argc, const char *argv[]);
void cmd_clear_faults(CliOutput &out, int argc, const char *argv[]);

// configure-mode settings
void cmd_set_hostname(CliOutput &out, int argc, const char *argv[]);
void cmd_set_current_capacity(CliOutput &out, int argc, const char *argv[]);
void cmd_set_service_level(CliOutput &out, int argc, const char *argv[]);

// Shared by every "feature <name> {enable|disable}" / "no feature <name>" leaf.
// argv[1] == "enable"/"disable" if the user typed it; if omitted (the "no ..."
// path never types a trailing word) it defaults to disable.
void cmd_feature_toggle(CliOutput &out, int argc, const char *argv[]);

// Shared by "service <name> {enable|disable}" leaves (v1: mqtt only).
void cmd_service_toggle(CliOutput &out, int argc, const char *argv[]);

void cmd_set_ssh_username(CliOutput &out, int argc, const char *argv[]);
void cmd_set_ssh_password(CliOutput &out, int argc, const char *argv[]);

// "set schedule clear <id>" — removing entries only; adding events via CLI
// is intentionally out of scope (time/day-mask entry isn't a good fit for a
// line-oriented CLI) — use the web UI or REST API to add schedule events.
void cmd_set_schedule_clear(CliOutput &out, int argc, const char *argv[]);

// "set claims release <client-name>" — argv[1] is one of: manual, divert,
// boost, schedule, limit, ohm, ocpp, rfid, mqtt, shaper, tempthrottle.
void cmd_set_claims_release(CliOutput &out, int argc, const char *argv[]);

// argv[0] is "enable"/"disable"/"server" (the matched leaf under "set ntp").
void cmd_set_ntp(CliOutput &out, int argc, const char *argv[]);

// argv[0] is "enable"/"disable"/"server"/"port"/"username"/"password" (the
// matched leaf under "set mqtt").
void cmd_set_mqtt(CliOutput &out, int argc, const char *argv[]);

// argv[0] is "ssid"/"password" (the matched leaf under "set interface wifi0").
void cmd_set_interface_wifi0(CliOutput &out, int argc, const char *argv[]);

// argv[0] is "scale"/"offset" (the matched leaf under "set ammeter"); argv[1]
// is the new value, the other axis is left at its current device value.
void cmd_set_ammeter(CliOutput &out, int argc, const char *argv[]);

// "set voltage <volts>" — mains voltage calibration (plain volts, e.g. "240").
void cmd_set_voltage(CliOutput &out, int argc, const char *argv[]);

// argv[0] is "interval"/"current" (the matched leaf under "set heartbeat");
// argv[1] is the new value, the other axis is left at its current value.
void cmd_set_heartbeat(CliOutput &out, int argc, const char *argv[]);

// argv[0] is "dc1"/"dc2"/"ac" (the matched leaf under "set relay"); argv[1]
// is "enable"/"disable".
void cmd_set_relay(CliOutput &out, int argc, const char *argv[]);

// "show temperature" — every sensor reading plus the configured
// shutdown/throttle setpoints and whether throttling is active right now.
void cmd_show_temperature(CliOutput &out, int argc, const char *argv[]);

// "install <url>" (argv[1] present) or "install github latest" (argv[0] ==
// "latest", no URL — the github/latest path resolves the URL itself from the
// device's buildenv before delegating to the same http_update_from_url()).
void cmd_install(CliOutput &out, int argc, const char *argv[]);
void cmd_install_github_latest(CliOutput &out, int argc, const char *argv[]);

// "show tech-support" — run a battery of show commands (mirrors web "Download Diagnostics")
void cmd_show_tech_support(CliOutput &out, int argc, const char *argv[]);

// "show energy log {daily|monthly|annual}" — absent when ENABLE_TSDB is set
#ifndef ENABLE_TSDB
void cmd_show_energy_log_daily(CliOutput &out, int argc, const char *argv[]);
void cmd_show_energy_log_monthly(CliOutput &out, int argc, const char *argv[]);
void cmd_show_energy_log_annual(CliOutput &out, int argc, const char *argv[]);
#endif

// "show logging" (summary) / "show logging last <n>"
void cmd_show_logging_summary(CliOutput &out, int argc, const char *argv[]);
void cmd_show_logging_block(CliOutput &out, int argc, const char *argv[]);

// "show limit"
void cmd_show_limit(CliOutput &out, int argc, const char *argv[]);

// "set limit {time|energy|soc|range} <value>" / "set limit clear"
void cmd_set_limit(CliOutput &out, int argc, const char *argv[]);
void cmd_set_limit_clear(CliOutput &out, int argc, const char *argv[]);

// "rfid enroll" — arms the reader to enroll the next card tap
void cmd_rfid_enroll(CliOutput &out, int argc, const char *argv[]);

// "show interface wifi0 scan"
void cmd_show_interface_wifi0_scan(CliOutput &out, int argc, const char *argv[]);

// "show tesla vehicles"
void cmd_show_tesla_vehicles(CliOutput &out, int argc, const char *argv[]);

// "clear mqtt" — force reconnect
void cmd_clear_mqtt(CliOutput &out, int argc, const char *argv[]);

// "set clock <HH:MM:SS> <day> <month> <year>"
void cmd_set_clock(CliOutput &out, int argc, const char *argv[]);

// "reset factory-default confirm" — erase config and reboot
void cmd_reset_factory_default(CliOutput &out, int argc, const char *argv[]);

// "reload" — reboot the device
void cmd_reload(CliOutput &out, int argc, const char *argv[]);

#endif // _OPENEVSE_CLI_COMMANDS_H
