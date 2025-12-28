# Load Sharing (Local Power Sharing)

## Overview

This document describes a proposed **local load sharing** feature for OpenEVSE
ESP32 WiFi firmware, based on the requirements and discussion in GitHub issues
\#592 and \#940.

The design goal is to allow a set of OpenEVSE devices on the same LAN to share
a single upstream electrical service limit (e.g. a 50A circuit) **without
requiring a cloud service, MQTT broker, Home Assistant, or external
controller**.

The existing **Current Shaper** functionality remains responsible for
**enforcing** the computed limit on each device. This feature adds:

- Group formation (configuration + discovery)
- Peer-to-peer communication of EVSE status
- A deterministic distributed algorithm to compute each node’s allowed current
- Fail-safe behavior when peers are unreachable

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
- Avoid a fixed “master/slave” architecture where possible.
- Keep the system safe if some nodes are offline or the network is unstable.
- Reuse existing primitives:
  - mDNS advertising (already present)
  - Websocket status updates `/ws` (already present)
  - Current Shaper (already present) for limiting and failsafe timing behavior

## Non-goals

- Coordinating across multiple subnets / VLANs.
- Scheduling / queuing logic beyond minimal “who gets to charge when”.
- OCPP local controller / proxy behavior.
- Guaranteeing perfect fairness under all packet-loss scenarios.

## Terminology

- **Node**: one OpenEVSE WiFi gateway.
- **Group**: a set of nodes that share a common upstream current limit.
- **Member**: a node that is configured as part of a group.
- **Peer**: another node on the LAN, discovered or configured.
- **Demanding**: a member that currently wants current (vehicle connected and
   not explicitly disabled).

## High-level architecture

### 1) Discovery

- Each device already advertises an mDNS service:
   - Service: `openevse` / `tcp` port `80`
   - TXT records include `type`, `version`, `id`

Add a discovery process that periodically performs mDNS queries for
`_openevse._tcp` (ESPmDNS uses `MDNS.queryService("openevse", "tcp")`).

Discovery output is a list of candidates with:

- unique device id (TXT `id`)
- hostname / instance name
- IP address
- firmware version

### 2) Group configuration

A group is configured locally on each node.

Configuration includes:

- `group_id`: user-defined string
- `group_max_current`: the total current limit for the group (amps)
- `safety_factor`: optional reduction (e.g. 0.8 to use 80% of circuit)
- `members`: a list of peer hosts (hostname, IP address, or device ID)
- `priority`: local node's priority (lower = higher priority, NOT synced)
- `failsafe`: what to do when some members are unreachable (disable or limit to
   safe current)

Members are simple strings that can be:

- mDNS hostname (e.g., `openevse-1.local`)
- Static IP address (e.g., `192.168.1.101`)
- Device ID (e.g., `openevse_abc123`)

Members are managed via the `/loadsharing/peers` POST and DELETE endpoints.

### 3) Peer communication (status replication)

Each node maintains a live view of other members by subscribing to their
websocket status stream:

- Connect to each peer’s websocket endpoint: `ws://<peer-host>/ws`
- On connect, the peer sends a full status document (current behavior).
- Subsequently, only changes are sent.

Each node keeps `last_seen` per peer and considers a peer **online** if a
message has been received within `heartbeat_timeout` seconds.

### 4) Distributed allocation algorithm (no permanent master)

All nodes run the same deterministic calculation to decide their own allowed
current.

Key idea:

- Each node computes **the same view** of which members are online and
   demanding.
- Given the same inputs, each node computes the same per-member allocations.

This avoids a fixed master while still converging to a consistent group
behavior.

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

#### Deterministic member ordering

To ensure all nodes compute the same ordering:

- Sort members by a stable key, e.g. numeric `id` (TXT `id`) ascending.

#### Baseline profile: “Equal Share with Minimums”

1. Determine `D` = set of demanding members that are online.
2. If `D` is empty, allocation is 0 for all.
3. Compute `I_avail = I_group`.
4. If `I_avail >= sum(min_i for i in D)`:
    - Assign each demanding member `alloc_i = min_i`.
    - Distribute remaining current equally among demanding members, capped by
       `max_i`.
5. Else (not enough for everyone’s minimum):
    - Select a subset `S ⊆ D` in deterministic order.
    - Add members until `sum(min_i in S) <= I_avail`.
    - Allocate `alloc_i = min_i` for i in S, and `alloc_i = 0` for others.

This yields a safe, deterministic “some charge, others wait” behavior when the
circuit cannot satisfy all minimums.

#### Other profiles (future)

- Priority-first: higher priority members get more current first.
- Reduce by percentage: everyone reduced proportionally.
- FIFO/ramp behavior: staged ramp-up like JuiceNet.

## Handling configuration differences

Because load sharing is a distributed system with no permanent master, each
node maintains its own configuration. This creates the possibility of
**configuration drift** where nodes disagree about group parameters.

### Sources of configuration mismatch

Common scenarios:

1. **Different `group_max_current`**: One node configured for 50A, another for
   60A.
2. **Different member lists**: Node A knows about nodes B, C, D while node C
   only knows about A and B.
3. **Different `safety_factor`**: Nodes applying different derating factors.
4. **Different `failsafe` modes**: Some nodes configured to disable on peer
   loss, others configured for safe current.

### Impact of mismatched configuration

When nodes have different configurations, they will compute **different
allocations** for themselves:

- If Node A thinks `group_max_current = 50A` but Node B thinks it's `60A`,
  Node B may allocate more current to itself than Node A expects.
- This can cause the **actual group current** to exceed the physical circuit
  limit, potentially tripping breakers.

### Detection strategies

#### 1. Configuration hash exchange

Include a configuration fingerprint in peer status messages:

- Compute a hash of critical group config: `hash(group_id, group_max_current,
  safety_factor, member_ids)`
- Include this hash in websocket status updates
- Each node compares received hashes against its own

If hashes differ, the node knows there's a configuration mismatch.

#### 2. Explicit config version field

Add a `config_version` timestamp or counter to the group configuration:

- When group config is updated, increment the version
- Exchange versions in status messages
- Nodes can detect when they're running older/newer configs

### Handling mismatches

When a configuration mismatch is detected:

1. **Log a warning** to alert the administrator
2. **Trigger automatic synchronization** (see Configuration Synchronization
   below)
3. **Apply conservative behavior** until sync completes:
   - Use the **most restrictive** values encountered:
     - `min(group_max_current)` across all peers
     - `min(safety_factor)` across all peers
   - This ensures the group stays under the physical limit even with
     mismatched configs
4. **Optionally disable charging** if mismatch is severe (e.g., different
   `group_id` values)

### Status reporting

The `/loadsharing/status` endpoint should report configuration health:

```json
{
  "config_consistent": false,
  "config_issues": [
    {
      "peer_id": "openevse_abc123",
      "issue": "group_max_current mismatch",
      "local_value": 50.0,
      "peer_value": 60.0
    }
  ]
}
```

### Configuration synchronization

**Automatic configuration sync** is part of the initial implementation to
prevent configuration drift:

#### Config version tracking

Each node maintains:

- `config_version`: monotonically increasing integer, incremented on every
  config change
- `config_hash`: hash of critical group parameters
- `config_updated_at`: timestamp of last config change

These are exchanged in peer status messages.

#### Sync triggers

1. **Adding a new node to the group**:
   - New node discovers existing group members
   - Queries each peer's config version via `GET /config`
   - Adopts the configuration from the peer with the **highest
     `config_version`**
   - If multiple peers have the same highest version, use the one with the
     most recent `config_updated_at`

2. **Detecting version mismatch during operation**:
   - If a node receives status from a peer with `config_version > local_version`:
     - Fetch the newer configuration via `GET /config`
     - Merge/adopt the group load sharing settings
     - Increment local `config_version` to match

3. **User updates configuration on any node**:
   - Increment local `config_version`
   - Push the new configuration to all online peers via `POST /config`
   - Peers that receive the update increment their `config_version`
   - If a peer is offline, it will sync when it comes back online and detects
     the version mismatch

#### Sync behavior

When syncing configuration:

- **Group-level settings** are fully synchronized:
  - `group_id`
  - `group_max_current`
  - `safety_factor`
  - `members` list
  - `heartbeat_timeout`
  - `failsafe` mode

- **Node-local settings** are preserved:
  - Node's own hostname/identification
  - WiFi/network settings
  - Other EVSE config unrelated to load sharing

#### Conflict resolution

If two nodes are updated simultaneously (split-brain):

- Both increment their `config_version`
- When they communicate, both detect a mismatch
- The node with the **lower `config_updated_at` timestamp** adopts the
  configuration from the node with the higher timestamp
- If timestamps are identical (unlikely), use stable node ID as tiebreaker

#### User experience

- When user changes load sharing config on any node, the UI shows:
  - "Configuration updated and synced to X peers"
  - "Configuration updated; Y peers offline, will sync when available"
- The `/loadsharing/status` endpoint shows per-peer sync status

### Best practices for administrators

1. **Update configuration from any node**: The system will automatically sync
   to others.
2. **Monitor status endpoint**: Check for configuration warnings and sync
   failures.
3. **Use static IPs or stable hostnames**: Reduces connection issues during
   sync.
4. **Verify sync completion**: After config changes, check that all nodes
   report the same `config_version`.

## Applying the allocation (TBD)

### Principle

The load-sharing subsystem should *not* directly manage EVSE relays; it should
compute an allowed current and rely on Current Shaper to:

- apply claims to enforce the current limit
- pause charging when insufficient current exists
- provide time-based failsafe behavior

### Proposed integration strategy

Use Current Shaper to enforce group limit by treating “other members’ EVSE
draw” as the shaper’s `live_pwr` (if actual values are not available) input:

- Convert group current limit to power:
  $P_{max} = I_{group} \times V_{local}$ (or 3-phase equivalent)
- Estimate other-members power:
  $P_{others} = \sum_{j \neq self} I_{j} \times V_{j}$
  - Prefer using peers’ measured `amp` and `voltage` from `/ws` status
- Set:
  - `shaper.setMaxPwr(P_max)`
  - `shaper.setLivePwr(P_others)`

Because the shaper already adds local amps back into the computed limit,
`live_pwr` should represent **group load excluding local load** to avoid
double-counting.

This approach uses the existing shaper claim path (`EvseClient_OpenEVSE_Shaper`)
and its existing “data timeout => disable” logic.

If a more direct mapping is preferred later, extend shaper with an explicit
“external max current” input while reusing its claim and failsafe timing.

## Fail-safe behavior

Network failures are expected. The system must remain safe.

### Offline member handling

A member is considered offline if `now - last_seen > heartbeat_timeout`.

Fail-safe modes (configurable):

1. **Disable on peer loss (strict safety)**
   - If any configured member is offline, stop charging locally
      (allocation = 0).
   - Matches #592’s “members refuse to charge on loss of connectivity”
      guidance.

2. **Safe current on peer loss (graceful degradation)**
   - If any member is offline, clamp local allocation to
      `failsafe_current` (e.g. 6A) or 0.
   - Intended for environments where occasional dropouts shouldn’t fully
      halt charging.

### Conservative accounting

When a peer is offline, you cannot trust its real consumption.

To remain safe, treat an offline peer as consuming at least:

- `failsafe_peer_assumed_current` (default = its last known pilot, or
   configured safe value)

This reduces the locally available current so the group remains under its
configured maximum even if an offline peer continues charging.

## Security and authorization

- Group communication occurs over the local network and uses existing
   HTTP/WebSocket infrastructure.
- If the firmware is configured with authentication / security profile settings
   for websocket/API access, load sharing connections should reuse them.
- The design assumes members are on a trusted LAN. If not, a shared group
   secret or pairing token can be added later.

## API surface (summary)

Load sharing configuration is part of the main `/config` endpoint.

New REST API endpoints:

- `GET /loadsharing/peers` - List discovered and configured peers
- `POST /loadsharing/peers` - Add a peer to the group
- `DELETE /loadsharing/peers/{host}` - Remove a peer from the group
- `GET /loadsharing/status` - Get runtime status and allocations
- `POST /loadsharing/discover` - Trigger mDNS peer discovery

See the OpenAPI spec updates in api.yml.

## Implementation plan (incremental)

1. Add data model + config persistence for load sharing.
2. Add mDNS discovery list endpoint.
3. Add peer status ingestion:
   - Phase 1: poll `GET /status` for configured peers
   - Phase 2: implement websocket client for `/ws` subscriptions
4. Implement allocation algorithm and integrate with shaper via
   `setMaxPwr/setLivePwr`.
5. Add fail-safe handling and test with simulated peer loss.

## Testing strategy (no hardware required)

- Unit-test allocation algorithm with synthetic peer sets.
- Simulate peer online/offline transitions and ensure failsafe triggers.
- Add a small local harness (or extend divert_sim) to feed status snapshots and
   verify allocations converge.

