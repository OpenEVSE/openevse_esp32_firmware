#include "replay_redact.h"

#include <stdio.h>
#include <string.h>

// What the simulator reads to reproduce a session: how each Charge Manager
// feature is configured, and the station's electrical limits. No credentials,
// hostnames, server addresses, MQTT topics or other free text a user could
// have put something private in. time_zone is kept: scheduled rules are
// local times and cannot be replayed without it.
static const char *ALLOWED[] = {
  "version",
  "buildenv",
  "time_zone",
  // Station defaults
  "default_state",
  "max_current_soft",
  "min_current_hard",
  "max_current_hard",
  "service",
  "charge_mode",
  "pause_uses_disabled",
  "is_threephase",
  "voltage",
  "heartbeat_interval",
  "heartbeat_current",
  "boot_lock",
  "scheduler_start_window",
  // Session limit
  "limit_default_type",
  "limit_default_value",
  // Eco / solar divert
  "divert_enabled",
  "divert_type",
  "divert_PV_ratio",
  "divert_attack_smoothing_time",
  "divert_decay_smoothing_time",
  "divert_min_charge_time",
  // Grid shaping
  "current_shaper_enabled",
  "current_shaper_max_pwr",
  "current_shaper_smoothing_time",
  "current_shaper_min_pause_time",
  "current_shaper_data_maxinterval",
  // Temperature protection
  "temp_throttle_enabled",
  "temp_throttle_setpoint",
  "over_temp_shutdown",
  // RFID / OCPP behaviour switches (not the server, key or tags)
  "rfid_enabled",
  "ocpp_enabled",
  "ocpp_auth_auto",
  "ocpp_auth_offline",
  "ocpp_suspend_evse",
  "ocpp_energize_plug",
  // Load sharing limits (not the group id or peer hosts)
  "loadsharing_enabled",
  "loadsharing_group_max_current",
  "loadsharing_safety_factor",
  "loadsharing_failsafe_mode",
  "loadsharing_failsafe_safe_current",
  "loadsharing_failsafe_peer_assumed_current",
  "loadsharing_rotation_interval",
  // Which data sources are in play
  "vehicle_data_src",
  "mqtt_vehicle_range_miles",
  "mqtt_enabled",
  "tesla_enabled",
  "sntp_enabled",
};

bool replay_redact_key_allowed(const char *key)
{
  if(!key || '\0' == key[0]) {
    return false;
  }
  for(size_t i = 0; i < sizeof(ALLOWED) / sizeof(ALLOWED[0]); i++) {
    if(0 == strcmp(key, ALLOWED[i])) {
      return true;
    }
  }
  return false;
}

void replay_redact_config(JsonObjectConst full, JsonObject out)
{
  for(JsonPairConst kv : full) {
    if(replay_redact_key_allowed(kv.key().c_str())) {
      out[kv.key()] = kv.value();
    }
  }

  // Keep how many cards are enrolled, never which.
  const char *tags = full["rfid_storage"] | "";
  size_t count = 0;
  bool in_tag = false;
  for(const char *p = tags; *p; p++) {
    bool sep = (',' == *p || ' ' == *p);
    if(!sep && !in_tag) {
      count++;
    }
    in_tag = !sep;
  }
  if(count > 0) {
    String storage;
    for(size_t i = 0; i < count; i++) {
      char tag[24];
      snprintf(tag, sizeof(tag), "%s%s%02u", i ? "," : "", REPLAY_RFID_PLACEHOLDER, (unsigned)(i + 1));
      storage += tag;
    }
    out["rfid_storage"] = storage;
  }
}
