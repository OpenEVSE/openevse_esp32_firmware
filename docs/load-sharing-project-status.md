# Load Sharing Project Status

Last reviewed: 2026-07-15

This note captures the current state of the load-sharing work so it can be picked
up later without needing to reconstruct the review from chat history.

## Branches and PRs

- Firmware repo: `OpenEVSE/openevse_esp32_firmware`
- Firmware branch: `jeremypoulter/issue940`
- Firmware PR: <https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1027>
- GUI repo: `OpenEVSE/openevse-gui-nightshift`
- GUI branch: `copilot/add-load-sharing-gui-support`
- GUI PR: <https://github.com/OpenEVSE/openevse-gui-nightshift/pull/47>

## Local Working Tree

At review time, the firmware working tree had two local uncommitted simulation UI
changes:

- `divert_sim/simulations.js`: async render-token guard to avoid stale renders
  when switching categories while CSV/source fetches are in flight.
- `divert_sim/view.html`: cache-bust query string bump from `20260605a` to
  `20260706a`.

The `gui-nightshift` working tree was clean.

## Implemented Firmware Scope

The firmware PR is broad and includes:

- Load-sharing config keys in `src/app_config.cpp` and `src/app_config.h`.
- API schema additions in `api.yml`.
- REST endpoints in `src/web_server_loadsharing.cpp`:
  - `GET /loadsharing/peers`
  - `POST /loadsharing/peers`
  - `DELETE /loadsharing/peers/{host}`
  - `POST /loadsharing/discover`
  - `GET /loadsharing/status`
- Peer discovery in `src/loadsharing_discovery_task.*`.
- Peer status polling and allocation push in `src/loadsharing_peer_poller.*`.
- Shared types and persisted peer list handling in `src/loadsharing_types.*`.
- Allocation algorithm in `src/loadsharing_algorithm.*`.
- Member-side WebSocket allocation handling in `src/web_server.cpp`.
- Claim client ID in `src/evse_man.h`:
  `EvseClient_OpenEVSE_LoadSharing` = `0x0001000E` / `65550`.
- Documentation in `docs/load-sharing.md` and `docs/IMPLEMENTATION_PLAN.md`.
- Simulation scenarios and tests under `divert_sim/`.
- Integration tests under `tests/integration/`.

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
uses client ID `65550`, so GUI code should read the active `charge_current` claim
from `claims_target_store` rather than inventing an `assigned_limit` field on the
load-sharing status endpoint.

## Validation Completed

Focused simulation validation was run locally.

Commands:

```sh
pio run -e native_simulator
cd divert_sim
pytest -v test_loadsharing.py
```

Result after rebuild:

```text
5 passed in 0.05s
```

Important detail: the first run failed because `run_simulations.py` used the
stale `divert_sim/divert_sim` binary from 2026-06-30. After rebuilding,
`run_simulations.py` used `.pio/build/native_simulator/program` and the same
tests passed.

## Current Blockers

### 1. Firmware Integration CI Cannot Start Native Firmware

Firmware PR checks showed 29 successful checks, but integration checks were red.
The failures were not load-sharing assertions. They failed during fixture setup:

```text
Failed to start native firmware: [Errno 2] No such file or directory: 'program'
```

Likely cause:

- `.github/workflows/integration_tests.yaml` sets `NATIVE_BINARY_PATH=./program`.
- `tests/integration/conftest.py` accepts that relative path.
- The fixture then starts the native process with `cwd` set to a per-instance
  temporary directory, so the relative executable path is resolved from the wrong
  directory.

Recommended fix:

- Either set `NATIVE_BINARY_PATH` to an absolute path in the workflow, or resolve
  `NATIVE_BINARY_PATH` to an absolute path in `get_native_binary_path()`.

### 2. Member Failsafe Appears To Report But Not Enforce

Controller-side offline-peer reserve is covered by the simulation tests. Member
side controller-loss handling appears incomplete.

Current behavior found during review:

- `LoadSharingGroupState::checkMemberFailsafe()` sets `_failsafe_active`.
- `LoadSharingPeerPoller::loop()` calls `checkMemberFailsafe()` for members.
- Incoming controller allocations are applied in `onWsFrame()` in
  `src/web_server.cpp`.
- No matching code was found that applies `loadsharing_failsafe_safe_current` or
  disables the EVSE when member allocation messages stop.

Recommended fix:

- When member failsafe becomes active, actively claim either:
  - safe current for `loadsharing_failsafe_mode == "safe_current"`, or
  - disabled state for `loadsharing_failsafe_mode == "disable"`.
- Add a targeted integration or native test that simulates a member losing
  controller allocations and verifies the EVSE claim/resulting state.

### 3. GUI PR Does Not Match Firmware Contract

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
- Avoid showing unavailable controller/member telemetry unless firmware adds it
  to the API.

### 4. Poller Timeout Comparisons Need Rollover-Safe Pattern

The repo convention is signed subtraction for `millis()` rollover safety, for
example:

```cpp
(long)(millis() - timeout) >= 0
```

The new load-sharing poller has direct comparisons such as:

```cpp
now - conn.lastHttpTime > _http_timeout_ms
now - conn.lastReconnectTime >= retryDelay
now - conn.lastMessageTime > _heartbeat_timeout_ms
```

Recommended fix:

- Sweep `src/loadsharing_peer_poller.cpp` and replace direct elapsed-time
  comparisons with the repository's rollover-safe pattern.

### 5. Implementation Plan Phase 3 Status Is Stale

Role designation (`loadsharing_role`), config lockout on members, and config
push from controller to members are implemented in the firmware (see
`pushConfigToPeer()`, `becomeMember()`, etc.), but `docs/IMPLEMENTATION_PLAN.md`
still marks Phase 3 tasks as "NOT STARTED".

Recommended fix:

- Update `docs/IMPLEMENTATION_PLAN.md` to reflect the implemented Phase 3
  scope and remaining gaps, if any.

### 6. Bulk DELETE /loadsharing/peers Returns 501

`handleLoadSharingPeersDelete()` returns HTTP 501 "Not implemented" when called
without a host path parameter. Only `DELETE /loadsharing/peers/{host}` is
functional.

Recommended fix:

- Either implement bulk delete for `DELETE /loadsharing/peers`, or document in
  `api.yml` and user-facing docs that only the `{host}` variant is supported.

## Suggested Next Work Order

1. Fix the integration CI native binary path first. This should turn the current
   integration failure from setup noise into real test signal.
2. Implement member failsafe enforcement and add a targeted test.
3. Bring the nightshift GUI branch back into alignment with the firmware API and
   EVSE claim model.
4. Sweep load-sharing poller timeout checks for rollover safety.
5. Update `docs/IMPLEMENTATION_PLAN.md` Phase 3 status to match implemented
   firmware scope.
6. Resolve bulk `DELETE /loadsharing/peers` behaviour — implement it or
   document that only `DELETE /loadsharing/peers/{host}` is supported.
7. Rerun validation:
   - `pio run -e native_simulator`
   - `cd divert_sim && pytest -v test_loadsharing.py`
   - firmware integration tests after the path fix
   - GUI tests/build after the API-contract fixes
