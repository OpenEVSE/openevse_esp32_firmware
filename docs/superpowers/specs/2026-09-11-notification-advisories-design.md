# Notification Advisories — Design

**Goal:** surface the conditions a user ought to know about but which are not
faults — safety checks switched off, faults that happened earlier and cleared,
relay wear, temperature — as a single ranked list, drawn as one icon on the LCD
and one badge in the web UI.

**Origin:** Chris asked for "a notification icon on the LCD and in the GUI for
things like Not all Required Safety Features are enabled, new errors, relay
life, temperature."

**Status:** design, not yet approved for implementation.

---

## 1. Why this is cheap

Three of the four sources Chris named are already in the firmware and simply
have nowhere to appear:

| Source | Today |
|---|---|
| Safety features | `EvseMonitor::isGfiTestEnabled()`, `isGroundCheckEnabled()`, `isStuckRelayCheckEnabled()`, `isDiodeCheckEnabled()`, `isVentRequiredEnabled()`, `isTemperatureCheckEnabled()` — all live, all already serialised into `/config` as `gfci_check`, `ground_check`, `relay_check`, `diode_check`, `vent_check`, `temp_check`. Nothing ever reads them back as an advisory. |
| Earlier faults | `EvseMonitor` already polls `$GF` into `_gfci_count` / `_nognd_count` / `_stuck_count` and exposes `getFaultCountGFCI()` / `getFaultCountNoGround()` / `getFaultCountStuckRelay()`; the latched `OPENEVSE_VFLAG_GFI_TRIPPED`, `OPENEVSE_VFLAG_NOGND_TRIPPED` and `OPENEVSE_VFLAG_HARD_FAULT` bits are in `getFlags()`. |
| Temperature | `EvseMonitor` temperature sensors plus `TempThrottle::isThrottling()` and `getPanicTemperature()`. |
| Relay life | **Not available.** See §8. |

`event_log.h` already declares an unused `EventType::Notification`, so even the
log slot is cut.

## 2. Scope

**In:** an advisory engine in the firmware; an HTTP + websocket API; a chip on
the LVGL screens; a badge and panel in the web UI (separate repo, separate PR).

**Out:** outbound push (email, HA notifications, MQTT topics beyond the existing
state publish); any change to fault handling or the charge state machine; OCPP
status mapping; per-advisory user configuration beyond what §8 needs.

## 3. The tier boundary

This is the part most likely to go wrong, so it is a rule, not a guideline.

```
live fault      fault_screen.cpp, full screen, blocks charging
advisory        THIS — one chip, never blocks, never takes the screen
event log       history, only interesting when you go looking
```

**An advisory never describes a condition that is stopping the EVSE from
charging right now.** If it is stopping charging it is a fault and the fault
screen owns it. `wear.relay_life` at 100% is an advisory; `OPENEVSE_STATE_STUCK_RELAY`
is a fault. `fault.gfci_tripped` is an advisory *because it has already
cleared* — while the GFI fault is live, the fault screen has it.

Consequence: the LCD chip is suppressed entirely while the fault screen is up.
There is no case where both should be competing for the user's attention.

## 4. Data model

```cpp
enum AdvisorySeverity : uint8_t { Advisory_Info = 0, Advisory_Warning, Advisory_Critical };
enum AdvisoryCategory : uint8_t { Advisory_Safety = 0, Advisory_Fault, Advisory_Wear, Advisory_Thermal };

struct Advisory {
  const char      *id;        // stable, dotted, e.g. "safety.ground_check"
  AdvisoryCategory category;
  AdvisorySeverity severity;
  bool             sticky;    // true: cannot be acked while the condition holds
  uint32_t         token;     // state token; an ack is void once this changes
  uint32_t         first_seen;
  uint32_t         last_seen;
  bool             acked;
};
```

`id` is the only thing the UIs key on: titles and detail text are looked up
from the id, so translations live in the GUI and never travel over the wire.
The LCD carries its own short English strings, exactly as `fault_text.cpp`
already does for fault states.

`token` is what makes acking honest. For a counter-derived advisory the token
is the counter value; ack it at `gfci_count == 3` and a fourth trip raises it
again, because the stored ack no longer matches. For a sticky advisory the
token is unused.

## 5. Rules (v1)

| id | category | severity | trigger | sticky |
|---|---|---|---|---|
| `safety.ground_check` | safety | critical | `!isGroundCheckEnabled()` | yes |
| `safety.gfci_check` | safety | critical | `!isGfiTestEnabled()` | yes |
| `safety.relay_check` | safety | warning | `!isStuckRelayCheckEnabled()` | yes |
| `safety.diode_check` | safety | warning | `!isDiodeCheckEnabled()` | yes |
| `safety.vent_check` | safety | warning | `!isVentRequiredEnabled()` | yes |
| `safety.temp_check` | safety | warning | `!isTemperatureCheckEnabled()` | yes |
| `fault.gfci_tripped` | fault | critical | `getFaultCountGFCI() > 0`, token = count | no |
| `fault.no_ground` | fault | critical | `getFaultCountNoGround() > 0`, token = count | no |
| `fault.stuck_relay` | fault | critical | `getFaultCountStuckRelay() > 0`, token = count | no |
| `thermal.throttling` | thermal | warning | `TempThrottle::isThrottling()` | yes (auto-clears) |
| `thermal.high_temp` | thermal | warning | max `getTemperature(s)` over valid sensors ≥ `getPanicTemperature()` − margin | yes (auto-clears) |
| `wear.relay_life` | wear | info → warning | cycles ≥ 80% → ≥ 100% of rating | no |

**"Not all Required Safety Features are enabled" is not its own rule.** It is
what the UI says when it has more than one `safety.*` advisory to summarise.
Inventing a thirteenth advisory that duplicates the other six would mean two
things to ack and two things to keep in sync.

The six safety rules are deliberately not collapsed into one: the user has to
know *which* check is off to go turn it back on, and ground/GFI are a different
severity from vent/diode.

## 6. Evaluation

A `MicroTask` on a 5 s loop (`EVSE_NOTIFICATIONS_LOOP_TIME`, same shape as
`EVSE_BOOST_LOOP_TIME`). Every input above is already cached in `EvseMonitor`,
so evaluation is pure reads — **no new RAPI traffic**, which matters given the
queue pressure the RAPI layer is already under.

The rule set is written as a pure function over a snapshot struct:

```cpp
size_t notifications_evaluate(const NotificationInputs &in, Advisory *out, size_t max);
```

so the whole rule table is testable in `env:native_test` with no hardware and
no Arduino — the same trick `charge_threshold.cpp` and `fault_text.cpp` already
use in that env's `build_src_filter`.

## 7. API

```
GET  /notifications
  { "count": 2, "max_severity": "critical", "notifications": [
      { "id": "safety.ground_check", "category": "safety", "severity": "critical",
        "sticky": true, "acked": false, "first_seen": 1757548800, "last_seen": 1757552400 },
      ... ] }

POST /notifications/<id>/ack     → 200, or 409 if sticky and still active
```

`/status` gains exactly two fields:

```json
"notifications": { "count": 2, "severity": "critical" }
```

Deliberately minimal. Both UIs need a badge without a second round trip, and
nothing more belongs in a payload that is already polled hard by the HA
integration. The list stays on its own endpoint. (This also keeps out of the
way of the deferred `/status` heap-fields work.)

A `notifications` websocket event fires on any change to the set, via the
existing `event_send(doc)` path that `boost.cpp` and `current_shaper.cpp` use.

Auth: same policy as the other `/status`-adjacent endpoints. `POST .../ack`
is a state change and takes the same CSRF guard as `/divertmode` et al.

## 8. Relay life — the one real gap

There is **no relay-cycle counter anywhere in the stack**. The controller
exposes fault counters via `$GF` (`getFaultCounters()` → gfci / no-ground /
stuck) and nothing else; the 0.0.23 OpenEVSE library has no command for relay
closes.

Two ways forward:

**(a) Controller-side counter — the right answer.** Chris adds a relay-close
count to the controller and a RAPI read for it. The count then lives with the
hardware it describes: it survives the ESP32 being reflashed, swapped, or
factory-reset, which is the whole point of a wear counter. Pairs naturally with
the existing `ClearCounters` command on 9.0.0.

**(b) ESP32-side counting — the fallback.** Count rising edges of
`OPENEVSE_VFLAG_CHARGING_ON` in the existing monitor poll and persist. Works
today with no controller change, but on any charger already in the field the
count starts at zero, so "relay life" is a fiction until the relay is replaced —
and a reflash or a config wipe silently resets it.

**Recommendation: ask Chris for (a), ship (b) behind the same advisory so the
feature is not blocked, and treat (b) as authoritative only from the first boot
that recorded it** (store a `since` timestamp and have the GUI say "since
<date>" rather than implying a lifetime total).

Persistence for (b): the count must **not** hit flash on every cycle. Write on
session end and on a dirty-and-idle timer, batched — a charger doing several
sessions a day should see a handful of writes a day, not one per relay close.

Open: the cycle rating to compare against. It is hardware-dependent, so it
belongs in config with a conservative default; needs a number from Chris.

## 9. LCD

`charge_screen.cpp` already builds a `chip_row` — a flex row of status chips
(temp / wifi / car) aligned top-right — and `make_chip()` / `chip_set()` are
already the house pattern for adding one. So: one more chip, glyph plus count,
background coloured by max severity (info neutral, warning amber, critical red),
hidden when `count == 0`.

Same chip on `standby_screen.cpp`. Suppressed while `fault_screen` is up (§3).

Not in v1: a browsable notification list on the LCD. The chip's job is to say
"there is something to look at in the app". A list screen means input handling,
scrolling and its own translations on a 320×480 panel, which is a second feature
wearing the first one's clothes. Revisit once the chip is in and Chris has
opinions.

The legacy TFT_eSPI renderer was removed upstream in d28a01c7, so LVGL is the
only screen stack to touch.

## 10. GUI

Separate PR against gui-nightshift: a bell badge in the header showing count and
severity colour, opening a panel that lists advisories newest-first with an ack
button on the non-sticky ones. Sticky safety entries link to the setting that
clears them, which is the single most useful thing the panel can do — "ground
check is off" is only actionable if it is one tap from the switch.

Titles and detail strings are looked up from `id` in the GUI's existing i18n
tables.

## 11. Testing

- **Unit (`env:native_test`, doctest):** the rule table as a pure function —
  every rule's on/off edge, severity ordering, token invalidation of an ack,
  sticky refusing an ack. This is the bulk of the risk and none of it needs
  hardware.
- **Integration:** the emulator harness for `GET /notifications`, the ack round
  trip, and the `/status` summary fields.
- **Hardware:** toggle each safety check via the existing config writes and
  confirm the chip and badge follow; force a thermal throttle; verify the chip
  is suppressed under a live fault.

## 12. Budget

Target under ~6 KB flash and ~500 B RAM for the firmware half. Measure on
`openevse_wifi_v1` (4 MB) before committing — it was at 97.8% before upstream
turned off C++ exceptions (99d581b0, ~155 KB back) and dropped TFT_eSPI
(d28a01c7), so there should be real headroom now, but "should" is not a number.

## 13. Open questions

1. Relay cycle rating default, and whether Chris will add the controller-side
   counter (§8).
2. Should advisories also write `EventType::Notification` rows into the event
   log? It is free and the enum value is already there, but the log is already
   noisy and this could re-open the repeat-spam problem that #1216 fixed.
3. Do acks survive a reboot? Proposed: yes, persisted with the token, because an
   ack that evaporates on every power cut is worse than no ack at all.
4. Does the HA integration want these as entities? Out of scope here, but the
   API shape should not make it awkward later.
