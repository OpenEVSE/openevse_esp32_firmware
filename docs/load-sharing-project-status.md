# Load Sharing Project Status

Last reviewed: 2026-07-15

This note captures the current state of the load-sharing work so it can be picked
up later without needing to reconstruct the review from chat history.

## Branches and PRs

- Firmware repo: `OpenEVSE/openevse_esp32_firmware`
- Firmware branch: `jeremypoulter/issue940`
- Firmware PR: <https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1027>
- Merged onto the branch: [#1147](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1147)
  (member failsafe enforcement, config validation, priority + rotation)
- GUI repo: `OpenEVSE/openevse-gui-nightshift`
- GUI branch: `copilot/add-load-sharing-gui-support`
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
read the active `charge_current` claim from `claims_target_store` rather than
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

## Current Blockers

### 1. GUI PR Does Not Match Firmware Contract

The nightshift GUI branch has green checks but unresolved review threads. The
main issue is API/claim contract mismatch.

Observed mismatches:

- `src/lib/stores/loadsharing.js` uses `Promise.all()` for peer/status reads.
  The firmware web server is single-threaded, so these should be serialized.
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

- Serialize load-sharing store reads.
- Use firmware `allocations[]` for group allocation display.
- Use `claims_target_store` for this EVSE's applied charge-current claim.
- Add `EvseClients.loadSharing` with ID `65550`.
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

### 2. Poller Timeout Comparisons Need Rollover-Safe Pattern

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

### 3. Bulk DELETE /loadsharing/peers Returns 501

`handleLoadSharingPeersDelete()` returns HTTP 501 "Not implemented" when called
without a host path parameter. Only `DELETE /loadsharing/peers/{host}` is
functional.

Recommended fix:

- Either implement bulk delete for `DELETE /loadsharing/peers`, or document in
  `api.yml` and user-facing docs that only the `{host}` variant is supported.

### 4. Live Controller Does Not Yet Ingest Peer Priority

The allocator consults `AllocationInput::priority`, and divert_sim scenarios
prove priority + rotation. On the live controller,
`LoadSharingPeerPoller::buildAllocationInputs()` still assigns
`loadsharing_priority` (the controller's local value) to every peer:

```cpp
input.priority = loadsharing_priority;  // For now, same priority
```

Until each peer's own priority is read from status/config sync, multi-node
priority only works in the simulator.

Recommended fix:

- Publish each node's `loadsharing_priority` in status (or config push), and
  use that value when building allocation inputs on the controller.

### 5. Claim Priority Interaction With Timer Windows (#1112)

#1147 notes an open design question: load-sharing limits and charge-manager
timer-window claims both sit at Limit/Safety-adjacent priority today. How they
should layer when both are active still needs an explicit decision and tests.

## Suggested Next Work Order

1. Bring the nightshift GUI branch back into alignment with the firmware API and
   EVSE claim model (including rotation interval and priority fields).
2. Sweep load-sharing poller timeout checks for rollover safety.
3. Ingest per-peer `loadsharing_priority` on the live controller so INV-3 holds
   outside the simulator.
4. Resolve bulk `DELETE /loadsharing/peers` behaviour — implement it or
   document that only `DELETE /loadsharing/peers/{host}` is supported.
5. Decide and document claim layering between load sharing and timer-window
   claims (#1112).
6. Rerun validation:
   - `pio run -e native_simulator`
   - `cd divert_sim && pytest -v test_loadsharing.py`
   - firmware integration tests
   - GUI tests/build after the API-contract fixes
