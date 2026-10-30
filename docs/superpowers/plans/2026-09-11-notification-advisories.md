# Notification Advisories Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** surface non-fault conditions the user should know about — safety checks switched off, faults that already cleared, thermal state, relay wear — as a ranked advisory list, drawn as an amber border plus one line on the LCD and served on `/notifications`.

**Architecture:** a pure, host-testable rule table (`notifications_rules.cpp`) evaluated by a `MicroTasks::Task` (`notifications.cpp`) that diffs the live set, applies persisted acks, and publishes over HTTP, websocket and the LCD. No new RAPI traffic: every input is already cached in `EvseMonitor`.

**Tech Stack:** C++17, PlatformIO, Arduino-ESP32, ArduinoJson 6.20.1, MicroTasks, LVGL, doctest (`env:native_test`).

**Spec:** `docs/superpowers/specs/2026-09-11-notification-advisories-design.md`

## Global Constraints

- Branch `feature/notifications`, off upstream master `af0c7649`. Commit as `Andrew Rankin <andrew@eiknet.com>`. No AI attribution in commit messages.
- Relay life thresholds: notice at `relay_life_pct <= 20`, warning at `<= 5`.
- Every `wear.*` and `thermal.relay_*` rule is skipped entirely unless `EvseMonitor::isRelayHealthKnown()`.
- `/status` gains exactly two fields, both inside one `notifications` object: `count` and `severity`. Nothing else.
- Acks persist across restart. Invalidated by (1) the advisory's token, (2) for `safety.*`, any change to the EVSE settings-flags word, (3) a firmware version change.
- Acking a sticky advisory **mutes** it (drops off the LCD, stays listed); it never clears it.
- Event-log rows fire on the raise edge only, at most once per advisory id per boot.
- Amber only on the LCD (`NS_WARNING`). Red is reserved for the fault screen. No animation.
- The GUI half is a separate PR against `OpenEVSE/openevse-gui-nightshift` and is **out of scope for this plan**.

## File Structure

| File | Responsibility |
|---|---|
| `src/notifications_rules.h` / `.cpp` | **Create.** The rule table and its pure evaluator. No Arduino, no firmware includes — only `<stdint.h>`/`<stddef.h>`, so it compiles on the host. |
| `src/notifications_acks.h` / `.cpp` | **Create.** Ack store: encode/decode the persisted string, token matching, pruning. Also pure. |
| `src/notifications.h` / `.cpp` | **Create.** The `MicroTasks::Task`. Gathers inputs from `EvseManager`/`EvseMonitor`/`TempThrottle`, calls the evaluator, applies acks, emits the websocket event and the event-log rows, serves JSON. |
| `src/web_server_notifications.cpp` | **Create.** `GET /notifications`, `POST /notifications/<id>/ack`. Mirrors `web_server_claims.cpp`. |
| `src/web_server.cpp` | **Modify.** Register the two routes; add the two `/status` fields in `buildStatus()`. |
| `src/app_config.cpp` | **Modify.** Two persisted `String` options for the ack blob and the firmware version that wrote it. |
| `src/main.cpp` | **Modify.** `notifications.begin(evse);` |
| `src/lvgl_tft/charge_screen.h` / `.cpp` | **Modify.** Border style + advisory line. |
| `src/lvgl_tft/standby_screen.h` / `.cpp` | **Modify.** Same, plus a second top-strip line it does not currently have. |
| `src/lcd_lvgl.cpp` | **Modify.** Fill the new screen-data fields. |
| `src/event_log.h` / `.cpp`, `src/web_server_events.cpp` | **Modify (Task 7 only).** Optional notification-id column. |
| `platformio.ini` | **Modify.** Add the two pure sources to `env:native_test`'s `build_src_filter`. |
| `test/test_notifications_rules/test_notifications_rules.cpp` | **Create.** |
| `test/test_notifications_acks/test_notifications_acks.cpp` | **Create.** |

---

### Task 1: Rule table and pure evaluator

**Files:**
- Create: `src/notifications_rules.h`, `src/notifications_rules.cpp`
- Create: `test/test_notifications_rules/test_notifications_rules.cpp`
- Modify: `platformio.ini` (the `build_src_filter` line of `[env:native_test]`)

**Interfaces:**
- Consumes: nothing.
- Produces: `NotificationSeverity`, `NotificationCategory`, `struct NotificationInputs`, `struct Notification`, `size_t notifications_evaluate(const NotificationInputs &, Notification *, size_t)`, `uint8_t notifications_max_severity(const Notification *, size_t)`, `int notifications_worst(const Notification *, size_t)`, `#define NOTIFICATION_MAX 16`.

This is where nearly all the logic risk lives, and none of it needs hardware.

- [ ] **Step 1: Write the header**

Create `src/notifications_rules.h`:

```cpp
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

// 13 rules today; the output array is sized with room to add without
// revisiting every caller.
#define NOTIFICATION_MAX 16

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
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_notifications_rules/test_notifications_rules.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "notifications_rules.h"
#include <string.h>

// A charger with nothing to complain about: every safety check on, no faults,
// no thermal event, relay health known and healthy.
static NotificationInputs clean()
{
  NotificationInputs in = {};
  in.ground_check = in.gfci_check = in.relay_check = true;
  in.diode_check = in.vent_check = in.temp_check = true;
  in.temp_valid = true;
  in.temp_c_x10 = 250;
  in.panic_temp_c = 65;
  in.relay_health_known = true;
  in.relay_life_pct = 100;
  in.settings_flags = 0x0001;
  return in;
}

static bool has(const Notification *l, size_t n, const char *id)
{
  for(size_t i = 0; i < n; i++) {
    if(0 == strcmp(l[i].id, id)) return true;
  }
  return false;
}

static const Notification *find(const Notification *l, size_t n, const char *id)
{
  for(size_t i = 0; i < n; i++) {
    if(0 == strcmp(l[i].id, id)) return &l[i];
  }
  return nullptr;
}

TEST_CASE("a clean charger raises nothing") {
  Notification out[NOTIFICATION_MAX];
  CHECK(notifications_evaluate(clean(), out, NOTIFICATION_MAX) == 0);
  CHECK(notifications_worst(out, 0) == -1);
}

TEST_CASE("each safety check raises its own advisory when disabled") {
  Notification out[NOTIFICATION_MAX];

  NotificationInputs in = clean(); in.ground_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.ground_check"));

  in = clean(); in.gfci_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.gfci_check"));

  in = clean(); in.relay_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.relay_check"));

  in = clean(); in.diode_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.diode_check"));

  in = clean(); in.vent_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.vent_check"));

  in = clean(); in.temp_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.temp_check"));
}

TEST_CASE("ground and gfci are critical, the other four are warnings") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.ground_check = in.gfci_check = in.relay_check = false;
  in.diode_check = in.vent_check = in.temp_check = false;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(n == 6);
  CHECK(find(out, n, "safety.ground_check")->severity == NOTIFICATION_CRITICAL);
  CHECK(find(out, n, "safety.gfci_check")->severity == NOTIFICATION_CRITICAL);
  CHECK(find(out, n, "safety.relay_check")->severity == NOTIFICATION_WARNING);
  CHECK(find(out, n, "safety.diode_check")->severity == NOTIFICATION_WARNING);
  CHECK(find(out, n, "safety.vent_check")->severity == NOTIFICATION_WARNING);
  CHECK(find(out, n, "safety.temp_check")->severity == NOTIFICATION_WARNING);
  CHECK(notifications_max_severity(out, n) == NOTIFICATION_CRITICAL);
}

TEST_CASE("every safety advisory is sticky and carries the settings flags in its token") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.ground_check = false;
  in.settings_flags = 0x1234;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *g = find(out, n, "safety.ground_check");
  CHECK(g->sticky);
  CHECK(g->token == 0x1234);

  // Changing an unrelated setting moves the token, which is what voids an ack.
  in.settings_flags = 0x1235;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "safety.ground_check")->token == 0x1235);
}

TEST_CASE("fault counters raise, and their token is the count") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.gfci_count = 3;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *f = find(out, n, "fault.gfci_tripped");
  REQUIRE(f != nullptr);
  CHECK(f->severity == NOTIFICATION_CRITICAL);
  CHECK_FALSE(f->sticky);
  CHECK(f->token == 3);

  in = clean(); in.no_ground_count = 1;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "fault.no_ground")->token == 1);

  in = clean(); in.stuck_relay_count = 7;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "fault.stuck_relay")->token == 7);
}

TEST_CASE("zero fault counters raise nothing") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK_FALSE(has(out, n, "fault.gfci_tripped"));
  CHECK_FALSE(has(out, n, "fault.no_ground"));
  CHECK_FALSE(has(out, n, "fault.stuck_relay"));
}

TEST_CASE("thermal throttling raises while it is active") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.temp_throttling = true;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *t = find(out, n, "thermal.throttling");
  REQUIRE(t != nullptr);
  CHECK(t->severity == NOTIFICATION_WARNING);
  CHECK(t->sticky);
}

TEST_CASE("high temp raises within the margin of the panic threshold") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.panic_temp_c = 65;          // margin 5 => raises at 60.0 C

  in.temp_c_x10 = 599;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));

  in.temp_c_x10 = 600;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));

  in.temp_c_x10 = 700;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));
}

TEST_CASE("high temp is skipped when the reading or the threshold is unknown") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.temp_c_x10 = 900;

  in.temp_valid = false; in.panic_temp_c = 65;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));

  in.temp_valid = true; in.panic_temp_c = 0;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));
}

TEST_CASE("relay life steps from info to warning at the two thresholds") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();

  in.relay_life_pct = 21;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "wear.relay_life"));

  in.relay_life_pct = 20;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_INFO);

  in.relay_life_pct = 6;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_INFO);

  in.relay_life_pct = 5;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_WARNING);

  in.relay_life_pct = 0;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_WARNING);
}

TEST_CASE("the relay life token steps with severity, not with every percent") {
  // Otherwise an ack would be voided by ordinary wear ticking 20 -> 19.
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();

  in.relay_life_pct = 20;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  uint32_t at20 = find(out, n, "wear.relay_life")->token;

  in.relay_life_pct = 9;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->token == at20);

  in.relay_life_pct = 5;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->token != at20);
}

TEST_CASE("cold opens, drift, recoveries and relay thermal") {
  Notification out[NOTIFICATION_MAX];

  NotificationInputs in = clean(); in.relay_cold_open_count = 2;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *c = find(out, n, "wear.relay_cold_open");
  REQUIRE(c != nullptr);
  CHECK(c->severity == NOTIFICATION_WARNING);
  CHECK(c->token == 2);

  in = clean(); in.relay_transit_drift = true;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *d = find(out, n, "wear.relay_transit_drift");
  REQUIRE(d != nullptr);
  CHECK(d->severity == NOTIFICATION_WARNING);
  CHECK(d->sticky);

  in = clean(); in.relay_stuck_recovery_count = 4;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *r = find(out, n, "wear.stuck_relay_recovery");
  REQUIRE(r != nullptr);
  CHECK(r->severity == NOTIFICATION_INFO);
  CHECK(r->token == 4);

  in = clean(); in.relay_thermal_warning_level = 1;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "thermal.relay_thermal")->severity == NOTIFICATION_INFO);

  in.relay_thermal_warning_level = 2;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "thermal.relay_thermal")->severity == NOTIFICATION_WARNING);
}

TEST_CASE("no relay rule exists at all when relay health is unknown") {
  // A controller without the RELAY_HEALTH feature must look like a controller
  // with nothing to say, not like one reporting a confident zero.
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.relay_health_known = false;
  in.relay_life_pct = 0;
  in.relay_cold_open_count = 9;
  in.relay_transit_drift = true;
  in.relay_stuck_recovery_count = 9;
  in.relay_thermal_warning_level = 2;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(n == 0);
}

TEST_CASE("worst picks highest severity, earliest in table order among equals") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.vent_check = false;         // warning, earlier in the table
  in.gfci_count = 1;             // critical, later
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(0 == strcmp(out[notifications_worst(out, n)].id, "fault.gfci_tripped"));

  in = clean();
  in.relay_check = false;        // warning
  in.diode_check = false;        // warning, later in the table
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(0 == strcmp(out[notifications_worst(out, n)].id, "safety.relay_check"));
}

TEST_CASE("evaluate never writes past max") {
  Notification out[3];
  NotificationInputs in = clean();
  in.ground_check = in.gfci_check = in.relay_check = false;
  in.diode_check = in.vent_check = in.temp_check = false;
  CHECK(notifications_evaluate(in, out, 3) == 3);
}

TEST_CASE("every id and storage key is unique") {
  // The ack blob is keyed on `key`; a duplicate would silently mute two things.
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.ground_check = in.gfci_check = in.relay_check = false;
  in.diode_check = in.vent_check = in.temp_check = false;
  in.gfci_count = in.no_ground_count = in.stuck_relay_count = 1;
  in.temp_throttling = true;
  in.temp_c_x10 = 900;
  in.relay_life_pct = 1;
  in.relay_cold_open_count = 1;
  in.relay_transit_drift = true;
  in.relay_stuck_recovery_count = 1;
  in.relay_thermal_warning_level = 2;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(n == 13);
  for(size_t i = 0; i < n; i++) {
    for(size_t j = i + 1; j < n; j++) {
      CHECK(0 != strcmp(out[i].id, out[j].id));
      CHECK(0 != strcmp(out[i].key, out[j].key));
    }
  }
}
```

- [ ] **Step 3: Add the source to the native test build**

In `platformio.ini`, in `[env:native_test]`, append to `build_src_filter` (keep it one line):

```
build_src_filter = -<*> +<tsdb_sample.cpp> +<home_battery.cpp> +<lvgl_tft/backlight.cpp> +<loadsharing_algorithm.cpp> +<crypto/sha256.c> +<crypto/hmac_sha256.cpp> +<web_auth.cpp> +<ota_url_allow.cpp> +<http_etag.cpp> +<fault_text.cpp> +<charge_threshold.cpp> +<notifications_rules.cpp>
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_notifications_rules`
Expected: FAIL — link error, `notifications_evaluate` undefined.

- [ ] **Step 5: Implement the evaluator**

Create `src/notifications_rules.cpp`:

```cpp
#include "notifications_rules.h"

// One helper per emit keeps the table below readable and stops the bounds
// check from being repeated thirteen times.
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
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_notifications_rules`
Expected: PASS, all cases.

- [ ] **Step 7: Commit**

```bash
git add src/notifications_rules.h src/notifications_rules.cpp \
        test/test_notifications_rules/test_notifications_rules.cpp platformio.ini
git commit -m "feat(notifications): advisory rule table as a pure evaluator

Thirteen rules over a snapshot struct with no Arduino or firmware
dependencies, so the whole table is host-testable in env:native_test.
Relay rules are skipped entirely unless relay health is known, safety
tokens carry the settings-flags word, and the relay-life token steps with
severity rather than with each percent of wear."
```

---

### Task 2: Ack store

**Files:**
- Create: `src/notifications_acks.h`, `src/notifications_acks.cpp`
- Create: `test/test_notifications_acks/test_notifications_acks.cpp`
- Modify: `platformio.ini` (`[env:native_test]` `build_src_filter`)

**Interfaces:**
- Consumes: `struct Notification` from `notifications_rules.h`.
- Produces: `struct NotificationAck { char key[4]; uint32_t token; }`, `NOTIFICATION_ACK_MAX`, `notification_acks_decode`, `notification_acks_encode`, `notification_acks_is_acked`, `notification_acks_set`, `notification_acks_prune`.

Also pure and host-tested. Persistence policy (which config string, when to drop on a version change) is Task 3's job; this task owns only the encoding and the matching.

- [ ] **Step 1: Write the header**

Create `src/notifications_acks.h`:

```cpp
#ifndef _OPENEVSE_NOTIFICATIONS_ACKS_H
#define _OPENEVSE_NOTIFICATIONS_ACKS_H

// Persisted acknowledgements, encoded as "key:hextoken;" repeated.
//
// An ack stores the token the advisory carried when it was acked. It applies
// only while the live advisory still carries that token, which is what stops a
// persisted ack from ever hiding something new: a later GFI trip moves the
// count, a settings change moves the safety token, and the stored ack simply
// stops matching.
//
// Pure: no Arduino, no String, host-testable.

#include <stdint.h>
#include <stddef.h>
#include "notifications_rules.h"

#define NOTIFICATION_ACK_MAX 16

// Keys are the 2-character codes from the rule table, with room for a NUL and
// one character of growth.
struct NotificationAck {
  char     key[4];
  uint32_t token;
};

// Parse "sg:1234;fg:3;" into `out`. Tolerates an empty or malformed string by
// returning what it could parse - a corrupt blob must degrade to "nothing is
// acked", never to a crash or to a wrong mute.
size_t notification_acks_decode(const char *s, NotificationAck *out, size_t max);

// Render `acks` back to the same form. Always NUL-terminates. Returns the
// number of entries written, which is fewer than `count` if out_size ran out.
size_t notification_acks_encode(const NotificationAck *acks, size_t count, char *out, size_t out_size);

// True when `key` is acked AND the stored token still matches `token`.
bool notification_acks_is_acked(const NotificationAck *acks, size_t count, const char *key, uint32_t token);

// Record an ack, replacing any existing entry for the key. Returns the new
// count. A full store drops the request rather than evicting someone else's.
size_t notification_acks_set(NotificationAck *acks, size_t count, size_t max, const char *key, uint32_t token);

// Drop acks whose advisory is no longer live, so the blob cannot grow without
// bound across a charger's lifetime. Returns the new count.
size_t notification_acks_prune(NotificationAck *acks, size_t count, const Notification *live, size_t live_count);

#endif // _OPENEVSE_NOTIFICATIONS_ACKS_H
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_notifications_acks/test_notifications_acks.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "notifications_acks.h"
#include <string.h>

TEST_CASE("round trip") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = notification_acks_decode("sg:1234;fg:3;", acks, NOTIFICATION_ACK_MAX);
  REQUIRE(n == 2);
  CHECK(0 == strcmp(acks[0].key, "sg"));
  CHECK(acks[0].token == 0x1234);
  CHECK(0 == strcmp(acks[1].key, "fg"));
  CHECK(acks[1].token == 3);

  char buf[128];
  CHECK(notification_acks_encode(acks, n, buf, sizeof(buf)) == 2);
  CHECK(0 == strcmp(buf, "sg:1234;fg:3;"));
}

TEST_CASE("empty and malformed blobs decode to nothing acked") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  CHECK(notification_acks_decode("", acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode(nullptr, acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode(";;;", acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode("nocolon;", acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode("sg:", acks, NOTIFICATION_ACK_MAX) == 0);
  // A truncated blob still yields the entries that did parse.
  CHECK(notification_acks_decode("sg:12;bro", acks, NOTIFICATION_ACK_MAX) == 1);
}

TEST_CASE("an over-long key is rejected rather than truncated into a collision") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  CHECK(notification_acks_decode("toolong:1;", acks, NOTIFICATION_ACK_MAX) == 0);
}

TEST_CASE("is_acked requires the token to still match") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = notification_acks_decode("fg:3;", acks, NOTIFICATION_ACK_MAX);
  CHECK(notification_acks_is_acked(acks, n, "fg", 3));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "fg", 4));   // a fourth trip
  CHECK_FALSE(notification_acks_is_acked(acks, n, "sg", 3));   // different rule
}

TEST_CASE("set adds, then replaces in place") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = 0;
  n = notification_acks_set(acks, n, NOTIFICATION_ACK_MAX, "sg", 1);
  CHECK(n == 1);
  n = notification_acks_set(acks, n, NOTIFICATION_ACK_MAX, "sg", 2);
  CHECK(n == 1);
  CHECK(notification_acks_is_acked(acks, n, "sg", 2));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "sg", 1));
}

TEST_CASE("a full store drops the new ack rather than evicting an old one") {
  NotificationAck acks[2];
  size_t n = 0;
  n = notification_acks_set(acks, n, 2, "aa", 1);
  n = notification_acks_set(acks, n, 2, "bb", 1);
  n = notification_acks_set(acks, n, 2, "cc", 1);
  CHECK(n == 2);
  CHECK(notification_acks_is_acked(acks, n, "aa", 1));
  CHECK(notification_acks_is_acked(acks, n, "bb", 1));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "cc", 1));
}

TEST_CASE("prune drops acks whose advisory is no longer live") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = notification_acks_decode("sg:1;fg:2;wl:1;", acks, NOTIFICATION_ACK_MAX);
  REQUIRE(n == 3);

  Notification live[2];
  live[0].id = "safety.ground_check"; live[0].key = "sg";
  live[0].category = NOTIFICATION_SAFETY; live[0].severity = NOTIFICATION_CRITICAL;
  live[0].sticky = true; live[0].token = 1;
  live[1].id = "wear.relay_life"; live[1].key = "wl";
  live[1].category = NOTIFICATION_WEAR; live[1].severity = NOTIFICATION_INFO;
  live[1].sticky = false; live[1].token = 1;

  n = notification_acks_prune(acks, n, live, 2);
  CHECK(n == 2);
  CHECK(notification_acks_is_acked(acks, n, "sg", 1));
  CHECK(notification_acks_is_acked(acks, n, "wl", 1));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "fg", 2));
}

TEST_CASE("encode stops cleanly when the buffer runs out") {
  NotificationAck acks[3] = { { "aa", 1 }, { "bb", 2 }, { "cc", 3 } };
  char buf[8];
  size_t written = notification_acks_encode(acks, 3, buf, sizeof(buf));
  CHECK(written < 3);
  CHECK(strlen(buf) < sizeof(buf));
  // Whatever fits must still be re-readable.
  NotificationAck back[NOTIFICATION_ACK_MAX];
  CHECK(notification_acks_decode(buf, back, NOTIFICATION_ACK_MAX) == written);
}
```

- [ ] **Step 3: Add the source to the native test build**

In `platformio.ini`, `[env:native_test]`, append `+<notifications_acks.cpp>` to `build_src_filter`.

- [ ] **Step 4: Run the tests to verify they fail**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_notifications_acks`
Expected: FAIL — link error, `notification_acks_decode` undefined.

- [ ] **Step 5: Implement the store**

Create `src/notifications_acks.cpp`:

```cpp
#include "notifications_acks.h"
#include <string.h>
#include <stdio.h>

namespace {

// Parse lowercase hex without pulling in strtoul's locale baggage. Returns
// false on an empty or non-hex run, which the caller treats as a malformed
// entry and skips.
bool parse_hex(const char *s, size_t len, uint32_t &out)
{
  if(0 == len || len > 8) {
    return false;
  }
  uint32_t v = 0;
  for(size_t i = 0; i < len; i++) {
    char c = s[i];
    uint32_t d;
    if(c >= '0' && c <= '9')      { d = (uint32_t)(c - '0'); }
    else if(c >= 'a' && c <= 'f') { d = (uint32_t)(c - 'a' + 10); }
    else if(c >= 'A' && c <= 'F') { d = (uint32_t)(c - 'A' + 10); }
    else                          { return false; }
    v = (v << 4) | d;
  }
  out = v;
  return true;
}

} // namespace

size_t notification_acks_decode(const char *s, NotificationAck *out, size_t max)
{
  size_t count = 0;
  if(NULL == s) {
    return 0;
  }

  const char *p = s;
  while(*p && count < max) {
    const char *colon = strchr(p, ':');
    const char *semi  = strchr(p, ';');
    if(NULL == colon || NULL == semi || colon > semi) {
      break;                       // no well-formed entry left
    }

    size_t klen = (size_t)(colon - p);
    uint32_t token = 0;
    // sizeof(key) - 1 so a longer key is rejected outright: silently
    // truncating it could collide with a different rule's key.
    if(klen > 0 && klen <= sizeof(out[count].key) - 1 &&
       parse_hex(colon + 1, (size_t)(semi - colon - 1), token))
    {
      memcpy(out[count].key, p, klen);
      out[count].key[klen] = '\0';
      out[count].token = token;
      count++;
    }
    p = semi + 1;
  }

  return count;
}

size_t notification_acks_encode(const NotificationAck *acks, size_t count, char *out, size_t out_size)
{
  size_t written = 0;
  size_t used = 0;

  if(NULL == out || 0 == out_size) {
    return 0;
  }
  out[0] = '\0';

  for(size_t i = 0; i < count; i++) {
    char entry[24];
    int n = snprintf(entry, sizeof(entry), "%s:%x;", acks[i].key, (unsigned)acks[i].token);
    if(n < 0 || used + (size_t)n + 1 > out_size) {
      break;                       // leave what fits, still well-formed
    }
    memcpy(out + used, entry, (size_t)n);
    used += (size_t)n;
    out[used] = '\0';
    written++;
  }

  return written;
}

bool notification_acks_is_acked(const NotificationAck *acks, size_t count, const char *key, uint32_t token)
{
  for(size_t i = 0; i < count; i++) {
    if(0 == strcmp(acks[i].key, key)) {
      return acks[i].token == token;
    }
  }
  return false;
}

size_t notification_acks_set(NotificationAck *acks, size_t count, size_t max, const char *key, uint32_t token)
{
  for(size_t i = 0; i < count; i++) {
    if(0 == strcmp(acks[i].key, key)) {
      acks[i].token = token;
      return count;
    }
  }
  if(count >= max || strlen(key) > sizeof(acks[0].key) - 1) {
    return count;
  }
  strncpy(acks[count].key, key, sizeof(acks[count].key) - 1);
  acks[count].key[sizeof(acks[count].key) - 1] = '\0';
  acks[count].token = token;
  return count + 1;
}

size_t notification_acks_prune(NotificationAck *acks, size_t count, const Notification *live, size_t live_count)
{
  size_t kept = 0;
  for(size_t i = 0; i < count; i++) {
    bool still_live = false;
    for(size_t j = 0; j < live_count; j++) {
      if(0 == strcmp(acks[i].key, live[j].key)) {
        still_live = true;
        break;
      }
    }
    if(still_live) {
      if(kept != i) {
        acks[kept] = acks[i];
      }
      kept++;
    }
  }
  return kept;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_notifications_acks`
Expected: PASS, all cases.

- [ ] **Step 7: Commit**

```bash
git add src/notifications_acks.h src/notifications_acks.cpp \
        test/test_notifications_acks/test_notifications_acks.cpp platformio.ini
git commit -m "feat(notifications): persisted ack store keyed on the advisory token

An ack applies only while the live advisory still carries the token it was
acked at, so a stored ack can never hide something new. A malformed blob
degrades to nothing acked, an over-long key is rejected rather than
truncated into a collision with another rule, and a full store drops the
new ack instead of evicting someone else's."
```

---

### Task 3: The notifications MicroTask

**Files:**
- Create: `src/notifications.h`, `src/notifications.cpp`
- Modify: `src/app_config.cpp` (two persisted options)
- Modify: `src/app_config.h` (their extern declarations)
- Modify: `src/main.cpp` (call `begin`)

**Interfaces:**
- Consumes: `notifications_evaluate`, `notifications_max_severity`, `notifications_worst`, `NOTIFICATION_MAX`, `struct Notification`, `struct NotificationInputs` (Task 1); the whole `notifications_acks.h` API (Task 2).
- Produces: `class Notifications` with `void begin(EvseManager &evse)`, `size_t count()`, `uint8_t maxSeverity()`, `bool worst(const char *&id, uint8_t &severity)`, `bool ack(const char *id)`, `void serialize(JsonDocument &doc)`; and the global `extern Notifications notifications;`.

- [ ] **Step 1: Add the persisted config options**

In `src/app_config.cpp`, beside the other `String` globals, add:

```cpp
String notification_acks;
String notification_acks_fw;
```

and in the `opts[]` array, beside the other `ConfigOptDefinition<String>` entries:

```cpp
  // Advisory acknowledgements: "key:hextoken;" repeated, plus the firmware
  // version that wrote them. A version change drops the lot - a firmware
  // update is a service event, where a power cut is not.
  new ConfigOptDefinition<String>(notification_acks, "", "notification_acks", "nak"),
  new ConfigOptDefinition<String>(notification_acks_fw, "", "notification_acks_fw", "nkv"),
```

In `src/app_config.h`, beside the other externs:

```cpp
extern String notification_acks;
extern String notification_acks_fw;
```

Do **not** add either to the `/config` JSON output — they are internal state, not settings.

- [ ] **Step 2: Write the header**

Create `src/notifications.h`:

```cpp
#ifndef _OPENEVSE_NOTIFICATIONS_H
#define _OPENEVSE_NOTIFICATIONS_H

// The advisory engine: reads EvseMonitor's cache on a slow loop, evaluates the
// rule table, applies persisted acks and publishes the result.
//
// Advisories sit between live faults (fault_screen, full screen, blocks
// charging) and the event log (history). Nothing here ever describes a
// condition that is stopping the EVSE charging right now.

#ifndef EVSE_NOTIFICATIONS_LOOP_TIME
#define EVSE_NOTIFICATIONS_LOOP_TIME 5000
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "notifications_rules.h"
#include "notifications_acks.h"

class Notifications : public MicroTasks::Task
{
  private:
    EvseManager *_evse;

    Notification _live[NOTIFICATION_MAX];
    size_t       _count;

    NotificationAck _acks[NOTIFICATION_ACK_MAX];
    size_t          _ack_count;

    // first_seen / last_seen per live advisory, parallel to _live.
    uint32_t _first_seen[NOTIFICATION_MAX];
    uint32_t _last_seen[NOTIFICATION_MAX];

    // One bit per rule index, RAM only: caps event-log rows at one per
    // advisory per boot so a flapping thermal rule cannot flood History.
    uint32_t _logged;

    void gather(NotificationInputs &in);
    void applyAcks();
    void saveAcks();
    int  indexOfKey(const char *key);   // -1 when absent

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    Notifications();

    void begin(EvseManager &evse);

    // Unmuted advisories only - what the badge and the LCD count.
    size_t count();
    uint8_t maxSeverity();

    // The advisory the LCD should name. False when there is nothing to name.
    bool worst(const char *&id, uint8_t &severity);

    // Ack by id. False when the id is not currently live.
    bool ack(const char *id);

    // The full list, muted entries included.
    void serialize(JsonDocument &doc);
};

extern Notifications notifications;

#endif // _OPENEVSE_NOTIFICATIONS_H
```

- [ ] **Step 3: Implement it**

Create `src/notifications.cpp`:

```cpp
#ifdef ENABLE_DEBUG
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "notifications.h"
#include "app_config.h"
#include "emonesp.h"          // currentfirmware
#include "event_log.h"
#include "temp_throttle.h"
#include "divert.h"
#include "current_shaper.h"
#include "web_server.h"       // event_send()

#include <string.h>

Notifications notifications;

Notifications::Notifications() :
  MicroTasks::Task(),
  _evse(NULL),
  _count(0),
  _ack_count(0),
  _logged(0)
{
  memset(_first_seen, 0, sizeof(_first_seen));
  memset(_last_seen, 0, sizeof(_last_seen));
}

void Notifications::begin(EvseManager &evse)
{
  _evse = &evse;
  MicroTask.startTask(this);
}

void Notifications::setup()
{
  // A firmware update is a service event, so the acks that preceded it are
  // dropped wholesale. A power cut is not, so they survive one.
  if(notification_acks_fw != currentfirmware) {
    DBUGF("Notifications: firmware changed (%s -> %s), dropping acks",
          notification_acks_fw.c_str(), currentfirmware.c_str());
    notification_acks = "";
    notification_acks_fw = currentfirmware;
    config_commit();
  }
  _ack_count = notification_acks_decode(notification_acks.c_str(), _acks, NOTIFICATION_ACK_MAX);
}

void Notifications::gather(NotificationInputs &in)
{
  EvseMonitor &m = _evse->getMonitor();

  in.ground_check = m.isGroundCheckEnabled();
  in.gfci_check   = m.isGfiTestEnabled();
  in.relay_check  = m.isStuckRelayCheckEnabled();
  in.diode_check  = m.isDiodeCheckEnabled();
  in.vent_check   = m.isVentRequiredEnabled();
  in.temp_check   = m.isTemperatureCheckEnabled();

  in.gfci_count        = (uint32_t)m.getFaultCountGFCI();
  in.no_ground_count   = (uint32_t)m.getFaultCountNoGround();
  in.stuck_relay_count = (uint32_t)m.getFaultCountStuckRelay();

  in.temp_throttling = temp_throttle.isThrottling();
  in.temp_valid   = false;
  in.temp_c_x10   = 0;
  for(uint8_t s = 0; s < EVSE_MONITOR_TEMP_COUNT; s++) {
    if(m.isTemperatureValid(s)) {
      int32_t t = (int32_t)(m.getTemperature(s) * 10.0);
      if(!in.temp_valid || t > in.temp_c_x10) {
        in.temp_c_x10 = t;
      }
      in.temp_valid = true;
    }
  }
  in.panic_temp_c = (int32_t)m.getPanicTemperature();

  in.relay_health_known = m.isRelayHealthKnown();
  in.relay_life_pct                = m.getRelayLifeRemainingPct();
  in.relay_cold_open_count         = m.getRelayColdOpenCount();
  in.relay_transit_drift           = m.isRelayTransitDriftWarning();
  in.relay_thermal_warning_level   = m.getRelayThermalWarningLevel();
  in.relay_stuck_recovery_count    = m.getRelayStuckRecoveryCount();

  in.settings_flags = m.getSettingsFlags();
}

int Notifications::indexOfKey(const char *key)
{
  for(size_t i = 0; i < _count; i++) {
    if(0 == strcmp(_live[i].key, key)) {
      return (int)i;
    }
  }
  return -1;
}

void Notifications::saveAcks()
{
  char buf[192];
  notification_acks_encode(_acks, _ack_count, buf, sizeof(buf));
  if(notification_acks != buf) {
    notification_acks = buf;
    notification_acks_fw = currentfirmware;
    config_commit();
  }
}

unsigned long Notifications::loop(MicroTasks::WakeReason reason)
{
  Notification previous[NOTIFICATION_MAX];
  uint32_t previous_first[NOTIFICATION_MAX];
  size_t previous_count = _count;
  memcpy(previous, _live, sizeof(previous));
  memcpy(previous_first, _first_seen, sizeof(previous_first));

  NotificationInputs in;
  gather(in);
  _count = notifications_evaluate(in, _live, NOTIFICATION_MAX);

  uint32_t now = (uint32_t)(millis() / 1000);
  bool changed = (_count != previous_count);

  for(size_t i = 0; i < _count; i++) {
    // Carry first_seen across from the previous pass; a rule that was absent
    // last pass is a raise edge.
    bool was_live = false;
    for(size_t j = 0; j < previous_count; j++) {
      if(0 == strcmp(previous[j].key, _live[i].key)) {
        _first_seen[i] = previous_first[j];
        was_live = true;
        if(previous[j].severity != _live[i].severity ||
           previous[j].token != _live[i].token)
        {
          changed = true;
        }
        break;
      }
    }
    _last_seen[i] = now;
    if(!was_live) {
      _first_seen[i] = now;
      changed = true;

      // Raise edge: one event-log row, at most once per rule per boot.
      if(0 == (_logged & (1u << i))) {
        _logged |= (1u << i);
        EvseMonitor &m = _evse->getMonitor();
        // Argument list copied from EvseManager's own call site
        // (src/evse_man.cpp:386) so the row is shaped like every other row.
        eventLog.log(EventType::Notification, _evse->getState(),
                     m.getEvseState(), m.getFlags(), m.getPilotState(),
                     m.getPilot(), m.getSessionEnergy(), m.getSessionElapsed(),
                     m.getTemperature(EVSE_MONITOR_TEMP_MONITOR),
                     m.getTemperature(EVSE_MONITOR_TEMP_MAX),
                     divert.isActive(), shaper.getState());
      }
    }
  }

  // Acks for advisories that have gone away are dead weight; drop them so the
  // persisted blob cannot grow across a charger's lifetime.
  size_t pruned = notification_acks_prune(_acks, _ack_count, _live, _count);
  if(pruned != _ack_count) {
    _ack_count = pruned;
    saveAcks();
  }

  if(changed) {
    DynamicJsonDocument doc(1024);
    JsonObject o = doc.createNestedObject("notifications");
    o["count"] = count();
    o["severity"] = maxSeverity();
    event_send(doc);
  }

  return EVSE_NOTIFICATIONS_LOOP_TIME;
}

size_t Notifications::count()
{
  size_t n = 0;
  for(size_t i = 0; i < _count; i++) {
    if(!notification_acks_is_acked(_acks, _ack_count, _live[i].key, _live[i].token)) {
      n++;
    }
  }
  return n;
}

uint8_t Notifications::maxSeverity()
{
  uint8_t worst_sev = NOTIFICATION_INFO;
  for(size_t i = 0; i < _count; i++) {
    if(notification_acks_is_acked(_acks, _ack_count, _live[i].key, _live[i].token)) {
      continue;
    }
    if(_live[i].severity > worst_sev) {
      worst_sev = _live[i].severity;
    }
  }
  return worst_sev;
}

bool Notifications::worst(const char *&id, uint8_t &severity)
{
  int best = -1;
  for(size_t i = 0; i < _count; i++) {
    if(notification_acks_is_acked(_acks, _ack_count, _live[i].key, _live[i].token)) {
      continue;
    }
    if(best < 0 || _live[i].severity > _live[best].severity) {
      best = (int)i;
    }
  }
  if(best < 0) {
    return false;
  }
  id = _live[best].id;
  severity = _live[best].severity;
  return true;
}

bool Notifications::ack(const char *id)
{
  for(size_t i = 0; i < _count; i++) {
    if(0 == strcmp(_live[i].id, id)) {
      _ack_count = notification_acks_set(_acks, _ack_count, NOTIFICATION_ACK_MAX,
                                         _live[i].key, _live[i].token);
      saveAcks();
      return true;
    }
  }
  return false;
}

void Notifications::serialize(JsonDocument &doc)
{
  static const char *severity_name[] = { "info", "warning", "critical" };
  static const char *category_name[] = { "safety", "fault", "wear", "thermal" };

  doc["count"] = count();
  doc["max_severity"] = severity_name[maxSeverity()];

  JsonArray list = doc.createNestedArray("notifications");
  for(size_t i = 0; i < _count; i++) {
    JsonObject o = list.createNestedObject();
    o["id"] = _live[i].id;
    o["category"] = category_name[_live[i].category];
    o["severity"] = severity_name[_live[i].severity];
    o["sticky"] = _live[i].sticky;
    o["acked"] = notification_acks_is_acked(_acks, _ack_count, _live[i].key, _live[i].token);
    o["first_seen"] = _first_seen[i];
    o["last_seen"] = _last_seen[i];
  }
}
```

- [ ] **Step 4: Wire it into main**

In `src/main.cpp`, add `#include "notifications.h"` beside `#include "boost.h"`, and after the `boost.begin(evse);` line add:

```cpp
  notifications.begin(evse);
  DBUGF("After notifications.begin: %d", ESPAL.getFreeHeap());
```

- [ ] **Step 5: Build for hardware and fix what does not compile**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: SUCCESS.

Every accessor used above was checked against `src/evse_monitor.h` on this branch. Note `currentfirmware` is declared in `src/emonesp.h`, not `app_config.h`, and `divert.cpp` / `current_shaper.cpp` supply `divert.isActive()` and `shaper.getState()` — add those includes as the compiler asks for them.

- [ ] **Step 6: Run the native suite to confirm nothing regressed**

Run: `~/.platformio/penv/bin/pio test -e native_test`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/notifications.h src/notifications.cpp src/app_config.cpp \
        src/app_config.h src/main.cpp
git commit -m "feat(notifications): advisory engine on a 5s MicroTask

Reads EvseMonitor's cache - no new RAPI traffic - evaluates the rule
table, applies persisted acks and pushes a websocket event on change.
Acks are dropped wholesale when the firmware version changes, since an
update is a service event where a power cut is not, and pruned when their
advisory goes away so the blob cannot grow across a charger's lifetime.
Event-log rows fire on the raise edge only, capped once per rule per boot
by a RAM bitmask."
```

---

### Task 4: HTTP API and the two `/status` fields

**Files:**
- Create: `src/web_server_notifications.cpp`
- Modify: `src/web_server.h` (declare the two handlers)
- Modify: `src/web_server.cpp` (register routes; extend `buildStatus()`)

**Interfaces:**
- Consumes: `notifications.serialize(JsonDocument &)`, `notifications.ack(const char *)`, `notifications.count()`, `notifications.maxSeverity()` (Task 3).
- Produces: `GET /notifications`, `POST /notifications/ack?id=<id>`, and a `notifications` object in `/status`.

**Deliberate deviation from the spec.** §7 writes the ack route as `POST /notifications/<id>/ack`; this plan uses `POST /notifications/ack?id=<id>`. The Mongoose route table matches anchored prefixes, so a path-segment id would need its own parsing for no benefit, and every other actuator on this server already takes its argument as a query parameter. Update the spec's §7 when this task lands so the two do not drift.

- [ ] **Step 1: Write the endpoint file**

Create `src/web_server_notifications.cpp`:

```cpp
#ifdef ENABLE_DEBUG
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "web_server.h"
#include "notifications.h"

// -------------------------------------------------------------------
// Returns the advisory list, muted entries included - the GUI needs to
// render them as muted rather than have them vanish.
// url: /notifications
// -------------------------------------------------------------------
void handleNotifications(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  const size_t capacity = JSON_ARRAY_SIZE(NOTIFICATION_MAX) +
                          NOTIFICATION_MAX * JSON_OBJECT_SIZE(7) +
                          JSON_OBJECT_SIZE(3) + 512;
  DynamicJsonDocument doc(capacity);
  notifications.serialize(doc);
  response->setCode(200);
  serializeJson(doc, *response);
  request->send(response);
}

// -------------------------------------------------------------------
// Acknowledge one advisory. A sticky advisory is muted rather than
// cleared - it stays in the list and keeps its settings-page marker.
// url: /notifications/ack?id=safety.ground_check
// -------------------------------------------------------------------
void handleNotificationAck(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }
  if(!actuatorMethodAllowed(request, response)) {
    return;
  }

  String id = request->getParam("id");
  if(0 == id.length()) {
    response->setCode(400);
    response->print("id required");
    request->send(response);
    return;
  }

  if(!notifications.ack(id.c_str())) {
    response->setCode(404);
    response->print("no such active notification");
    request->send(response);
    return;
  }

  response->setCode(200);
  response->print("acknowledged");
  request->send(response);
  DBUGF("Notification acked: %s", id.c_str());
}
```

- [ ] **Step 2: Declare the handlers and un-static the CSRF guard**

In `src/web_server.h`, beside the other handler declarations:

```cpp
void handleNotifications(MongooseHttpServerRequest *request);
void handleNotificationAck(MongooseHttpServerRequest *request);
bool actuatorMethodAllowed(MongooseHttpServerRequest *request,
                           MongooseHttpServerResponseStream *response);
```

In `src/web_server.cpp`, drop the `static` from `actuatorMethodAllowed`'s definition so the new file can use the same guard. Do not copy the guard — one implementation, one behaviour.

- [ ] **Step 3: Register the routes**

In `src/web_server.cpp`, beside `server.on("/boost", handleBoost);`:

```cpp
  server.on("/notifications/ack$", handleNotificationAck);
  server.on("/notifications$", handleNotifications);
```

Register `/notifications/ack$` **first**: the route table is matched in order and the anchored patterns must not shadow each other.

- [ ] **Step 4: Add the two status fields**

In `src/web_server.cpp`, inside `buildStatus()`, beside the other module blocks:

```cpp
  // Exactly two fields: both UIs need a badge without a second round trip,
  // and nothing more belongs in a payload the HA integration already polls
  // hard. The list lives on /notifications.
  JsonObject notify = doc.createNestedObject("notifications");
  notify["count"] = notifications.count();
  notify["severity"] = notifications.maxSeverity();
```

Add `#include "notifications.h"` at the top. Then check the comment above `buildStatus`'s capacity constant — it names the field count the document is sized for. Raise that constant to cover three more entries (the object plus its two members) and update the comment to match; an undersized document silently truncates.

- [ ] **Step 5: Build and smoke-test against the emulator**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: SUCCESS.

Then, with a device or the emulator reachable at `$DEV`:

```bash
curl -s http://$DEV/notifications | python3 -m json.tool
curl -s http://$DEV/status | python3 -c 'import sys,json; print(json.load(sys.stdin)["notifications"])'
curl -s "http://$DEV/notifications/ack?id=safety.vent_check"          # expect 403, headerless GET
curl -s -X POST "http://$DEV/notifications/ack?id=safety.vent_check"  # expect 200 or 404
curl -s -X POST "http://$DEV/notifications/ack?id=nope"               # expect 404
curl -s -X POST "http://$DEV/notifications/ack"                       # expect 400
```

Expected: the list parses, `/status` carries `{"count": N, "severity": S}`, the bare GET is refused by the CSRF guard, and the three POST cases return 200/404/400 as listed.

- [ ] **Step 6: Commit**

```bash
git add src/web_server_notifications.cpp src/web_server.cpp src/web_server.h
git commit -m "feat(notifications): /notifications endpoints and two status fields

GET /notifications serves the list with muted entries included, so the GUI
can render them as muted rather than have them vanish. POST
/notifications/ack takes the id as a query parameter and reuses the
existing actuator CSRF guard rather than copying it. /status gains one
object with count and severity and nothing else."
```

---

### Task 5: LCD — amber border and advisory line on the charge screen

**Files:**
- Modify: `src/lvgl_tft/charge_screen.h` (two fields on `ChargeScreenData`)
- Modify: `src/lvgl_tft/charge_screen.cpp` (border style, line rendering)
- Modify: `src/lcd_lvgl.cpp` (fill the fields)

**Interfaces:**
- Consumes: `notifications.worst(const char *&, uint8_t &)`, `notifications.count()` (Task 3).
- Produces: `ChargeScreenData::notify_line` (`const char *`, `""` when none) and `ChargeScreenData::notify_active` (`bool`).

- [ ] **Step 1: Add the fields**

In `src/lvgl_tft/charge_screen.h`, at the end of `struct ChargeScreenData`, after `msg_line`:

```cpp
  // Advisory state (notifications.h). notify_active drives the amber
  // perimeter border; notify_line is the worst advisory's short text, with a
  // "+N" suffix when there are more. A transient msg_line outranks it: OTA
  // progress is time-critical where an advisory is not.
  bool     notify_active;
  const char *notify_line;    // "" when there is nothing to say
```

- [ ] **Step 2: Draw the border**

In `src/lvgl_tft/charge_screen.cpp`, in `charge_screen_build()`, immediately after the screen object is created and its background set:

```cpp
  // Amber perimeter, hidden until an advisory is active. A style on the screen
  // object rather than a widget, so it is drawn with the screen background and
  // costs no extra invalidation. Amber only: red belongs to the fault screen,
  // and a red border here would read as "this charger has stopped".
  lv_obj_set_style_border_color(scr, COL_WARN, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_border_side(scr, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_radius(scr, 0, 0);
```

Add `#define COL_WARN NS_WARNING` to the colour block at the top of the file if it is not already there.

- [ ] **Step 3: Render both in the update path**

In `charge_screen_update()`, where `msg_lbl` is currently set, replace that block with:

```cpp
  // A transient message outranks an advisory: OTA progress is time-critical,
  // an advisory is not.
  const char *line = (d.msg_line && d.msg_line[0]) ? d.msg_line :
                     (d.notify_line && d.notify_line[0]) ? d.notify_line : NULL;
  if(line) {
    lv_label_set_text(msg_lbl, line);
    lv_obj_set_style_text_color(msg_lbl,
        (d.msg_line && d.msg_line[0]) ? COL_ACCENT : COL_WARN, 0);
    lv_obj_clear_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_set_style_border_width(charge_scr, d.notify_active ? 4 : 0, 0);
```

- [ ] **Step 4: Fill the fields**

In `src/lcd_lvgl.cpp`, where `ChargeScreenData` is populated, add:

```cpp
  // Advisory line: the worst one, named, with a count of the rest. The border
  // is the "is there anything?" signal; this says what.
  static char notify_buf[48];
  const char *notify_id = NULL;
  uint8_t notify_sev = 0;
  size_t notify_count = notifications.count();
  if(notifications.worst(notify_id, notify_sev)) {
    if(notify_count > 1) {
      snprintf(notify_buf, sizeof(notify_buf), LV_SYMBOL_WARNING " %s  +%u",
               notification_short_text(notify_id), (unsigned)(notify_count - 1));
    } else {
      snprintf(notify_buf, sizeof(notify_buf), LV_SYMBOL_WARNING " %s",
               notification_short_text(notify_id));
    }
    data.notify_line = notify_buf;
    data.notify_active = true;
  } else {
    data.notify_line = "";
    data.notify_active = false;
  }
```

Add `#include "notifications.h"` at the top of `src/lcd_lvgl.cpp`.

- [ ] **Step 5: Add the short-text table**

The LCD carries its own short English strings, exactly as `fault_text.cpp` already does for fault states — ids are what the GUI translates, and no translation table belongs on the panel.

Add to `src/notifications.h`:

```cpp
// Short, upper-case English for the LCD line. Never a number: the detail lives
// on the GUI's Monitoring -> Health page, which this points people at.
const char *notification_short_text(const char *id);
```

and to `src/notifications.cpp`:

```cpp
const char *notification_short_text(const char *id)
{
  if(0 == strcmp(id, "safety.ground_check"))      return "GROUND CHECK OFF";
  if(0 == strcmp(id, "safety.gfci_check"))        return "GFCI SELF TEST OFF";
  if(0 == strcmp(id, "safety.relay_check"))       return "RELAY CHECK OFF";
  if(0 == strcmp(id, "safety.diode_check"))       return "DIODE CHECK OFF";
  if(0 == strcmp(id, "safety.vent_check"))        return "VENT CHECK OFF";
  if(0 == strcmp(id, "safety.temp_check"))        return "TEMP MONITOR OFF";
  if(0 == strcmp(id, "fault.gfci_tripped"))       return "GFCI HAS TRIPPED";
  if(0 == strcmp(id, "fault.no_ground"))          return "GROUND FAULT LOGGED";
  if(0 == strcmp(id, "fault.stuck_relay"))        return "STUCK RELAY LOGGED";
  if(0 == strcmp(id, "thermal.throttling"))       return "REDUCING CURRENT - HOT";
  if(0 == strcmp(id, "thermal.high_temp"))        return "TEMPERATURE HIGH";
  if(0 == strcmp(id, "thermal.relay_thermal"))    return "RELAY RUNNING HOT";
  if(0 == strcmp(id, "wear.relay_life"))          return "RELAY NEAR END OF LIFE";
  if(0 == strcmp(id, "wear.relay_transit_drift")) return "RELAY SLOWING";
  if(0 == strcmp(id, "wear.relay_cold_open"))     return "RELAY OPENED UNDER LOAD";
  if(0 == strcmp(id, "wear.stuck_relay_recovery"))return "RELAY RECOVERY RUN";
  return "CHECK THE APP";
}
```

- [ ] **Step 6: Build and render on the host harness**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: SUCCESS.

```bash
~/.platformio/penv/bin/pio run -e native_openevse_lvgl
.pio/build/native_openevse_lvgl/program --dump-lvgl-screens /tmp/lvgl-screens
```
Expected: PPM files written to `/tmp/lvgl-screens`. Open the charge screen and confirm the border and the line render without overlapping the clock or the chip row. Use the host harness rather than the bench — that is what it is for (`docs/developer/building.md:160`).

- [ ] **Step 7: Commit**

```bash
git add src/lvgl_tft/charge_screen.h src/lvgl_tft/charge_screen.cpp \
        src/lcd_lvgl.cpp src/notifications.h src/notifications.cpp
git commit -m "feat(notifications): amber border and advisory line on the charge screen

A 480x320 perimeter is legible from across a garage where a corner pill is
not, and as a style on the screen object it draws with the background
rather than adding invalidation. Amber only - red stays reserved for the
fault screen. One named advisory with a +N for the rest; a transient
message outranks it, because OTA progress is time-critical and an advisory
is not."
```

---

### Task 6: LCD — the same on the standby screen

**Files:**
- Modify: `src/lvgl_tft/standby_screen.h` (two fields on `StandbyScreenData`)
- Modify: `src/lvgl_tft/standby_screen.cpp` (border, second top-strip line)
- Modify: `src/lcd_lvgl.cpp` (fill the fields)

**Interfaces:**
- Consumes: `notifications.worst()`, `notifications.count()`, `notification_short_text()` (Tasks 3 and 5).
- Produces: `StandbyScreenData::notify_line`, `StandbyScreenData::notify_active`.

Standby has no second top-strip line — its `hostname`/`ip` occupy that space. An advisory takes precedence over the address: a warning matters more than knowing where to point a browser.

- [ ] **Step 1: Add the fields**

In `src/lvgl_tft/standby_screen.h`, at the end of `struct StandbyScreenData`:

```cpp
  // Advisory state (notifications.h). notify_active drives the amber
  // perimeter border. notify_line REPLACES the hostname/ip line while it is
  // set: a warning outranks knowing where to point a browser.
  bool     notify_active;
  const char *notify_line;    // "" when there is nothing to say
```

- [ ] **Step 2: Draw the border**

In `standby_screen_build()`, right after the screen object is created:

```cpp
  // Same amber perimeter as the charge screen, hidden until an advisory is
  // active. See charge_screen.cpp for why it is amber and not red.
  lv_obj_set_style_border_color(scr, NS_WARNING, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_border_side(scr, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_radius(scr, 0, 0);
```

- [ ] **Step 3: Render the line in place of the address**

In `standby_screen_update()`, where the hostname/ip label is set, replace that block with:

```cpp
  if(d.notify_line && d.notify_line[0]) {
    lv_label_set_text(hostip_lbl, d.notify_line);
    lv_obj_set_style_text_color(hostip_lbl, NS_WARNING, 0);
  } else {
    char addr[64];
    snprintf(addr, sizeof(addr), "%s   %s", d.hostname, d.ip);
    lv_label_set_text(hostip_lbl, addr);
    lv_obj_set_style_text_color(hostip_lbl, NS_TEXTDIM, 0);
  }

  lv_obj_set_style_border_width(standby_scr, d.notify_active ? 4 : 0, 0);
```

Use whatever the existing label and screen-object variables are actually called in that file; the names above follow the charge screen's convention.

- [ ] **Step 4: Fill the fields**

In `src/lcd_lvgl.cpp`, where `StandbyScreenData` is populated, add the same block as Task 5 Step 4, writing into the standby struct. Reuse one static buffer across both screens — only one is ever on display.

- [ ] **Step 5: Build and render**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: SUCCESS.

```bash
~/.platformio/penv/bin/pio run -e native_openevse_lvgl
.pio/build/native_openevse_lvgl/program --dump-lvgl-screens /tmp/lvgl-screens
```
Expected: PPM files written. Check the standby screen: border present, advisory text where the address normally sits, and the address back when there is no advisory.

- [ ] **Step 6: Commit**

```bash
git add src/lvgl_tft/standby_screen.h src/lvgl_tft/standby_screen.cpp src/lcd_lvgl.cpp
git commit -m "feat(notifications): amber border and advisory line on standby

Standby's top strip has one spare line and the address is already in it,
so an advisory takes it: a warning outranks knowing where to point a
browser, and the address returns as soon as the advisory clears."
```

---

### Task 7: Event-log notification rows (independently droppable)

**Files:**
- Modify: `src/event_log.h`, `src/event_log.cpp`
- Modify: `src/web_server_events.cpp`
- Modify: `src/notifications.cpp` (pass the id)

**Interfaces:**
- Consumes: `EventLog::log(...)` as called in Task 3.
- Produces: an optional trailing `const char *notification` parameter on `EventLog::log()` and a `notification` key in the `/logs/<n>` JSON.

**Drop this task if the schema change is not wanted.** Nothing else in the plan depends on it — Task 3 already logs a row; this only adds the id that says *which* advisory raised. Without it the row records that an advisory appeared but not which one, which is close to useless, so it is drop-it-or-do-it, not drop-it-and-half-do-it.

- [ ] **Step 1: Extend the log signature**

In `src/event_log.h`, add a defaulted trailing parameter to `log()`:

```cpp
  // `notification` is the advisory id for EventType::Notification rows and
  // NULL for every other row, which is what every existing caller passes by
  // omission. Older stored rows simply lack the field.
  void log(EventType type, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint8_t pilotState, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode, uint8_t shaper, const char *notification = NULL);
```

Add the same trailing `const char *notification` to the `enumerate()` callback signature.

- [ ] **Step 2: Persist and read it back**

In `src/event_log.cpp`, in `log()`, write the field only when it is set, so existing rows and non-notification rows are byte-identical to today:

```cpp
  if(notification) {
    event["notification"] = notification;
  }
```

In `enumerate()`, read it back with `event["notification"] | ""` and pass it to the callback. A row without the key yields `""`, which is what every pre-existing row will do.

- [ ] **Step 3: Serve it**

In `src/web_server_events.cpp`, in the per-entry serialisation, add the field only when non-empty:

```cpp
      if(notification && notification[0]) {
        event["notification"] = notification;
      }
```

Update the lambda's parameter list to match the new `enumerate()` signature.

- [ ] **Step 4: Pass the id from the engine**

In `src/notifications.cpp`, in the raise-edge branch, add `, _live[i].id` as the final argument to the `eventLog.log(...)` call.

- [ ] **Step 5: Build and verify against a device**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: SUCCESS.

With a device at `$DEV`, disable a safety check to force a raise edge, then:

```bash
curl -s http://$DEV/logs | python3 -m json.tool
curl -s http://$DEV/logs/<max> | python3 -m json.tool
```

Expected: the newest entry has `"type": "notification"` and `"notification": "safety.vent_check"`. Fetch an older entry and confirm it has no `notification` key and still parses.

- [ ] **Step 6: Commit**

```bash
git add src/event_log.h src/event_log.cpp src/web_server_events.cpp src/notifications.cpp
git commit -m "feat(notifications): carry the advisory id on event-log rows

EventType::Notification already existed in the enum but the log record is a
fixed EVSE-state schema with nowhere to put an id, so a row could say an
advisory appeared but not which one. Adds an optional trailing field,
written only when set, so existing rows and every other caller are
unchanged."
```

---

## Verification

After Task 6 (or Task 7 if taken):

- [ ] `~/.platformio/penv/bin/pio test -e native_test` — the whole native suite passes.
- [ ] `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1` — builds.
- [ ] `~/.platformio/penv/bin/pio run -e openevse_wifi_v1` — builds, and **record the flash percentage**. The spec's budget is ~6 KB flash and ~500 B RAM for the firmware half; this env is the 4 MB squeeze, so if the delta is materially worse than that, say so rather than shipping it quietly.
- [ ] On hardware: toggle a safety check off via the GUI's Charger settings, confirm within ~5 s that the amber border appears, the line names it, `/status` shows `count: 1`, and `/notifications` lists it. Turn it back on and confirm all three clear.
- [ ] On hardware: ack a sticky advisory, confirm the border and line clear while `/notifications` still lists it with `acked: true`. Reboot and confirm the ack survived. Change any other EVSE setting and confirm the advisory re-raises.
- [ ] On hardware: force a fault-screen condition and confirm the advisory border and line are not visible underneath it.
