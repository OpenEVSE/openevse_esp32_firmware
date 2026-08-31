# Boost Module Design

Direction set by Jeremy on PR #1195: Boost is a first-class backend feature, not
UI-composed claims. The generic `duration` on claims (PR #1195) is abandoned —
the claims layer stays simple. Boost supports the same dimensions as Limit, and
both sit on a shared threshold layer. Simulator support is required so Boost's
interaction with divert/shaper/scheduler can be simulated.

## Semantics

Boost = "charge NOW until a target is reached, then hand control back."

| | Limit | Boost |
|---|---|---|
| While active | passive (watches session) | claims Active, `EvseClient_OpenEVSE_Boost`, priority `EvseManager_Priority_Boost` (200) |
| On threshold reached | claims Disabled (1100) until unplug | **releases its claim**; whatever was in control resumes |
| Time basis | session-elapsed charging minutes | **wall-clock seconds from activation** |
| Energy basis | session total Wh | **delta Wh since activation** |
| SoC / Range | absolute target | absolute target (already met → release immediately, arm is a no-op) |

- Priority 200: overrides Divert (50) and Schedule (100); loses to Manual
  (1000), Limit (1100), Safety (5000). Both client id and priority already
  exist unused in `evse_man.h` — use them, define nothing new.
- Re-POST while active: restart with a fresh activation snapshot (new wall-clock
  zero, new energy baseline). One boost at a time; a new POST replaces the old.
- Cancel (DELETE) releases the claim immediately.
- Coexistence with Limit: no coupling. Limit's Disabled claim outranks Boost's
  Active claim by priority alone.
- Time values are clamped to 7 days (`BOOST_MAX_TIME_S = 7*24*3600`): beyond
  ~24.8 days the signed millis() rollover compare reads the deadline as past.
  Deadline math and its host tests lift from feature/claim-duration's
  `claim_timer` (rollover-safe `(int32_t)(deadline - now) <= 0`).
- No persistence: reboot clears any active boost (matches Limit and claims).
- Module ticks at 1 Hz while a boost is active, else sleeps on
  `MicroTask.Infinate` waiting for an arm event (same pattern as claim_timer).
- SoC/range with no vehicle data: arm is rejected with 422 (vehicle data source
  required), mirroring how the GUI gates these on vehicle support.

## Shared threshold layer

New `src/charge_threshold.h/.cpp`. Pure functions over values the caller reads
— no EvseManager dependency, so it is trivially host-testable:

```cpp
class ChargeThreshold {
  public:
    // basis = value at activation (0 for absolute dimensions)
    static bool reached(LimitType type, uint32_t target, uint32_t basis, uint32_t current);
    // remaining amount in the dimension's unit (0 when reached)
    static uint32_t remaining(LimitType type, uint32_t target, uint32_t basis, uint32_t current);
};
```

- `LimitType` moves to (or is included from) a header both modules can use
  without pulling all of limit.h into boost — reuse `limit.h`'s enum as-is;
  Boost includes `limit.h` for the type only.
- `Limit` refactors its four `limitTime/Energy/Soc/Range` compares onto
  `ChargeThreshold::reached(type, value, 0, current)` — observable behavior
  unchanged; the existing unit + simulator + integration suites are the proof.
- Boost calls with `basis = activation snapshot` for energy, `basis = 0` for
  SoC/range; time is handled by the deadline math, not ChargeThreshold.

## HTTP API

Wired like `handleLimit` (`web_server.cpp`):

- `POST /boost` body `{"type": "time|energy|soc|range", "value": N}` — same
  type/value vocabulary as `/limit` (time in **seconds** — differs from
  Limit's minutes; document loudly; energy Wh; soc %; range km/mi per config).
  201 on arm, 400 malformed, 422 unsupported dimension (no vehicle data).
- `GET /boost` — active: `{"type": ..., "value": N, "remaining": M,
  "started": <ISO8601 UTC>}` where `remaining` is seconds / Wh / %-gap /
  distance-gap (ceil, so it shows 1 not 0 just before expiry). Idle: `{}`
  with 200 (capability probe: 200+{} = firmware supports boost; 404 = old
  firmware).
- `DELETE /boost` — 200 and release; 404 if no boost active.

## Events / MQTT / status parity (the HA surface)

- `/status`: `"boost": bool`, `"boost_version": N` (version bumps on every
  arm/re-arm/release/expiry, pattern copied from `limit`).
- `event_send` on arm (`{"boost": {props}}`) and on end
  (`{"boost": false, "boost_reason": "reached|cancelled|replaced"}`).
- MQTT: subscribe `<base>/boost/set` accepting the same JSON as POST /boost
  (empty/`"off"` payload = cancel); publish boost JSON on version change,
  exactly as mqtt.cpp handles `<base>/limit/set` + `_limitVersion`.

## Simulator

- `divert_sim/Makefile`: add `boost.o`, `limit.o`, `charge_threshold.o` to
  `OPENEVSE_WIFI_OBJ`.
- `divert_sim.cpp`: options `--boost-at <offset-s> --boost-type <t>
  --boost-value <n>` arm the real module mid-run; boost claim state is visible
  in the existing CSV output columns (pilot/state).
- `divert_sim/test_boost.py` (pattern: `test_shaper.py`), asserting at minimum:
  1. Eco divert active, boost armed → charging at full current during boost.
  2. Time boost releases at the deadline and divert regains control (charge
     current returns to the solar-following value).
  3. Energy boost releases after the delta kWh, not the session total.
  4. Manual Disabled claim outranks an active boost (no charging).
- Host unit tests: `test_charge_threshold` (all four dimensions, basis math,
  remaining/ceil) and boost deadline math (lifted claim_timer cases incl.
  rollover), in the existing native test env.

## Branch / PR / rollout

- Branch `feature/boost` off upstream master (worktree exists). New PR;
  close #1195 with a pointer comment once the new PR is up.
- Firmware + simulator only; gui-nightshift switches Boost to `/boost` in a
  follow-up after Jeremy blesses the API.
- Validation order: host tests → sim suite → CI three suites → bench HW
  (REST contract, real timed + energy boost, manual-beats-boost) → PR
  review-ready.

## Out of scope

- Persistence across reboot; multiple concurrent boosts; OCPP interaction
  beyond existing priority ordering; GUI changes; RAPI/LCD surface.
