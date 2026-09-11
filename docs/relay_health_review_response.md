# Response to the `relay_health` review

Point-by-point response to the review of `openevse_esp32_firmware`'s
`relay_health` branch (compiler-verification follow-up to the original PR
description). Fixes landed in `692afa75` (`relay_health`, merged with
upstream `master` at `1000bb44`); the typed-endpoint follow-up landed in the
same commit plus `ad977ae` on `gui-nightshift`'s `http_endpoints` branch.

Thanks for building it and catching these — five real issues in code that
had only been reviewed by hand, not by a compiler that could actually
exercise the paths.

## 1. `readFrequency()` silently drops its D9 gate

**Fixed.** Restored the `!isD9Supported()` check the library's
`getFrequency()` had and the raw-`$GZ` rewrite lost:

```cpp
if(!_sender || !_openevse.isD9Supported()) {
  return;
}
```

A pre-D9 controller no longer gets sent a `$GZ` it can only NAK, at boot and
every ~60s settings-poll cycle.

## 2. `runStuckRelayRecovery()` has no caller / `resetRelayHealth()` has no gateway exposure

**Both fixed, and taken further.** Added `EvseMonitor`/`EvseManager::resetRelayHealth()`
(wraps `$FH`, re-reads `$GL` on success) — the gap you flagged as "the one
an installer actually needs" is closed.

For `runStuckRelayRecovery()`, we went with your other option and then some:
rather than leave it uncalled, we built the consumer. New typed HTTP
endpoints on the gateway:

| Endpoint | Calls |
|---|---|
| `GET /relay/reset` | `EvseManager::resetRelayHealth()` ($FH) |
| `GET /relay/recovery` | `EvseManager::runStuckRelayRecovery()` ($FK) |

Both use the deferred-response pattern already established for `/scan`
(WiFi scan) — the handler holds the `MongooseHttpServerResponseStream` open
and returns immediately; the actual HTTP response is sent from the async
RAPI callback. That matters specifically for `/relay/recovery`: the raw `/r`
passthrough's `sendCmdSync()` busy-loops the calling task until the RAPI
reply arrives, which would have frozen the *entire* Mongoose server —
HTTP, WebSocket, MQTT, everything — for the recovery's ~30s worst case, not
just the one request. The deferred pattern keeps the rest of the server
responsive while the recovery runs. Both endpoints carry the same
`actuatorMethodAllowed()` CSRF guard as `/restart`/`/apoff`/etc.

`gui-nightshift`'s Health tab now points both buttons at these instead of
raw RAPI passthrough — "Relay Replaced - Reset Health Values" (existing,
repointed) and a new "Run Stuck Relay Recovery" button. See point 3: this is
what makes the queue guard there actually take effect.

## 3. The 35s `$FK` will stall the RAPI queue past the heartbeat interval

**Fixed.** Added `_relay_recovery_in_flight`, checked at the top of
`EvseMonitor::loop()`:

```cpp
if(_relay_recovery_in_flight) {
  _count++;
  return EVSE_MONITOR_POLL_TIME;
}
```

Set around the `$FK` call in `runStuckRelayRecovery()`, cleared in its
callback. While a recovery is outstanding, `loop()` skips its own periodic
RAPI traffic entirely for that tick — heartbeat pulse, state/amp/temp/
settings polls — rather than letting them pile into the queue and get
`RAPI_RESPONSE_QUEUE_FULL`'d once it fills, heartbeat pulses included, which
is what risked tripping the controller's heartbeat-supervision fallback
current.

Set unconditionally rather than only on the "will actually run" path: an
EV-connected refusal NAKs almost immediately (the controller checks before
doing anything), so the pause costs nothing on that path and only matters
when the recovery genuinely runs.

This guard is only exercised when `EvseMonitor::runStuckRelayRecovery()` is
actually called — which, per point 2, is now the case: `/relay/recovery` is
what the GUI calls, so the fix isn't inert.

## 4. Cached health is never invalidated

**Fixed.** `evseBoot()` (runs on every controller connect, not just ESP32
power-on) now resets both at the top, before any RAPI round-trips:

```cpp
_relay_health_known = false;
_zero_cross_threshold_ma = OPENEVSE_RELAY_HEALTH_NOT_AVAILABLE;
```

A controller swap to one without `RELAY_HEALTH`/D9 no longer keeps serving
the previous unit's cached life percentage (or zero-cross threshold)
indefinitely.

## 5. Most of this is telemetry in a config endpoint

**Not resolved — still your call, as offered.** We didn't move the drifting
fields (`relay_life_pct` and siblings) to `/status`. It's a real design
question, but also a real API change with `gui-nightshift` implications on
the other side (`HealthTab.svelte` currently reads all of it from
`config_store`) — moving it means updating both repos in lockstep, and we'd
rather you make that call than have us commit to a direction unprompted.

Did the concrete, low-risk part: added a comment at the `/config`
`DynamicJsonDocument` allocation site (`web_server_config.cpp`) noting the
headroom is thinner now — measured against a live TFT unit's ~135 members,
the ~11 new keys fit, but it's worth rechecking before the next addition
rather than assuming.

## Minor

- **"without adding RAPI traffic"** — this was in `docs/relay_health.md`
  (the closest thing we have to a PR description), not a code comment.
  Corrected: the settings-poll cadence does add `$GZ` + `$GL` to that
  60s cycle, small but not zero.
- **Stale commit/version references** — `docs/relay_health.md` described
  head `6856978` pinning `OpenEVSE_Lib` 0.0.21; the actual head at review
  time was `de6fe6a1` at 0.0.22. Doc rewritten to match, and again since for
  everything in this response.
- **`/config` JSON example missing `relay_stuck_recovery_count`** — fixed
  in the same doc rewrite.

## Verification

`pio run -e openevse_wifi_v1` builds clean at every stage of this response:
after the five fixes (flash 96.0%, 1887925/1966080 bytes), after adding the
typed endpoints (96.1%, 1889937/1966080), and after merging upstream
`master`'s intervening changes into `relay_health` (96.3%, 1892813/1966080).
RAM steady at 23.8% throughout. `gui-nightshift`'s `http_endpoints` branch:
full test suite (958/958), production build, and a Playwright screenshot
against the mock dev server confirming both buttons render and fire the new
endpoints.
