# FakeEVSE — In-Firmware Fake OpenEVSE Controller (Design)

**Date:** 2026-06-04
**Status:** Approved (design); pending implementation plan
**Branch target:** `feature/esp32-modernization` (new work branch `feature/fake-evse`)

## Goal

Provide a **compile-time-gated, in-firmware fake OpenEVSE controller** so the ESP32 WiFi
firmware reports a live, *connected* charger with **no controller hardware attached**. This
lets us bench-drive the web GUI (gui-nightshift), `/status`, the Home Assistant feature
stack, and full charge-session flows on a bare ESP32 module (e.g. the 16MB WROOM at
10.75.0.28, or the P4) without wiring a real ATmega controller.

This is a **developer/bench tool**, not a shipped feature. It must be impossible to enable
in a production build by accident.

## Background / why this approach

The entire EVSE stack hangs off a single Arduino `Stream`:

- `main.cpp:80` — `EvseManager evse(RAPI_PORT, eventLog);`
- `RapiSender(Stream *stream)` with a `setStream(Stream *)` setter.
- `EvseMonitor` drives everything through the `OpenEVSE` protocol library, which only ever
  talks to that `Stream` via `RapiSender`.

So a fake controller can be a **`Stream` subclass swapped in for the real UART**. Everything
downstream — RAPI framing/checksums in `RapiSender`, the `OpenEVSE` lib's command/response
parsing, `EvseMonitor`, `evse_man`, `/status`, the GUI — runs unmodified. Only the physical
UART pins are bypassed.

This was chosen over two alternatives:
- **Stub at `EvseMonitor`** — skips all RAPI parsing; less realistic, and touches a large,
  central file. Rejected.
- **Replace `evse_man`** — too invasive, would fork a core class. Rejected.

The codebase already anticipates simulation: `divert_sim/` exists and `divert.cpp` declares
`__attribute__((weak)) divertmode_get_time()` "so the simulator can override." FakeEVSE
extends that spirit rather than fighting the design.

## Architecture

```
                 (FAKE_EVSE only)
  EvseManager ── RapiSender ──▶ FakeEvseStream  (in-RAM, no UART)
       ▲                              │
       │  $GS/$GG/$GU/$GP/$GF/$GE...  │ parses cmd, mutates/reads FakeEvseState,
       │  $SC/$FE/$FS/$FD/$SY...      │ queues "$OK ...^CK" reply + async "$ST n^CK"
       └──────────────────────────────┘
                                       ▲
  POST/GET /fakeevse ──▶ FakeEvseState │   (vehicle plug/unplug, fault injection, voltage)
```

### Components

1. **`FakeEvseStream` (`src/fake_evse.h` / `src/fake_evse.cpp`)** — `class FakeEvseStream :
   public Stream`. Implements `write()` (firmware → fake: accumulate a RAPI command line),
   and `available()/read()/peek()` (fake → firmware: drain queued reply bytes). On each
   complete command line it calls into the state object and enqueues the reply.

2. **`FakeEvseState`** — the simulated controller state + tick logic (plain struct/class,
   no Arduino deps so it is native-testable). Holds evse_state, pilot_state, vflags, amps
   (mA), voltage (mV), session elapsed (s), session Wh, total kWh (Wh-accumulated),
   temperatures, fault counters, enabled/sleeping flags, vehicle-present flag, active fault.

3. **RAPI command handler** — maps each incoming `$XX` to a response built from
   `FakeEvseState`. Unknown/unmodeled commands return a bare `$OK`. Emits async `$ST <state>`
   notifications on transition (matching real firmware's unsolicited state events).

4. **Control endpoint** — `POST/GET /fakeevse` in `web_server.cpp`, registered **only** under
   `#ifdef FAKE_EVSE`, behind the existing web-auth wrapper.

5. **Wiring** — under `#ifdef FAKE_EVSE`, `main.cpp` constructs the fake stream and passes it
   to `EvseManager`/`RapiSender` instead of `RAPI_PORT`. A periodic ~1 Hz tick (driven from
   the existing main loop / a MicroTask) advances accrual.

## RAPI command/response contract

Responses must be **valid RAPI frames including the trailing `^XX` XOR checksum** that
`RapiSender` validates (the plan will confirm RapiSender's exact checksum/`getToken`
expectations against `RapiSender.h`). Token formats below are taken from the bundled
`OpenEVSE` lib (`openevse.cpp`):

| Cmd  | Meaning | Fake reply (`$OK` + tokens) |
|------|---------|------------------------------|
| `$GV` | version | `firmware protocol` (e.g. `8.2.0 4.0.1`; protocol 4.0.1 selects the decimal/3-token `$GS` form) |
| `$GS` | status | `evse_state session_elapsed pilot_state vflags` |
| `$GG` | charge A/V | `milliAmps milliVolts` (0 unless charging) |
| `$GU` | energy | `wattSeconds wattHoursAccumulated` (session = ws/3600; total = wh/1000) |
| `$GP` | temps | `t1*10 t2*10 t3*10` (`-2560` = sensor absent) |
| `$GF` | fault counts | `gfci nognd stuck` |
| `$GE` | settings | `pilot flags` |
| `$GC` | capacity | `min_A max_hw_A pilot max_cfg_A` |
| `$GA` | ammeter | `scale offset` |
| `$GT`/`$GD`/`$GI` | time/delay/serial | plausible canned values |
| `$SC <a>[mode]` | set current | update amps; reply `$OK <pilot>` |
| `$SV <mV>` | set voltage | update voltage |
| `$SL <c>` | service level | update; `$OK` |
| `$FE` / `$FS` / `$FD` / `$FR` | enable / sleep / disable / reset | set enabled/sleeping; `$OK` |
| `$FF`,`$F0`,`$FB`,`$FP`,`$SY`,`$SA`,`$S1`,`$SB` | features/heartbeat/etc. | accept, `$OK` |
| (unmodeled) | — | bare `$OK` |
| async | state change | unsolicited `$ST <evse_state>` on transition |

## State model + accrual (the "live" part)

OpenEVSE state codes used: **1 = Not Connected**, **2 = Connected (EV present)**,
**3 = Charging**, **254 = Sleeping**, fault states for **GFCI / no-ground / stuck-relay /
over-temp**.

Transition rules:
- `vehicle=0` → state **1** (Not Connected). Charging current 0.
- `vehicle=1` and not enabled / sleeping → state **254** (Sleeping). (state 2 Connected is not separately emitted; a plugged-but-paused EVSE reports 254 Sleeping)
- `vehicle=1` and enabled (via `$FE`, i.e. the GUI "start") and no fault → state **3**
  (Charging). Charging current = last `$SC` value (clamped to capacity).
- A fault injected via the endpoint → the corresponding fault state; clears to 1/2 when set
  back to `none`.

Accrual (~1 Hz tick, only while Charging):
- `session_elapsed += 1s`
- instantaneous power `W = amps * volts`; `session_Wh += W/3600`; `total_Wh += W/3600`.
- Pilot/temperatures report plausible steady values (e.g. temp1 ≈ 25.0 °C).

This makes the GUI's session timer, power, and energy graphs move in real time, and lets a
session run start→charge→stop entirely through the normal GUI/RAPI path.

## Control surface — `/fakeevse`

Registered only under `FAKE_EVSE`, behind existing web auth.

```
POST /fakeevse
  { "vehicle": 0|1,                              // EV plugged in
    "fault":   "none|gfci|noground|stuck|overtemp",
    "voltage": 240 }                             // optional, volts
  → 200 { ...resulting fake state... }

GET /fakeevse
  → 200 { state, pilot, amps, volts, session_elapsed, session_wh, total_kwh,
          vehicle, charging_allowed, fault }
```

Charging current / start / stop are **not** in this endpoint — they ride the real RAPI path
(GUI → ESP32 → `$SC`/`$FE`/`$FS` → faker), so the existing UI controls are exercised.

## Gating & build

- Guard: **`-D FAKE_EVSE`**. When undefined: `fake_evse.cpp` is excluded from the build (or
  fully `#ifdef`-empty), the `/fakeevse` route is not registered, and `main.cpp` wires the
  real `RAPI_PORT` exactly as today. **Zero** footprint and no behavioral change in normal
  builds.
- Convenience dev env **`[env:openevse_wifi_v1_16mb_fake]`** extending `openevse_wifi_v1_16mb`
  with `-D FAKE_EVSE`, so the bench WROOM can be reflashed with one command. The flag works on
  any env (incl. `openevse_p4`) for whoever wants it elsewhere.

## Testing

- **Native unit tests** (`pio test -e native`): `FakeEvseState` + the command handler are
  pure C++ (no Arduino runtime), so they test on the host. Assert: each `$Gx` produces a
  reply with the right token count and a valid checksum; `$SC` updates amps; a
  plug(`vehicle=1`)→`$FE`→N×tick sequence advances `session_elapsed`/`session_Wh`/`total`
  and reports state 3; fault injection yields the fault state and clears correctly; `vehicle=0`
  returns to state 1 with zero current. (Mirrors the existing `test/test_ha_oauth` native
  pattern; `build_src_filter` includes only `fake_evse.cpp`.)
- **On-device**: flash `openevse_wifi_v1_16mb_fake` to the WROOM; confirm `/status` flips
  `evse_connected:1` and `rapi_connected:1`; `POST /fakeevse {vehicle:1}` then start a charge
  from the GUI; confirm session timer/power/energy advance and stop works; inject a GFCI fault
  and confirm it surfaces.

## Out of scope (defaulted)

- **Not persisted** — fake state resets on reboot (bench tool; no config storage).
- **Single fixed EV profile** — no configurable battery capacity / SoC ramp model.
- **No physical-link testing** — by design the UART pins are bypassed; validating the real
  serial/electrical layer still requires real controller hardware.
