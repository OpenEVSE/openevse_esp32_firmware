# Load Sharing (Local Power Sharing)

## Overview

Load sharing lets a small group of OpenEVSE devices on the same LAN share a
single upstream circuit limit (for example a 50 A feeder) **without** a cloud
service, MQTT broker, Home Assistant, or external controller.

The design uses a **controller/member** model:

- One device is the **controller**: it discovers peers, owns group config, runs
  the allocation algorithm, and pushes limits to members.
- Other devices are **members**: they receive config and allocations from the
  controller.

Limits are enforced on each node through the existing **Current Shaper** claim
(`EvseClient_OpenEVSE_Shaper` at `EvseManager_Priority_Safety`), via
`setLoadSharingLimit()` / `clearLoadSharingLimit()`. Member islanding also has
an explicit failsafe path driven by `loadsharing_heartbeat_timeout`.

### Related documents

| Document | Role |
|----------|------|
| [load-sharing-theory-of-operation.md](load-sharing-theory-of-operation.md) | **Normative** behaviour, invariants, algorithm detail |
| [load-sharing-project-status.md](load-sharing-project-status.md) | Branch/PR status, known gaps, next work |
| `api.yml` | OpenAPI for `/loadsharing/*` |

Originating discussion: GitHub issues
[#592](https://github.com/OpenEVSE/openevse_esp32_firmware/issues/592) and
[#940](https://github.com/OpenEVSE/openevse_esp32_firmware/issues/940).

## Goals

- Local multi-charger sharing of a fixed circuit limit (typical group size 2–8).
- Simple setup from a single controller; members are configured by config push.
- Safe behaviour if the controller or members go offline.
- Reuse existing primitives:
  - mDNS advertising / discovery
  - WebSocket `/ws` for status and allocation delivery
  - Current Shaper for limit enforcement

## Non-goals

- Coordinating across multiple subnets / VLANs.
- OCPP local controller / proxy behaviour.
- Hierarchical “group of groups”.
- Automatic controller election / failover.
- Guaranteeing perfect fairness under all packet-loss scenarios.

## Terminology

- **Node**: one OpenEVSE WiFi gateway.
- **Group**: nodes that share a common upstream current limit.
- **Controller**: node that owns group config and runs allocation.
- **Member**: node that receives config and allocations from the controller.
- **Peer**: another node on the LAN (discovered or manually added).
- **Connected**: vehicle present, EVSE state B (not drawing current).
- **Demanding / charging**: vehicle charging, EVSE state C (counts against the
  shared budget).
- **Connected min**: minimum offered to a connected (state B) member so it can
  start on demand; this allocation is **outside** the shared budget because the
  EV draws 0 A while connected.

## High-level architecture

```mermaid
flowchart LR
  subgraph controller [Controller]
    discovery[mDNS discovery]
    poller[Peer poller]
    algo[Allocation algorithm]
    discovery --> poller
    poller --> algo
  end
  subgraph member [Member]
    ws[/ws receive]
    shaper[Shaper load-share limit]
    ws --> shaper
  end
  poller -->|"WS status"| poller
  algo -->|"WS allocation"| ws
  algo --> shaperLocal[Local shaper limit]
```

### 1) Discovery

Each device advertises `_openevse._tcp` (service `openevse` / `tcp` port 80)
with TXT records such as `type`, `version`, and `id`.

The controller periodically queries for peers and builds a candidate list
(hostname, IP, firmware version, device id). Peers can also be added by
hostname, static IP, or device id via `POST /loadsharing/peers`.

### 2) Group configuration (controller)

Set `loadsharing_role` to `"controller"` (typically via the web UI), enable load
sharing, and add peers. The controller pushes group settings to each member with
`POST http://{member}/config` after the WebSocket connection is up. That push
sets the peer’s role to `"member"` and stores `loadsharing_controller_host`.

Key config fields (see theory doc §4.1 for the full table):

| Key | Default | Notes |
|-----|---------|-------|
| `loadsharing_enabled` | `false` | Master enable |
| `loadsharing_role` | `""` | `""`, `"controller"`, or `"member"` |
| `loadsharing_group_max_current` | `0` | Group circuit limit (A) |
| `loadsharing_safety_factor` | `1.0` | De-rate `[0..1]` |
| `loadsharing_heartbeat_timeout` | `30` | Seconds; member allocation staleness |
| `loadsharing_failsafe_mode` | `"safe_current"` | Or `"disable"` |
| `loadsharing_failsafe_safe_current` | `6.0` | Must be ≤ group max |
| `loadsharing_failsafe_peer_assumed_current` | `6.0` | Offline peer reserve |
| `loadsharing_priority` | `0` | Lower = higher priority; **not** pushed to peers |
| `loadsharing_rotation_interval` | `1800` | Seconds; scarcity time-slice; `0` disables |

`POST /config` rejects writes where failsafe safe current exceeds the group
max. Role transitions (`becomeMember` / `resetRole`) run only after validation.

Members store the peer list in `/loadsharing_peers.json` on LittleFS so it
survives reboot.

### 3) Status monitoring (controller → members)

The controller connects to each member’s `ws://{host}/ws`, bootstraps with
`GET /status` when needed, and tracks online/offline from the live stream.
Members do not poll each other.

### 4) Allocation algorithm (controller)

Profile: **Equal Share with Minimums**.

1. Offline handling:
   - `disable` mode → all allocations 0 if any peer is offline.
   - `safe_current` mode → reserve
     `offline_count × failsafe_peer_assumed_current` from the budget.
2. Connected (state B) members get `connected_min` **outside** the shared budget.
3. Charging (state C) members share `I_avail = group_max × safety_factor − reserve`.
4. If budget covers all minimums: give each min, then equal-share the remainder
   (capped by each member’s max / underutilized-pilot cap).
5. If not: select a subset ordered by **priority** (lower value wins), then
   device id. With `loadsharing_rotation_interval > 0`, equal-priority runs
   rotate so one vehicle is not permanently starved overnight.

When more than one member is charging and an EV draws below its pilot, the
controller caps that member’s effective max so freed budget can move to others.

> **Live firmware note**: the algorithm supports per-peer priority, but the
> controller currently fills every peer’s priority with its own
> `loadsharing_priority`. Per-peer priority works in divert_sim; ingesting each
> peer’s priority on the live controller is still outstanding (see project
> status).

### 5) Allocation delivery

Controller → member over the existing WebSocket:

```json
{"loadsharing": {"target_current": 16.5, "reason": "equal_share"}}
```

Common reasons: `equal_share`, `connected_min`, `min_subset`, `insufficient`,
`offline`, `failsafe_disabled`, `idle`.

Each node applies the limit with `shaper.setLoadSharingLimit(...)` (or clears
it). The shaper claim is at **Safety** priority, so while active it outranks a
manual override. Solar divert and other lower-priority claims still interact
normally; the effective charge current is the winning claim stack with
`min(shaper_limit, loadshare_limit)` on the shaper claim.

`EvseClient_OpenEVSE_LoadSharing` (claim id `65550`) is defined but **not** used
for enforcement today.

## Fail-safe behaviour

### Member failsafe (controller offline)

`checkMemberFailsafe()` runs on the member poller loop. Failsafe engages when
the controller host is missing, the controller is offline, no allocation has
been received, or the last allocation is older than
`loadsharing_heartbeat_timeout` (default **30 s**).

Enforcement:

| Mode | Action |
|------|--------|
| `safe_current` | `setLoadSharingLimit(failsafe_safe_current)` |
| `disable` | `setLoadSharingLimit(0, force_disabled=true)` |

When a fresh allocation arrives, the WebSocket handler replaces the failsafe
limit.

### Controller failsafe (member offline)

- **`safe_current`** (default): reserve assumed current for each offline peer;
  online peers keep charging on the reduced budget.
- **`disable`**: if any configured peer is offline, allocate 0 to everyone.

### Controller failure

Members engage failsafe within the heartbeat timeout (not by waiting for the
shaper’s 120 s live-power data interval). A new controller must be designated
manually; automatic election is future work.

## Configuration lockout on members

When `loadsharing_role == "member"`:

- Local `POST /config` **strips** `loadsharing_*` keys unless the post is a
  controller push that sets `loadsharing_role` to `"member"`.
- Intended behaviour (theory INV-7): peer management writes on members should
  be rejected. Treat hard HTTP 403 on `/loadsharing/peers` as a target; verify
  against current firmware if relying on it.

## API surface

Load-sharing settings also appear on `/config`. Dedicated endpoints:

| Method | Path | Notes |
|--------|------|-------|
| `GET` | `/loadsharing/peers` | Discovered + group peers |
| `POST` | `/loadsharing/peers` | Add peer. Duplicate → `200 {"msg":"already in group"}` (idempotent; skips reciprocal sync) |
| `DELETE` | `/loadsharing/peers/{host}` | Remove peer; triggers reset config on the peer and reciprocal remove |
| `DELETE` | `/loadsharing/peers` | Returns **501** (not implemented); only the `{host}` form works |
| `GET` | `/loadsharing/status` | Runtime status + `allocations[]` |
| `POST` | `/loadsharing/discover` | Trigger mDNS query |

`GET /loadsharing/status` fields: `enabled`, `group_id`, `computed_at`,
`failsafe_active`, `online_count`, `offline_count`, `peers[]`, `allocations[]`.
There is no `assigned_limit` / `controller_id` field; read the applied limit
from the EVSE claim / shaper path.

See `api.yml` for schemas.

## Testing

- divert_sim scenarios + `divert_sim/test_loadsharing.py` (equal share, offline
  failsafe, undrawn-pilot redistribution, priority, scarcity rotation, …)
- Integration tests under `tests/integration/`
  (`test_loadsharing_peer_*`, `test_loadsharing_fixes.py`,
  `test_loadsharing_drills.py`)

## Known limitations

See [load-sharing-project-status.md](load-sharing-project-status.md) for the
live list. Important ones for readers of this overview:

- GUI contract still mismatches firmware status/claim fields.
- Live controller does not yet ingest each peer’s own priority.
- Some poller elapsed-time checks are not rollover-safe.
- Bulk `DELETE /loadsharing/peers` returns 501.
- Claim layering vs charge-manager timer windows (#1112) still needs an
  explicit decision.

## Future extensions

- Additional allocation profiles (FIFO/ramp, proportional reduce).
- Automatic controller election on controller loss.
- Fully distributed config sync (no designated controller).
- Hierarchical groups (group-of-groups).
- Optional shared secret / pairing for untrusted LANs.
