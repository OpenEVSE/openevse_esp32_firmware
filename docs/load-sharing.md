# Load Sharing (Local Power Sharing)

## Overview

This document describes a proposed **local load sharing** feature for OpenEVSE
ESP32 WiFi firmware, based on the requirements and discussion in GitHub issues
\#592 and \#940.

The design goal is to allow a set of OpenEVSE devices on the same LAN to share
a single upstream electrical service limit (e.g. a 50A circuit) **without
requiring a cloud service, MQTT broker, Home Assistant, or external
controller**.

The system uses a **controller/member** architecture for the initial MVP:
one device is designated as the **controller** and manages the group, while
other devices act as **members** that receive their configuration and current
allocations from the controller.

The existing **Current Shaper** claim system remains responsible for
**enforcing** the computed limit on each member device, including built-in
failsafe timeout behavior. This feature adds:

- Group formation (configuration + discovery) on the controller
- Real-time status monitoring of members via WebSocket
- A centralized allocation algorithm on the controller
- Allocation push from controller to members via WebSocket
- Fail-safe behavior leveraging the existing Current Shaper timeout

## Requirements (from issues)

### Must have

- Local (LAN) operation; no dependency on cloud services (#940).
- Multi-charger sharing of a fixed circuit limit (#592, #940).
- Simple setup flow: discover peers by mDNS and/or allow static IP entry
  (#940 comments).
- Safety-first behavior on communication loss (either refuse to charge, or fall
  back to a safe configured current) (#592, #940).

### Nice to have / future extension

- Multiple sharing profiles (equal share, priority-first, FIFO/ramp, etc.)
  (#940).
- Potential future data sharing (metering/log export) (#940 comments).
- Hierarchical groups like JuiceNet “group of groups” (discussed in #940
  comments). Not in scope for initial implementation.

## Goals

- Provide a robust local load-sharing mechanism for small groups (2–8 chargers
  typical).
- Simple setup: configure load sharing from a single controller device;
  members are configured automatically.
- Keep the system safe if the controller or members go offline.
- Reuse existing primitives:
  - mDNS advertising (already present) for peer discovery on the controller
  - WebSocket status updates `/ws` (already present) for status monitoring
    and allocation delivery
  - Current Shaper claim system (already present) for enforcing limits and
    failsafe timeout behavior on members

## Non-goals

- Coordinating across multiple subnets / VLANs.
- Scheduling / queuing logic beyond minimal “who gets to charge when”.
- OCPP local controller / proxy behavior.
- Guaranteeing perfect fairness under all packet-loss scenarios.

## Terminology

- **Node**: one OpenEVSE WiFi gateway.
- **Group**: a set of nodes that share a common upstream current limit.
- **Controller**: the node where the user configures load sharing. It runs
   the allocation algorithm and pushes current limits to members.
- **Member**: a node that receives its load sharing configuration and current
   allocation from the controller. Load sharing config is read-only on members.
- **Peer**: another node on the LAN, discovered or configured.
- **Demanding**: a member that currently wants current (vehicle connected and
   not explicitly disabled).

## High-level architecture

### 1) Discovery (controller only)

- Each device already advertises an mDNS service:
   - Service: `openevse` / `tcp` port `80`
   - TXT records include `type`, `version`, `id`

The **controller** periodically performs mDNS queries for `_openevse._tcp`
(ESPmDNS uses `MDNS.queryService("openevse", "tcp")`).

Discovery output is a list of candidates with:

- unique device id (TXT `id`)
- hostname / instance name
- IP address
- firmware version

Member devices also run discovery initially (to show available peers in the
UI), but **stop discovery once they are connected to a controller** and have
received their load sharing configuration. Discovery resumes on members if the
controller connection is lost.

### 2) Group configuration (controller only)

A group is configured on the **controller** device only. The user sets up load
sharing from the controller's web UI or REST API. The controller then pushes
the configuration to each member.

Configuration includes:

- `group_id`: user-defined string
- `group_max_current`: the total current limit for the group (amps)
- `safety_factor`: optional reduction (e.g. 0.8 to use 80% of circuit)
- `members`: a list of peer hosts (hostname, IP address, or device ID)
- `priority`: node's priority (lower = higher priority)
- `failsafe`: what to do when members are unreachable (disable or limit to
   safe current)

Members are simple strings that can be:

- mDNS hostname (e.g., `openevse-1.local`)
- Static IP address (e.g., `192.168.1.101`)
- Device ID (e.g., `openevse_abc123`)

Members are managed via the `/loadsharing/peers` POST and DELETE endpoints
**on the controller only**. These endpoints return 403 on member devices.

#### Role designation

The device where the user first enables load sharing and adds peers **becomes
the controller** implicitly. When the controller pushes configuration to a
peer, that peer becomes a **member** automatically. Member devices:

- Accept load sharing configuration from the controller
- Display load sharing status as read-only in the UI
- Reject local changes to load sharing config fields (403)
- Accept current allocation commands from the controller

### 3) Status monitoring (controller → members)

The **controller** maintains a live view of all members by subscribing to their
WebSocket status stream (reusing the existing `/ws` endpoint):

- Connect to each member's WebSocket endpoint: `ws://<member-host>/ws`
- On connect, the member sends a full status document (current behavior).
- Subsequently, only changes are sent.

The controller keeps `last_seen` per member and considers a member **online**
if a message has been received within `heartbeat_timeout` seconds.

Members do not need to poll other members. Only the controller maintains
WebSocket connections to all members.

### 4) Allocation algorithm (controller only)

The **controller** runs the allocation algorithm and pushes results to members.
This centralized approach eliminates the need for distributed consensus or
configuration synchronization.

#### Inputs

For each member `i` (including self), the algorithm uses:

- `online_i`: boolean based on heartbeat
- `demand_i`: boolean derived from status (e.g. `vehicle==1` and state not
   disabled)
- `min_i`: EVSE minimum current (from EVSE config/status)
- `max_i`: EVSE max allowable current (pilot maximum)
- `priority_i`: optional (default derived from stable device id)

Group-level:

- `I_group = group_max_current * safety_factor`

#### Baseline profile: "Equal Share with Minimums"

1. Determine `D` = set of demanding members that are online.
2. If `D` is empty, allocation is 0 for all.
3. Compute `I_avail = I_group`.
4. If `I_avail >= sum(min_i for i in D)`:
    - Assign each demanding member `alloc_i = min_i`.
    - Distribute remaining current equally among demanding members, capped by
       `max_i`.
5. Else (not enough for everyone's minimum):
    - Select a subset `S ⊆ D` in deterministic order (sorted by device id).
    - Add members until `sum(min_i in S) <= I_avail`.
    - Allocate `alloc_i = min_i` for i in S, and `alloc_i = 0` for others.

This yields a safe "some charge, others wait" behavior when the
circuit cannot satisfy all minimums.

### 5) Allocation delivery (controller → members)

After computing allocations, the controller pushes each member's current limit
via the existing WebSocket connection (the same connection used for status
monitoring in step 3). The controller sends a JSON message containing the
allocation:

```json
{"loadsharing": {"target_current": 16.5, "reason": "equal_share"}}
```

Members receive this message and apply it as a **claim** via the existing
`EvseManager` claim system, using a dedicated `EvseClient_LoadSharing` client.
The claim uses `Priority_Limit` to coexist with manual overrides and other
claims.

The controller also applies its own allocation locally via the same claim
mechanism.

#### Other profiles (future)

- Priority-first: higher priority members get more current first.
- Reduce by percentage: everyone reduced proportionally.
- FIFO/ramp behavior: staged ramp-up like JuiceNet.

## Configuration management (controller/member model)

Because the controller is the single source of truth for load sharing
configuration, configuration drift between nodes is eliminated by design.

### How configuration flows

1. **User configures load sharing on the controller** via web UI or REST API:
   group settings, member list, failsafe parameters.
2. **Controller pushes config to each member** via `POST http://{member}/config`
   when a peer is first added to the group (after the controller establishes a
   WebSocket connection to the member).
3. **Member accepts config from controller** and transitions to `role=member`.
   Load sharing config fields become read-only on the member.
4. **If the user changes config on the controller**, the controller pushes the
   updated config to all online members. Members that are offline will receive
   the updated config when they reconnect.

### Member config lockout

When a device is operating as a member (`loadsharing_role=member`):

- Load sharing config fields are **read-only** (POST returns 403)
- `/loadsharing/peers` POST and DELETE endpoints return 403
- The web UI shows load sharing config as greyed-out / read-only
- The member stores the controller's hostname in
  `loadsharing_controller_host` for reconnection

### Config fields pushed by controller

The controller pushes these group-level settings to members:

- `loadsharing_enabled`: true
- `loadsharing_role`: "member"
- `loadsharing_controller_host`: controller's hostname/IP
- `loadsharing_group_id`
- `loadsharing_group_max_current`
- `loadsharing_safety_factor`
- `loadsharing_heartbeat_timeout`
- `loadsharing_failsafe_mode`
- `loadsharing_failsafe_safe_current`
- `loadsharing_failsafe_peer_assumed_current`

### Resetting a member

To remove a device from a load sharing group:

1. **From the controller**: DELETE the peer via `/loadsharing/peers/{host}`.
   The controller sends a config update to the member resetting
   `loadsharing_enabled=false` and `loadsharing_role=""`.
2. **From the member**: Factory-reset the load sharing config (or manually
   clear `loadsharing_role` and `loadsharing_controller_host` via the API
   with admin access).

## Applying the allocation

### Principle

The load-sharing subsystem computes an allowed current for each member and
delivers it as a **claim** via the existing `EvseManager` claim system. This
approach:

- Uses the existing claim priority hierarchy
- Coexists with manual overrides and solar divert
- Does not directly manage EVSE relays

### Controller applies allocations

On the **controller**:

1. The allocation algorithm computes per-member target current.
2. The controller applies its own allocation locally via a
   `EvseClient_LoadSharing` claim at `Priority_Limit`.
3. The controller sends each member's allocation via the WebSocket connection.

On each **member**:

1. The member receives an allocation message from the controller via WebSocket.
2. The member applies the allocation as a `EvseClient_LoadSharing` claim at
   `Priority_Limit`.
3. The claim is subject to the existing Current Shaper timeout failsafe.

### Voltage handling

When the controller computes power-based limits:

- **Preferred**: Use each member's measured voltage from WebSocket status
- **Fallback**: Use the controller's local measured voltage
- **Last resort**: Use nominal 240V

## Fail-safe behavior

Network failures are expected. The system must remain safe.

### Member failsafe (controller offline)

Each member applies its load sharing allocation as a **claim** in the
`EvseManager` claim system. The claim leverages the existing Current Shaper
timeout mechanism:

- If the controller does not send an allocation update within
  `current_shaper_data_maxinterval` (default: 120 seconds), the claim
  expires automatically.
- When the claim expires, the member reverts to safe behavior (existing
  shaper failsafe logic).

This means **no new failsafe code is needed** on members for the MVP. The
existing Current Shaper timeout provides the safety net.

### Controller failsafe (member offline)

The controller tracks each member's online status via WebSocket heartbeat.
A member is considered offline if `now - last_seen > heartbeat_timeout`.

When a member goes offline, the controller's allocation algorithm handles
this with **conservative accounting**:

- For each offline member, reserve `failsafe_peer_assumed_current` (default
  6A) as assumed consumption.
- This reduces the available current for online members, ensuring the group
  stays under its configured maximum even if the offline member continues
  charging.

### Fail-safe modes (configurable on controller)

1. **Safe current on peer loss (graceful degradation)** (default)
   - If any member is offline, reduce allocations to account for the
     offline member's assumed consumption.
   - Online members continue charging with reduced capacity.

2. **Disable on peer loss (strict safety)**
   - If any configured member is offline, stop charging on all members
     (allocation = 0).
   - Matches #592's "members refuse to charge on loss of connectivity"
     guidance.

### Controller failure scenario

If the controller goes offline permanently:

- Members' claims expire after the Current Shaper timeout (120s).
- Members revert to safe behavior (no load sharing active).
- A new controller must be manually designated to restore load sharing.
- **Automatic controller election is a future enhancement.**

## Security and authorization

- Group communication occurs over the local network and uses existing
   HTTP/WebSocket infrastructure.
- If the firmware is configured with authentication / security profile settings
   for websocket/API access, load sharing connections should reuse them.
- The design assumes members are on a trusted LAN. If not, a shared group
   secret or pairing token can be added later.

## API surface (summary)

Load sharing configuration is part of the main `/config` endpoint.

New REST API endpoints (available on **controller** only, except where noted):

- `GET /loadsharing/peers` - List discovered and configured peers
  *(read-only on members: shows own group membership)*
- `POST /loadsharing/peers` - Add a peer to the group *(controller only; 403
  on members)*
- `DELETE /loadsharing/peers/{host}` - Remove a peer from the group
  *(controller only; 403 on members)*
- `GET /loadsharing/status` - Get runtime status and allocations *(available
  on both controller and members; members show own allocation and controller
  connection status)*
- `POST /loadsharing/discover` - Trigger mDNS peer discovery *(controller
  only)*

See the OpenAPI spec updates in api.yml.

## Implementation plan (incremental)

1. Add data model + config persistence for load sharing.
2. Add mDNS discovery list endpoint (controller).
3. Add peer status ingestion (controller connects to members):
   - HTTP GET `/status` bootstrap for initial cache
   - WebSocket client for `/ws` subscriptions
4. Implement allocation algorithm on controller.
5. Implement allocation delivery: controller pushes allocations to members
   via WebSocket; members apply as claims.
6. Implement config push: controller pushes config to members on peer add.
7. Add fail-safe handling leveraging existing Current Shaper timeout.
8. Web UI for controller (config + dashboard) and members (read-only status).

## Testing strategy (no hardware required)

- Unit-test allocation algorithm with synthetic peer sets.
- Integration-test controller/member flow: config push, allocation delivery,
  claim application.
- Simulate member online/offline transitions and ensure failsafe triggers.
- Extend divert_sim with multi-peer scenarios using OpenEVSE_Emulator.
- Test controller failure: verify member claims expire within timeout.

## Future extensions

The following features are deferred from the initial MVP and may be
implemented in future versions:

### Distributed configuration synchronization

The MVP uses a controller/member model where the controller is the single
source of truth. A future enhancement could implement fully distributed
configuration synchronization:

- Config version tracking: each node maintains a monotonic `config_version`
  counter and `config_hash` of critical parameters.
- Config hash exchange: include config fingerprint in WebSocket status
  messages for mismatch detection.
- Automatic sync: nodes with older config versions fetch newer config from
  peers.
- Conflict resolution: simultaneous updates resolved by timestamp comparison
  with device ID tiebreaker.
- Conservative fallback: use most restrictive values when configs disagree.

This would allow any node to be configured and changes to propagate
automatically, eliminating the need for a designated controller.

### Automatic controller election

If the controller goes offline, the remaining members could automatically
elect a new controller based on a deterministic algorithm (e.g., lowest
device ID among online members). This would provide automatic failover
without manual intervention.

### Multiple allocation profiles

- Priority-first: higher priority members get more current first.
- Reduce by percentage: everyone reduced proportionally.
- FIFO/ramp behavior: staged ramp-up like JuiceNet.

### Hierarchical groups

Group-of-groups configuration like JuiceNet, allowing nested sharing
hierarchies for complex electrical installations.
