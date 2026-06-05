# FakeEVSE — Bench Tool Cheatsheet

In-firmware fake OpenEVSE controller. Lets the ESP32 report a live, connected charger with
**no controller hardware** on the UART — for bench-driving the web GUI, `/status`, the Home
Assistant feature stack, and full charge-session flows.

**Compile-time gated by `FAKE_EVSE`. It is absent from every production build — there is no
runtime switch (by design: this is a mains/EV controller).** Spec/plan:
`docs/superpowers/specs/2026-06-04-fake-evse-design.md`, `docs/superpowers/plans/2026-06-04-fake-evse.md`.

---

## 1. Build & flash

Two ways to turn it on:

**A. Dedicated env (WROOM 16 MB):**
```bash
# bundles the nightshift web GUI; flash over the CH340 USB-serial port
GUI_NAME=gui-nightshift pio run -e openevse_wifi_v1_16mb_fake -t upload --upload-port /dev/ttyUSB0
```

**B. Add the flag to any existing env (e.g. the P4) — no platformio.ini change:**
```bash
GUI_NAME=gui-nightshift PLATFORMIO_BUILD_FLAGS=-DFAKE_EVSE \
  pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5
```

**After flashing with `GUI_NAME=gui-nightshift`, restore the committed default GUI** (the build
overwrites `src/web_static/` with nightshift headers):
```bash
git checkout -- src/web_static && git clean -fdq src/web_static
```
(Omit `GUI_NAME` to bundle the default gui-v2 and skip the restore.)

Ports on this bench: WROOM 16 MB → `/dev/ttyUSB0` (10.75.0.28) · P4 → `/dev/ttyACM5` (10.75.1.143).
First core-3 build may print a transient `FRAMEWORK_DIR is None` — just re-run the same command once.

## 2. Confirm it's alive

```bash
curl -s http://<ip>/status  | grep -oE '"(evse|rapi)_connected":[0-9]+|"state":[0-9]+'
curl -s http://<ip>/fakeevse        # full fake state as JSON
```
`evse_connected:1` + `rapi_connected:1` = the fake answered the RAPI handshake.

## 3. Drive the physical-world inputs — `POST /fakeevse`

Sets things a charger only *senses* (not start/stop — see §4):
```bash
curl -s -X POST http://<ip>/fakeevse -d '{"vehicle":1}'                 # plug in / unplug (0)
curl -s -X POST http://<ip>/fakeevse -d '{"fault":"gfci"}'             # none|gfci|noground|stuck|overtemp
curl -s -X POST http://<ip>/fakeevse -d '{"voltage":240}'             # volts
curl -s -X POST http://<ip>/fakeevse -d '{"current":48}'             # bench amps knob (6..80)
curl -s -X POST http://<ip>/fakeevse -d '{"vehicle":1,"voltage":208}' # combine keys
```
`current` sets delivered amps directly and lifts `max_hw_a`/`max_cfg_a` so the firmware's next
`$SC` (clamped to capacity) won't pull it back down — bench knob only.

GET/POST return: `state, pilot, max_current, amps, volts, session_elapsed, session_wh, total_kwh,
vehicle, charging_allowed, fault`.

## 4. Start/stop charging — it rides the REAL path, not /fakeevse

The fake mirrors the firmware's `$FE`/`$FS` exactly like real hardware, so **charging is driven
by the firmware's normal charge-control surface**, not a fake knob:
```bash
curl -s -X POST http://<ip>/override -H 'Content-Type: application/json' -d '{"state":"active"}'    # force charge
curl -s -X POST http://<ip>/override -H 'Content-Type: application/json' -d '{"state":"disabled"}'  # force sleep
curl -s -X DELETE http://<ip>/override                                                              # back to auto
```
…or use the GUI charge/sleep button, schedules, or eco/divert mode.

**On boot it parks at `state:254` (Sleeping)** — no active charge claim yet. Plug in a vehicle
*and* enable (override/GUI) to reach `state:3` (Charging); energy then accrues ~1 Hz from
`pilot_a × voltage`.

## 5. Charging won't start? Check the claim system

The fake plugs into the real `EvseManager` claim system, so **any higher-priority "disabled"
claim outranks your override** (higher priority number wins). Inspect:
```bash
curl -s http://<ip>/claims     # look for the highest-priority entry with "state":"disabled"
```
Common blockers (all real features, not fake limitations):
- **RFID** (`client 0x1000A`, prio ~1030) — access control: holds "disabled" until an authorised
  card is scanned. Disable RFID, scan a card, or release the claim to charge.
- **Schedule** (`client 0x10004`) — a disabled time-window. Check `GET /schedule`.
- **Shaper / divert / OCPP / limit** — current or state claims from those features.

Client IDs decode via `src/evse_man.h` (`EvseClient_OpenEVSE_*`).

## 6. State codes (what `state` means)

`1` Not Connected · `2` Connected · `3` **Charging** · `6` GFI fault · `7` No ground ·
`8` Stuck relay · `10` Over-temp · `254` Sleeping.

## 7. RAPI commands the fake answers

`$GV $GS $GG $GU $GP $GF $GE $GC $GA $GT $GD $GI` (getters), `$SC` (set current, clamped to
capacity), `$SV` (set voltage), `$FE`/`$FS`/`$FD` (enable/sleep/disable), `$SY` (heartbeat);
anything else → bare `$OK`. Reports firmware `8.2.0` / protocol `5.1.0` (≥5.0.0), so `$GS` uses
the **hex 5-token** form `$OK state elapsed pilot vflags` — the only path where the lib reads
vflags. The fake drives `EV_CONNECTED` (and `CHARGING_ON`) in vflags so `isVehicleConnected()`
and the session-complete reset work: `{"vehicle":0}` clears `EV_CONNECTED` → firmware zeroes
`session_elapsed`. State-change events are emitted as **`$AT state pilot capacity vflags`** (not
bare `$ST`, whose async handler reports vflags=0 and would clobber `EV_CONNECTED`).

## 8. Pure-logic unit tests

```bash
pio test -e native -f test_fake_evse   # FakeEvseState / handler / tick (Arduino-free core)
```
