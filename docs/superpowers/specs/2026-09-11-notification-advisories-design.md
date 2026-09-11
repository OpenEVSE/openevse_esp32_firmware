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

**All four** of the sources Chris named are already in the firmware. None of
them has anywhere to appear except the Monitoring → Health page, which you only
see if you go looking:

| Source | Today |
|---|---|
| Safety features | `EvseMonitor::isGfiTestEnabled()`, `isGroundCheckEnabled()`, `isStuckRelayCheckEnabled()`, `isDiodeCheckEnabled()`, `isVentRequiredEnabled()`, `isTemperatureCheckEnabled()` — all live, all already serialised into `/config` as `gfci_check`, `ground_check`, `relay_check`, `diode_check`, `vent_check`, `temp_check`. Nothing ever reads them back as an advisory. |
| Earlier faults | `EvseMonitor` already polls `$GF` into `_gfci_count` / `_nognd_count` / `_stuck_count` and exposes `getFaultCountGFCI()` / `getFaultCountNoGround()` / `getFaultCountStuckRelay()`; the latched `OPENEVSE_VFLAG_GFI_TRIPPED`, `OPENEVSE_VFLAG_NOGND_TRIPPED` and `OPENEVSE_VFLAG_HARD_FAULT` bits are in `getFlags()`. |
| Temperature | `EvseMonitor` temperature sensors plus `TempThrottle::isThrottling()` and `getPanicTemperature()`. |
| Relay life | **Already there too.** The controller's RELAY_HEALTH feature answers `$GL`, and `EvseMonitor` holds the lot: `getRelayLifeRemainingPct()`, `getRelayColdOpenCount()`, `getRelayElecDamageX1e6()`, `getRelayTransitBaselineMs()`, `isRelayTransitDriftWarning()`, `getRelayThermalIndexX100()`, `getRelayThermalWarningLevel()`, `getRelayStuckRecoveryCount()` — all already serialised into `/config` behind `isRelayHealthKnown()`. Relay switch count is separate and ESP-side: `EnergyMeter` counts closes into `total_switches`. |

`event_log.h` already declares an unused `EventType::Notification`, so even the
log slot is cut.

## 2. Scope

**In:** an advisory engine in the firmware; an HTTP + websocket API; a border
and a status line on the LVGL screens; inline markers, a status strip and a
badge in the web UI (separate repo, separate PR).

**Out:** outbound push (email, HA notifications, MQTT topics beyond the existing
state publish); any change to fault handling or the charge state machine; OCPP
status mapping; per-advisory user configuration beyond what §8 needs.

## 3. The tier boundary

This is the part most likely to go wrong, so it is a rule, not a guideline.

```
live fault      fault_screen.cpp, full screen, red, blocks charging
advisory        THIS — amber border + one line, never blocks, never takes the screen
event log       history, only interesting when you go looking
```

**An advisory never describes a condition that is stopping the EVSE from
charging right now.** If it is stopping charging it is a fault and the fault
screen owns it. `wear.relay_life` at 100% is an advisory; `OPENEVSE_STATE_STUCK_RELAY`
is a fault. `fault.gfci_tripped` is an advisory *because it has already
cleared* — while the GFI fault is live, the fault screen has it.

Consequence: the amber border and its line are suppressed entirely while the
fault screen is up. There is no case where both should be competing for the
user's attention — and red stays reserved for "this charger has stopped" (§9.1).

## 4. Data model

```cpp
enum AdvisorySeverity : uint8_t { Advisory_Info = 0, Advisory_Warning, Advisory_Critical };
enum AdvisoryCategory : uint8_t { Advisory_Safety = 0, Advisory_Fault, Advisory_Wear, Advisory_Thermal };

struct Advisory {
  const char      *id;        // stable, dotted, e.g. "safety.ground_check"
  AdvisoryCategory category;
  AdvisorySeverity severity;
  bool             sticky;    // true: acking mutes, it does not clear (§4.1)
  uint32_t         token;     // state token; an ack is void once this changes
  uint32_t         first_seen;
  uint32_t         last_seen;
  bool             acked;     // non-sticky: dismissed. sticky: muted.
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

### 4.1 Acking a sticky advisory mutes it, it does not dismiss it

The first draft of this design made safety advisories un-ackable: the condition
is real until the setting is changed, so why let anyone wave it away?

Because some installs disable a check deliberately, and a charger that shows a
permanent amber border for a configuration its installer chose is a charger
whose owner learns to ignore the border. Crying wolf costs more here than
under-reporting does — the LCD's whole job is to be believed at a glance, and it
only gets to spend that credibility once.

So acking a sticky advisory **demotes** it rather than clearing it:

| | not acked | acked (muted) |
|---|---|---|
| LCD border + line (§9) | drives them | does not |
| GUI notification list | listed, unmuted | listed, marked muted |
| GUI settings inline marker (§10) | shown | **still shown** |

Nothing is ever hidden — only the alarm is silenced, and only by a deliberate
act. If the condition clears and later returns, `first_seen` resets and the
advisory comes back unmuted, because that is a new event and not the one that
was acknowledged.

### 4.2 Acks persist across a restart

Acks are stored and survive a reboot. A power cut changed nothing about the
charger, so re-raising on the far side of one is the same crying-wolf failure
4.1 exists to prevent — and it would bite hard here, because these units restart
on every OTA. An advisory that comes back after every firmware push is one the
owner stops reading.

A restart is in any case a poor proxy for what we actually want to invalidate an
ack: it fires when nothing happened (power cut) and does not fire when something
did (a settings change). So the invalidations are tied to the real events:

1. **The token**, as in §4. Any movement in the underlying counter or state
   voids the ack, so persistence can never hide something new.
2. **For `safety.*`, the settings-flags word is part of the token.** Changing
   *any* EVSE setting voids a muted safety ack and re-raises it. That is the
   right moment to re-show the full picture: someone is in the settings with
   their hands on the config.
3. **The stored firmware version.** Acks are dropped wholesale when the running
   version differs from the one that wrote them. A firmware update is a genuine
   service event; a power cut is not. One string comparison at boot, no
   `esp_reset_reason()` plumbing.

Net: a power cut or a panic keeps the mute; a firmware update or any settings
change clears it.

Considered and left out: expiring acks after ~90 days, to catch the charger that
holds a years-old mute through no updates and no settings changes. It is the one
case none of the three invalidations covers, but it costs a persisted timestamp
per ack and an uptime-independent clock to compare against, to solve a scenario
that only exists on a charger nobody has touched in a very long time. Easy to
add later if that turns out to be a real charger rather than a hypothetical one.

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
| `wear.relay_life` | wear | info → warning | `getRelayLifeRemainingPct()` ≤ 20 → ≤ 5 | no |
| `wear.relay_transit_drift` | wear | warning | `isRelayTransitDriftWarning()` | yes (auto-clears) |
| `wear.relay_cold_open` | wear | warning | `getRelayColdOpenCount() > 0`, token = count | no |
| `wear.stuck_relay_recovery` | wear | info | `getRelayStuckRecoveryCount() > 0`, token = count | no |
| `thermal.relay_thermal` | thermal | info → warning | `getRelayThermalWarningLevel()` 1 → 2 | yes (auto-clears) |

Every `wear.*` / `thermal.relay_*` rule is skipped entirely unless
`isRelayHealthKnown()` — on a controller without the RELAY_HEALTH feature they
simply do not exist, the same way `/config` omits the block rather than
reporting a confident zero. Electrical damage is deliberately not its own rule:
it is an input to life-remaining, and two advisories for one wear story is one
too many.

"sticky: yes" means acking mutes rather than clears (§4.1); the advisory only
goes away when the condition does.

**"Not all Required Safety Features are enabled" is not its own rule.** It is
what the UI says when it has more than one `safety.*` advisory to summarise.
Inventing another advisory that duplicates the other six would mean two
things to ack and two things to keep in sync.

The six safety rules are deliberately not collapsed into one: the user has to
know *which* check is off to go turn it back on, and ground/GFI are a different
severity from vent/diode.

## 6. Evaluation

A `MicroTask` on a 5 s loop (`EVSE_NOTIFICATIONS_LOOP_TIME`, same shape as
`EVSE_BOOST_LOOP_TIME`). Every input above is already cached in `EvseMonitor`,
so evaluation is pure reads — **no new RAPI traffic**, which matters given the
queue pressure the RAPI layer is already under.

### 6.1 Event log: raise edge only, once per id per boot

An advisory writes one `EventType::Notification` row when it transitions from
absent to present, carrying its id and severity. Nothing on clear, nothing on
ack, nothing while it persists.

Guarded by a per-boot bitmask — one bit per advisory id, RAM only, not
persisted — so an id can log at most once between restarts.

The guard is not belt-and-braces, it is the whole point. The counter-derived
rules are already rate-limited by the physical event behind them, and the
`safety.*` rules only move when a human changes a setting. But
`thermal.throttling` and `thermal.high_temp` raise and clear as temperature
hunts around a setpoint, and logging every raise edge is precisely how the
History flood that #1216 fixed would come back. A per-boot cap makes that
impossible by construction rather than by tuning: thirteen rules, thirteen bits,
a worst case of thirteen rows per boot however badly something flaps.

Asymmetry (raised but never cleared) is deliberate. The event log is a record of
things that happened; what is true *now* lives on `/notifications`, which is
always current. A clear row would add volume without adding an answer to any
question the list does not already answer better.

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

POST /notifications/<id>/ack     → 200
                                   non-sticky: dismissed until the token changes
                                   sticky:     muted, still listed (§4.1)
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

## 8. Relay life — already solved, so do not restate it

An earlier draft of this spec called relay life "the one real gap" and proposed
either asking for a controller-side counter or counting relay closes on the
ESP32. Both already exist. Recorded here because the wrong version was written
down first:

- **Relay health** comes from the controller's RELAY_HEALTH feature over `$GL`
  (9.3.0+). `EvseMonitor` caches life-remaining %, cold-open count, electrical
  damage, contact-transit baseline and drift warning, thermal index / baseline /
  warning level, and the stuck-relay recovery count. `/config` emits the whole
  block, gated on `isRelayHealthKnown()` so it is omitted rather than defaulted
  on controllers without the feature.
- **Relay switch count** is ESP-side: `EvseMonitor` calls
  `EnergyMeter::increment_switch_counter()` and it is published as
  `total_switches`.
- **Resetting after a physical relay swap** is handled: `resetRelayHealth()`
  clears the accumulator, the self-learned baselines and the recovery counter.

The GUI already renders all of it under **Monitoring → Health**.

So the advisory's job here is **not** to restate any of these numbers. It is to
tell someone who is not on that page that they should be. The rules in §5 read
the cached values; the GUI entry links to Monitoring → Health rather than
duplicating a single figure, and the LCD line names the condition, never a
number.

One consequence worth stating: because `resetRelayHealth()` exists, the relay
advisories need no ack story of their own. Replacing the relay resets the
health, which clears the condition, which retires the advisory. That is better
than an ack, because it is tied to the physical act rather than to someone
clicking.

Thresholds are the only genuinely open part — see §13.

## 9. LCD

The panel is 480×320, read from across a garage. A pill in the top-right corner
is close to invisible at three metres, so the corner cannot be where "something
is wrong" lives. Severity picks the *form*, not just the colour.

### 9.1 The border

**When any advisory is active, the screen carries an amber perimeter border.**
Four pixels, flush to the edge, no radius, `NS_WARNING` — which resolves the
active palette, so it is correct in both the dark and light themes with no
second definition.

This is the whole point of the design: a 480×320 perimeter is legible from
across the garage in a way nothing in the corner is. It costs no layout space,
it does not move, and because it is a style on the screen object rather than a
widget it is drawn with the screen background — no extra invalidation, nothing
for the ~9 fps bus ceiling to care about.

**Amber only. Never red.** Red belongs to the fault screen and means "this
charger has stopped". If a critical advisory painted a red border, someone would
read a working charger as a broken one — and the tier rule in §3 exists
precisely so the two never blur. Amber means "still working, but look at me";
severity is carried by the text, which you read once you have walked over. That
also means the border is binary — present or absent — which is what makes it
readable at a distance.

Suppressed entirely on the fault, boot and setup screens: the fault screen is
already shouting (§3), and during boot or setup there is nothing the user can
act on.

### 9.2 The line

```
 14:32  Tue 11 Sep                      [28°C] [wifi 72] [⚡]
 ⚠ GROUND CHECK OFF  +2                          ← worst item, named
 ┌──────────────────────────────────────────────────────────┐  amber, 4px
```

At most one advisory is ever named, the highest-severity one, in the existing
`msg_lbl` strip (charge screen top strip line 2 — already 18px, already
positioned, unused most of the time). A `+N` suffix counts the rest. Glyph is a
**warning triangle, not a bell**: a bell means "you have messages", and none of
this is messages — it is the state of the user's charger.

`standby_screen.cpp` has no message strip and needs one adding; it already has
the `chip_row`, clock and the same `make_chip()` pattern, so this is a label and
an alignment.

**No notification chip, and nothing blinks.** The border already answers "is
there anything?", so a counting chip in `chip_row` would be a third way of
saying the same thing while crowding a row that exists for live telemetry
(temp / wifi / car). And animation on a screen bolted to a garage wall is
hostile — it also costs redraws we do not need to spend.

### 9.3 Not in v1

A browsable list on the LCD. The border plus the named line says "there is
something, and here is the worst of it"; a list means input handling, scrolling
and its own translations on a 480×320 panel — a second feature wearing the
first one's clothes. Revisit once this is in and Chris has opinions.

The legacy TFT_eSPI renderer was removed upstream in d28a01c7, so LVGL is the
only screen stack to touch.

## 10. GUI

Separate PR against gui-nightshift. Three surfaces, in order of how much they
actually matter:

1. **Inline markers on the settings page** — a warning marker beside the very
   switch that is off. This is the one that makes the feature useful: "ground
   check is off" is only actionable if it is one tap from the control that
   fixes it. These are drawn straight from the advisory list and are **not**
   suppressed by muting (§4).
2. **A strip on the status page** for critical advisories, dismissible.
3. **A bell badge in the header** opening a panel that lists everything
   newest-first, with ack buttons and links through to the relevant setting.

The bell is the index, not the feature. A badge that only opens a list of
complaints the user must then go hunting for is a worse version of the event
log — which they already have.

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

1. ~~Relay life thresholds.~~ **Settled: notice at ≤ 20% life remaining,
   warning at ≤ 5%.** Worth a nod from Chris since he owns what "getting close"
   means for this hardware, but not a blocker. **Cold opens and stuck-relay
   recoveries are also raised to the owner** — they are not service-only
   diagnostics. Both stay as specified in §5: cold opens a warning, recoveries
   informational, each tokened on its count so a later one re-raises.
2. ~~Should advisories write event-log rows?~~ **Settled: yes, but only on the
   raise edge and only once per id per boot — see §6.1.**
3. ~~Do acks survive a reboot?~~ **Settled: yes — see §4.2.** Voided by the
   token, by any settings change for `safety.*`, and by a firmware version
   change. Ack expiry considered and left out.
4. Does the HA integration want these as entities? Out of scope here, but the
   API shape should not make it awkward later.
