#include "notifications_rules.h"

// One helper per emit keeps the table below readable and stops the bounds
// check from being repeated sixteen times.
namespace {

struct Sink {
  Notification *out;
  size_t max;
  size_t count;

  void add(const char *id, const char *key, uint8_t category,
           uint8_t severity, bool sticky, uint32_t token)
  {
    if(count >= max) {
      return;
    }
    out[count].id       = id;
    out[count].key      = key;
    out[count].category = category;
    out[count].severity = severity;
    out[count].sticky   = sticky;
    out[count].token    = token;
    count++;
  }
};

} // namespace

size_t notifications_evaluate(const NotificationInputs &in, Notification *out, size_t max)
{
  Sink s = { out, max, 0 };

  // --- safety -------------------------------------------------------------
  // Sticky: acking mutes, it never clears. The token is the settings-flags
  // word, so changing any EVSE setting voids a mute and re-raises.
  if(!in.ground_check) {
    s.add("safety.ground_check", "sg", NOTIFICATION_SAFETY, NOTIFICATION_CRITICAL, true, in.settings_flags);
  }
  if(!in.gfci_check) {
    s.add("safety.gfci_check", "sf", NOTIFICATION_SAFETY, NOTIFICATION_CRITICAL, true, in.settings_flags);
  }
  if(!in.relay_check) {
    s.add("safety.relay_check", "sr", NOTIFICATION_SAFETY, NOTIFICATION_WARNING, true, in.settings_flags);
  }
  if(!in.diode_check) {
    s.add("safety.diode_check", "sd", NOTIFICATION_SAFETY, NOTIFICATION_WARNING, true, in.settings_flags);
  }
  if(!in.vent_check) {
    s.add("safety.vent_check", "sv", NOTIFICATION_SAFETY, NOTIFICATION_WARNING, true, in.settings_flags);
  }
  if(!in.temp_check) {
    s.add("safety.temp_check", "st", NOTIFICATION_SAFETY, NOTIFICATION_WARNING, true, in.settings_flags);
  }

  // --- faults that have already cleared ------------------------------------
  // A live fault belongs to the fault screen; these are the latched records of
  // one that happened. Tokened on the count so a later trip re-raises.
  if(in.gfci_count > 0) {
    s.add("fault.gfci_tripped", "fg", NOTIFICATION_FAULT, NOTIFICATION_CRITICAL, false, in.gfci_count);
  }
  if(in.no_ground_count > 0) {
    s.add("fault.no_ground", "fn", NOTIFICATION_FAULT, NOTIFICATION_CRITICAL, false, in.no_ground_count);
  }
  if(in.stuck_relay_count > 0) {
    s.add("fault.stuck_relay", "fs", NOTIFICATION_FAULT, NOTIFICATION_CRITICAL, false, in.stuck_relay_count);
  }

  // --- thermal -------------------------------------------------------------
  if(in.temp_throttling) {
    s.add("thermal.throttling", "tt", NOTIFICATION_THERMAL, NOTIFICATION_WARNING, true, 0);
  }
  if(in.temp_valid && in.panic_temp_c > 0 &&
     in.temp_c_x10 >= (in.panic_temp_c - NOTIFICATION_HIGH_TEMP_MARGIN_C) * 10)
  {
    s.add("thermal.high_temp", "th", NOTIFICATION_THERMAL, NOTIFICATION_WARNING, true, 0);
  }

  // --- relay wear ----------------------------------------------------------
  // Skipped wholesale on a controller without the RELAY_HEALTH feature: no
  // rule at all beats a rule reporting a confident zero.
  if(in.relay_health_known) {
    if(in.relay_life_pct <= NOTIFICATION_RELAY_LIFE_WARNING_PCT) {
      // Token is the severity step, not the percentage: ordinary wear ticking
      // 20 -> 19 must not void an ack the user gave at 20.
      s.add("wear.relay_life", "wl", NOTIFICATION_WEAR, NOTIFICATION_WARNING, false, 2);
    } else if(in.relay_life_pct <= NOTIFICATION_RELAY_LIFE_NOTICE_PCT) {
      s.add("wear.relay_life", "wl", NOTIFICATION_WEAR, NOTIFICATION_INFO, false, 1);
    }
    if(in.relay_transit_drift) {
      s.add("wear.relay_transit_drift", "wd", NOTIFICATION_WEAR, NOTIFICATION_WARNING, true, 0);
    }
    if(in.relay_cold_open_count > 0) {
      s.add("wear.relay_cold_open", "wc", NOTIFICATION_WEAR, NOTIFICATION_WARNING, false, in.relay_cold_open_count);
    }
    if(in.relay_stuck_recovery_count > 0) {
      s.add("wear.stuck_relay_recovery", "ws", NOTIFICATION_WEAR, NOTIFICATION_INFO, false, in.relay_stuck_recovery_count);
    }
    if(in.relay_thermal_warning_level >= 2) {
      s.add("thermal.relay_thermal", "tr", NOTIFICATION_THERMAL, NOTIFICATION_WARNING, true, 2);
    } else if(in.relay_thermal_warning_level == 1) {
      s.add("thermal.relay_thermal", "tr", NOTIFICATION_THERMAL, NOTIFICATION_INFO, true, 1);
    }
  }

  return s.count;
}

uint8_t notifications_max_severity(const Notification *list, size_t count)
{
  uint8_t worst = NOTIFICATION_INFO;
  for(size_t i = 0; i < count; i++) {
    if(list[i].severity > worst) {
      worst = list[i].severity;
    }
  }
  return worst;
}

int notifications_worst(const Notification *list, size_t count)
{
  int best = -1;
  for(size_t i = 0; i < count; i++) {
    if(best < 0 || list[i].severity > list[best].severity) {
      best = (int)i;
    }
  }
  return best;
}
