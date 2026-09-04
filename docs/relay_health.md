# Relay contact-life health estimation

Estimates how much life is left in the main charging contactor and surfaces
it through the stack: controller firmware → RAPI → client library → this
gateway firmware → GUI. Diagnostic only — never gates or alters charging
logic at any layer.

Spans four repos:

| Repo | Branch/tag | What it adds |
|---|---|---|
| `open_evse` (ATmega/SAMD controller) | `dev` @ `5b3a755` (built on `442a98f`, `544f277`) | The estimation model: `RelayHealth` module, new RAPI commands, plus stuck-relay auto-recovery ($FK) |
| `OpenEVSE_Lib` (client library) | `OpenEVSE9` @ `2c7c3f3`, tag `v0.0.22` | `getRelayHealth()` / `resetRelayHealth()` / `runStuckRelayRecovery()` wrappers |
| `openevse_esp32_firmware` (this repo) | `relay_health` | Polls the new data, caches it, exposes it via `/config` JSON |
| `openevse-gui-nightshift` | `relay_health` @ `c3f49b8` | Displays it: Monitoring → Health tab's Relay Health section |

## Background / the problem

Relay life splits into two budgets that are easy to conflate: **mechanical**
life (cycles with no current flowing — springs/bearings, normally
10⁷–10⁸ cycles) and **electrical** life (cycles under load — contact erosion
from arcing, 10⁴–10⁵ cycles at rated current). For an EVSE contactor the
electrical budget is the one that matters, and the single biggest lever is
whether the contact opens hot (current still flowing) or cold (the car has
already ramped down). A handful of hot disconnects — fault interrupts,
e-stops, GFCI trips — can consume more life than years of normal cold
cycling.

The firmware already measured charging current and knew whether a stop was
clean or a fault, but nothing accumulated that into a life estimate, and
nothing gave early warning of the one failure mode a cycle count can't
predict: a welded contact.

## Model (`open_evse` firmware)

Cumulative-damage (Miner's rule): each relay open consumes a fraction of a
life budget, summed over the relay's life.

- **Cold open** (current already at/below a zero threshold): `d = 1/N_m`
- **Hot open** (current still flowing): `d = (1/N_e) · (I/I_r)² · α_load · α_temp`

Where `N_e`/`N_m` are the rated electrical/mechanical life (cycles, from the
relay's datasheet), `I_r` is rated current, and `α_load`/`α_temp` are
load-character and temperature penalty multipliers. In a well-behaved
installation the cold-cycle term rounds to ~0 (mechanical life is normally
untouchable at any realistic cycle count) and the reported percentage is
really tracking hot-switch events — exactly what you want surfaced.

Two more independent signals feed into the picture, both self-baselined per
station since enclosure thermals and hardware vary:

- **Coil drop-out (open) transit-time drift** — a lengthening time between
  the open command and the contact physically opening (measured via the
  load-side AC-sense pin, CGMI hardware only) is an early symptom of
  welding, ahead of the hard `EVSE_STATE_STUCK_RELAY` fault that ultimately
  catches it.
- **Thermal index `H = ΔT / I²`** (requires `TEMPERATURE_MONITORING`) —
  proportional to contact resistance. Heating at the contact is `I²·R`;
  normalizing by `I²` removes the load dependence, so `H` stays flat while
  the contact is healthy and climbs as it erodes/oxidizes.

Not implemented: direct contact-resistance measurement via closed-contact
voltage drop — this hardware has no differential voltage sensor across the
contacts to measure it.

### New source: `firmware/open_evse/RelayHealth.{h,cpp}`

Gated by `RELAY_HEALTH`, auto-enabled wherever `RELAY_ZC_SWITCH` + `AMMETER`
are (every current build target). New EEPROM state at offsets 44–55.

### New RAPI commands

| Command | Direction | Purpose |
|---|---|---|
| `$SZ ma` | set | Relay-open current-zero threshold (mA) — runtime-tunable per-unit ammeter noise floor |
| `$GZ` | get | Now returns **two** fields: `freqx100 zerothreshma` (was frequency-only) |
| `$GW` | get | Relay wear diagnostics: `hotswitchcnt lastopenma closetransitms opentransitms` |
| `$GL` | get | Relay life/health: `pctremain coldopencnt elecdamagex1e6 transitbaselinems transitdrift thermalx100 thermalbaselinex100 thermalwarn stuckrelayrecoverycnt` (9th field, `stuckrelayrecoverycnt`, added alongside stuck-relay auto-recovery) |
| `$FH` | set | Reset the health accumulator/baselines/recovery counter (use after a physical relay replacement) |
| `$FK` | set | Run the stuck-relay recovery cycle manually. NAK'd if an EV is connected; blocking on the controller side for up to ~30s |

Full field-by-field reference: `open_evse/firmware/open_evse/rapi_proc.h`.

## Client library (`OpenEVSE_Lib` 0.0.22)

Added `OpenEVSEClass::getRelayHealth(callback)`, `resetRelayHealth(callback)`,
and `runStuckRelayRecovery(callback)` in `src/openevse.{h,cpp}`, mirroring
the existing `getFrequency()` / `getRelayStatus()` / `resetFaultCounters()`
D9 extensions (same `isD9Supported()` protocol gate, same token-parsing
style). Added the `OPENEVSE_RELAY_HEALTH_NOT_AVAILABLE` (`0xffff`) sentinel
for fields that aren't available yet (baseline not established) or at all
(thermal fields need `TEMPERATURE_MONITORING` on the controller).
`getRelayHealth()`'s callback took a breaking 9th → 10th argument
(`stuck_relay_recovery_count`) between 0.0.21 and 0.0.22; older callbacks
still work against a newer controller (the field defaults to 0 if the
9-argument form is compiled), but a 10-argument callback against an older
0.0.21-vintage controller response is a compile-time signature mismatch, not
a silent bug.

`getFrequency()` itself was **not** changed to expose the new `$GZ`
zero-threshold field, to avoid a breaking signature change to an existing
public method — see below for how this gateway firmware gets that value
instead.

## Gateway firmware (this repo, `relay_health` branch)

`src/evse_monitor.{h,cpp}` — `EvseMonitor` gained:

- **`readRelayHealth()`** — calls `OpenEVSE.getRelayHealth()`, caches all
  nine fields, exposed via `isRelayHealthKnown()` +
  `getRelay*()`/`isRelay*()` getters.
- **`readFrequency()` rewritten** — reads `$GZ` raw via `EvseMonitor`'s own
  `RapiSender*` instead of the library's single-field `getFrequency()`, so it
  now also captures the zero-cross threshold from the same response instead
  of round-tripping `$GZ` twice. Gated on `isD9Supported()` (as
  `getFrequency()` itself is), so a pre-D9 controller isn't sent a `$GZ` it
  can only NAK.
- **`resetRelayHealth()`** — wraps `$FH`; re-reads `$GL` on success rather
  than guessing what a freshly-reset accumulator looks like locally.
- **`runStuckRelayRecovery()`** — wraps `$FK`; re-reads `$GL`/`$GR` on
  success. Sets `_relay_recovery_in_flight` for the duration, which
  `loop()` checks to skip its own periodic RAPI traffic (heartbeat pulse,
  state/amp/temp/settings polls) — `$FK` can hold the RAPI queue for up to
  ~30s, and without this guard everything `loop()` would otherwise enqueue
  during that window gets `RAPI_RESPONSE_QUEUE_FULL`, heartbeat pulses
  included, which risks tripping the controller's heartbeat-supervision
  fallback current if enough consecutive pulses are dropped.
- **Cache invalidation at `evseBoot()`** — `_relay_health_known` and
  `_zero_cross_threshold_ma` are reset at the top of `evseBoot()` (which
  runs on every controller connect, not just ESP32 power-on). Without this,
  swapping in a controller that doesn't support `RELAY_HEALTH`/D9 — or is
  simply older — would keep serving the *previous* controller's cached
  values indefinitely, since nothing else clears them.

**Known gap**: `runStuckRelayRecovery()` and `resetRelayHealth()` are both
correctly wired C++ API now, but nothing in this repo calls them — the GUI's
"Relay Replaced - Reset Health Values" button and any future recovery
trigger go through the generic RAPI passthrough (`/r?json=1&rapi=$FH` /
`$FK`) instead, bypassing this layer (and, for `$FK`, its queue guard)
entirely. That's fine functionally — the passthrough works — but it means
the guard above only takes effect if/when something is pointed at the typed
method instead.

### Polling cadence

| When | Why |
|---|---|
| At boot (`evseBoot()`) | Populate immediately on connect |
| After every charge session (`updateEvseState()`, the existing relay-open `!isCharging()` branch) | `$GL` is only meaningful once an open has occurred — that's when the controller's accumulator actually updates |
| Every ~60s (`getSettingsFromEvse()`, existing settings-poll cadence) | Catches changes from other RAPI clients and long-running sessions with no session boundary. Does add `$GZ` + `$GL` to that cycle — small, but not zero. |

### Exposure

`src/app_config.cpp`'s `config_serialize()` (the `/config` JSON), alongside
the existing `zero_cross`/`relay_dc1` fields:

```json
{
  "zero_cross": true,
  "zero_cross_threshold_ma": 300,
  "relay_dc1": true,
  "relay_life_pct": 100,
  "relay_cold_open_count": 42,
  "relay_elec_damage_x1e6": 0,
  "relay_transit_drift_warning": false,
  "relay_transit_baseline_ms": 18,
  "relay_thermal_warning_level": 0,
  "relay_thermal_index_x100": 12,
  "relay_thermal_baseline_x100": 11,
  "relay_stuck_recovery_count": 0
}
```

Each field/block is **omitted** (not defaulted to 0/false) until the
controller has actually answered — same pattern already used for
`relay_dc1`/`relay_dc2`/`relay_ac`, so the GUI can distinguish "not
supported by this controller" from a real zero.

**Design question, unresolved**: most of these fields are telemetry that
drifts over the life of the unit (life%, cold-open count, damage, thermal
index/baseline, recovery count) sitting in an otherwise-static `/config`
endpoint — `zero_cross_threshold_ma` is the one genuine setting in the
group. As written, a client has to re-poll `/config` to notice a change
(e.g. a new thermal warning), and none of it reaches the WebSocket. Moving
the drifting fields to `/status` (`create_rapi_json()`) would fit the
existing status/config split better, but is a real API change with GUI
implications on the other side (`gui-nightshift` currently reads all of
this from `config_store`) — deliberately not done here.

`/config`'s `DynamicJsonDocument` capacity (`JSON_OBJECT_SIZE(128) + 1024`,
`web_server_config.cpp`) has headroom for these ~11 new keys on hardware
(measured: a live TFT unit already serves ~135 members within this budget),
but it's thinner now — see the comment at the allocation site.

## Verification status

Firmware side (`open_evse`) built clean on `m328p_core`, `m328p_LCD_WIFI`,
`samd`, `samd_ice` via PlatformIO. Gateway side (`openevse_esp32_firmware`):
`pio run -e openevse_wifi_v1` builds clean (flash 96.0%, 1887925/1966080
bytes; RAM 23.8%), including the review-driven fixes above.
