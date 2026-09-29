#ifndef _OPENEVSE_NOTIFICATIONS_RULES_H
#define _OPENEVSE_NOTIFICATIONS_RULES_H

// Advisory rule table, evaluated as a pure function over a snapshot.
//
// Deliberately free of Arduino and firmware headers so it builds on the host
// in env:native_test. The caller (notifications.cpp) reads every input from
// EvseMonitor's cache, so evaluation costs no RAPI traffic.
//
// An advisory is NOT a fault: nothing here ever describes a condition that is
// stopping the EVSE charging right now. That belongs to the fault screen.

#include <stdint.h>
#include <stddef.h>

enum NotificationSeverity : uint8_t {
  NOTIFICATION_INFO     = 0,
  NOTIFICATION_WARNING  = 1,
  NOTIFICATION_CRITICAL = 2,
};

enum NotificationCategory : uint8_t {
  NOTIFICATION_SAFETY  = 0,
  NOTIFICATION_FAULT   = 1,
  NOTIFICATION_WEAR    = 2,
  NOTIFICATION_THERMAL = 3,
};

// 16 rules today; the output array is sized with room to add without
// revisiting every caller.
#define NOTIFICATION_MAX 20

// Relay life thresholds (percent remaining).
#define NOTIFICATION_RELAY_LIFE_NOTICE_PCT   20
#define NOTIFICATION_RELAY_LIFE_WARNING_PCT   5

// How far below the controller's panic temperature the high-temp advisory
// starts. Degrees C.
#define NOTIFICATION_HIGH_TEMP_MARGIN_C       5

struct NotificationInputs {
  // Safety checks. true = the check is ENABLED (EvseMonitor's sense).
  bool     ground_check;
  bool     gfci_check;
  bool     relay_check;
  bool     diode_check;
  bool     vent_check;
  bool     temp_check;

  // Latched fault counters from $GF.
  uint32_t gfci_count;
  uint32_t no_ground_count;
  uint32_t stuck_relay_count;

  // Thermal.
  bool     temp_throttling;
  bool     temp_valid;
  int32_t  temp_c_x10;      // hottest valid sensor, tenths of a degree C
  int32_t  panic_temp_c;    // getPanicTemperature(); 0 = unknown, rule skipped

  // Relay health ($GL). Every field below is ignored unless relay_health_known.
  bool     relay_health_known;
  uint8_t  relay_life_pct;
  uint32_t relay_cold_open_count;
  bool     relay_transit_drift;
  uint8_t  relay_thermal_warning_level;   // 0 none, 1 watch, 2 warn
  uint32_t relay_stuck_recovery_count;

  // The EVSE settings-flags word, folded into every safety token so that
  // changing any setting voids a muted safety ack.
  uint32_t settings_flags;
};

struct Notification {
  const char *id;        // stable, dotted; the only thing the UIs key on
  const char *key;       // short stable key for the persisted ack blob
  uint8_t     category;  // NotificationCategory
  uint8_t     severity;  // NotificationSeverity
  bool        sticky;    // acking mutes rather than clears
  uint32_t    token;     // ack is void once this changes
};

// Fills `out` with the active advisories, in table order, and returns how many.
// Never writes more than `max`.
size_t notifications_evaluate(const NotificationInputs &in, Notification *out, size_t max);

// Highest severity in the list. NOTIFICATION_INFO for an empty list — callers
// gate on count, not on this.
uint8_t notifications_max_severity(const Notification *list, size_t count);

// Index of the advisory the LCD should name: highest severity, earliest in
// table order among equals. -1 when count is 0.
int notifications_worst(const Notification *list, size_t count);

#endif // _OPENEVSE_NOTIFICATIONS_RULES_H
