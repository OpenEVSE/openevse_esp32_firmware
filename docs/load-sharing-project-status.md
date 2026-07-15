# Load Sharing Project Status

Last reviewed: 2026-07-15

This note captures the current state of the load-sharing work so it can be picked
up later without needing to reconstruct the review from chat history.

## Branches and PRs

- Firmware repo: `OpenEVSE/openevse_esp32_firmware`
- Firmware branch: `TODO-721-complete-load-sharing`
- Firmware PR: <https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1027>
- Merged onto the branch: [#1147](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1147)
  (member failsafe enforcement, config validation, priority + rotation)
- GUI repo: `OpenEVSE/openevse-gui-nightshift`
- GUI branch: `TODO-721-complete-load-sharing`
- GUI PR: <https://github.com/OpenEVSE/openevse-gui-nightshift/pull/47>

## Implemented Firmware Scope

The firmware PR is broad and includes:

- Load-sharing config keys in `src/app_config.cpp` and `src/app_config.h`
  (including `loadsharing_rotation_interval`, default 1800 s).
- API schema additions in `api.yml`.
- REST endpoints in `src/web_server_loadsharing.cpp`:
  - `GET /loadsharing/peers`
  - `POST /loadsharing/peers` (idempotent: existing peer → `200 already in group`)
  - `DELETE /loadsharing/peers/{host}`
  - `POST /loadsharing/discover`
  - `GET /loadsharing/status`
- Peer discovery in `src/loadsharing_discovery_task.*`.
- Peer status polling and allocation push in `src/loadsharing_peer_poller.*`.
- Shared types and persisted peer list handling in `src/loadsharing_types.*`.
- Allocation algorithm in `src/loadsharing_algorithm.*` (priority-aware scarcity
  subset + equal-priority rotation).
- Member-side WebSocket allocation handling in `src/web_server.cpp`.
- Member failsafe enforcement via shaper load-sharing limit in
  `LoadSharingPeerPoller::loop()` (#1147).
- Config write validation: failsafe safe current must not exceed group max;
  role transitions only after validation (#1147).
- Claim client ID in `src/evse_man.h`:
  `EvseClient_OpenEVSE_LoadSharing` = `0x0001000E` / `65550`.
- Normative behaviour in `docs/load-sharing-theory-of-operation.md`.
- Simulation scenarios and tests under `divert_sim/`.
- Integration tests under `tests/integration/` (including
  `test_loadsharing_fixes.py` and `test_loadsharing_drills.py`).

## Current API Contract

`GET /loadsharing/status` currently returns the firmware contract described by
`LoadSharingStatus` in `api.yml`:

- `enabled`
- `group_id`
- `computed_at`
- `failsafe_active`
- `online_count`
- `offline_count`
- `peers[]`
- `allocations[]`

It does not expose fields such as `member.assigned_limit`, `assigned_limit`,
`controller`, `controller_id`, `comms_status`, or `last_command_age`.

Applied current limits are visible via the normal EVSE claim path. Load sharing
limits are applied through the shaper claim (Safety priority). GUI code should
read the shaper-owned `max_current` claim from `claims_target_store` rather than
inventing an `assigned_limit` field on the load-sharing status endpoint.

## Recently Resolved

### #1147 — Member failsafe, config validation, priority, rotation

Merged 2026-07-15 onto `jeremypoulter/issue940`. Highlights:

1. Role transitions (`becomeMember` / `resetRole`) run only after config
   validation succeeds.
2. Duplicate `POST /loadsharing/peers` is idempotent (`200 already in group`).
3. Config rejects `loadsharing_failsafe_safe_current > loadsharing_group_max_current`.
4. Member failsafe applies a shaper load-sharing limit (safe current or
   force-disabled), outranking a manual override while islanded.
5. Scarcity subset selection sorts by priority, then id.
6. `loadsharing_rotation_interval` time-slices equal-priority members under
   scarcity.
7. New divert_sim scenarios/tests and integration drills/fixes suites.

### Integration CI native binary path

Previously blocked integration checks with `FileNotFoundError: program`. Fixed in
commit `522074bd` (`get_native_binary_path()` / workflow path resolution).

### Stale implementation plan doc

`docs/IMPLEMENTATION_PLAN.md` has been removed; normative behaviour lives in
`docs/load-sharing-theory-of-operation.md`.

## Validation Completed

TODO-721 completion evidence:

- All load-sharing simulator scenarios pass executable physical-budget,
  allocation/reason, offline-reserve, redistribution, and stability checks.
- Native allocator tests cover empty/invalid budgets, min/max boundaries,
  reserve exhaustion, disable mode, deterministic priority, completeness,
  redistribution caps, and rotation across `millis()` rollover.
- Load-sharing integration suites passed 18 tests. The four-peer mDNS case
  timed out once during Docker teardown and passed when repeated.
- GUI production build and all 872 unit tests pass; deterministic screenshots
  include `/settings/loadsharing` and are mirrored into the user guide.
- Documentation coverage reports 75/75 config options, 25/25 UI routes, and
  17/17 HTTP paths documented.
- `pio test -e native_test`, `pio run -e native_simulator`, and the production
  `openevse_wifi_v1` firmware build pass.

Focused simulation validation was run on the #1147 bench:

- divert_sim load-sharing suite (including priority + rotation scenarios)
- `tests/integration/test_loadsharing_fixes.py`
- `tests/integration/test_loadsharing_drills.py`
- `tests/integration/test_loadsharing_peer_status.py` (reported green after fixes)

`test_loadsharing_peer_management.py` still shows pre-existing mDNS discovery
flakiness unrelated to #1147.

Local rebuild reminder:

```sh
pio run -e native_simulator
cd divert_sim
pytest -v test_loadsharing.py
```

## Historical Blockers Resolved by TODO-721

### 1. GUI/Firmware Contract — Resolved

The nightshift GUI branch has green checks but unresolved review threads. The
main issue is API/claim contract mismatch.

Observed mismatches:

- `src/routes/Dashboard.svelte` expects non-existent status fields such as
  `status.member.assigned_limit`, `status.assigned_limit`, and
  `status.member.reason`.
- `src/routes/settings/LoadSharing.svelte` expects non-existent `controller`,
  `controller_id`, `last_command_age`, and similar member status fields.
- `src/routes/settings/LoadSharing.svelte` maps a "minimum per EVSE" concept onto
  `loadsharing_failsafe_peer_assumed_current` or
  `loadsharing_failsafe_safe_current`, which are safety/failsafe settings with
  different meanings.
- `src/lib/vars.js` lacks load-sharing client ID `65550`, so claim ownership
  display will be wrong when load sharing throttles.
- New firmware config such as `loadsharing_rotation_interval` is not yet exposed
  in the GUI.

Recommended GUI fixes:

- Use firmware `allocations[]` for group allocation display.
- Use the shaper-owned `max_current` property from `claims_target_store` for
  this EVSE's applied load-sharing cap. The defined load-sharing client ID
  65550 is not the enforcing claim.
- Expose actual config fields with accurate labels:
  - `loadsharing_failsafe_mode`
  - `loadsharing_failsafe_safe_current`
  - `loadsharing_failsafe_peer_assumed_current`
  - `loadsharing_safety_factor`
  - `loadsharing_heartbeat_timeout`
  - `loadsharing_rotation_interval`
  - `loadsharing_priority`
- Avoid showing unavailable controller/member telemetry unless firmware adds it
  to the API.

Resolved during review:

- `src/lib/stores/loadsharing.js` already serializes peer/status requests
  through `serialQueue`; the earlier `Promise.all()` finding was stale.

### 2. Poller Rollover Safety — Resolved

The repo convention is signed subtraction for `millis()` rollover safety, for
example:

```cpp
(long)(millis() - timeout) >= 0
```

The load-sharing poller still has direct comparisons such as:

```cpp
now - conn.lastHttpTime > _http_timeout_ms
now - conn.lastReconnectTime >= retryDelay
now - conn.lastMessageTime > _heartbeat_timeout_ms
```

(Allocation cadence and algorithm rotation already use the signed pattern.)

Recommended fix:

- Sweep `src/loadsharing_peer_poller.cpp` and replace remaining direct
  elapsed-time comparisons with the repository's rollover-safe pattern.

### 3. Bulk DELETE Contract — Resolved

Bulk deletion is intentionally unsupported. The firmware returns 405 and the
OpenAPI contract exposes only `DELETE /loadsharing/peers/{host}`.

### 4. Per-Peer Priority and Limits — Resolved

Status now publishes each node's minimum, maximum, and priority. The controller
uses those peer-local values when building allocation inputs and does not
overwrite member priority during config sync.

### 5. Timer/Load-Sharing Claim Layering (#1112) — Resolved

Schedule owns `charge_current`, while load sharing owns the
shaper claim's `max_current`; `EvseManager` composes the lower effective pilot.
An active load-sharing cap keeps the shaper claim at Safety priority even when
the shaper was enabled by a timer window.

### 6. Zero-Allocation Safety Enforcement (INV-1) — Resolved

The controller, member handler, and simulator now retain 0 A allocations as
active shaper limits. `clearLoadSharingLimit()` is reserved for leaving or
disabling the group. The 601 s offline regression remains within the physical
group budget.

### 7. Executable Stability Checks (INV-STAB) — Resolved

Simulation checks reject period-two allocation flapping, winner changes outside
rotation boundaries, non-finite outputs, reason/state mismatches, and physical
budget violations on the firmware's 5 s allocation cadence.

## Remaining Non-Goals

- Bulk peer deletion remains unsupported.
- Peer transport remains LAN HTTP/WebSocket without a new authentication layer;
  this work preserves the existing local-network trust model.
