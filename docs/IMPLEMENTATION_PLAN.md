# Load Sharing Implementation Plan

This document breaks down the load-sharing feature into concrete implementation phases, with dependencies and estimated scope per phase.

**Current Status**: Phase 2 (Peer Status Ingestion) ✅ COMPLETED
- Phase 1 (Discovery & Peer Management) ✅ fully complete (14 integration tests passing)
- Phase 2 implementation complete:
  - WebSocket client connections to peers via `MongooseWebSocketClient` (ArduinoMongoose `websocket_client` branch)
  - HTTP GET `/status` bootstrap for initial peer status cache population
  - Background `LoadSharingPeerPoller` MicroTasks task with connection state machine
  - Per-peer connection tracking: DISCONNECTED → HTTP_FETCHING → WS_CONNECTING → WS_CONNECTED
  - Exponential backoff reconnection (1s base, 60s cap, 5 max retries)
  - Heartbeat monitoring (120s default timeout, configurable)
  - Status cache with delta updates from WebSocket messages (amp, voltage, pilot, vehicle, state)
  - `/loadsharing/status` endpoint returns per-peer status from poller cache
  - Config version/hash tracking in status cache for Phase 6 sync detection
- Ready for Phase 3 (Allocation Algorithm)

## Project Overview

**Goal**: Implement local peer-to-peer load sharing for OpenEVSE ESP32 WiFi firmware, allowing multiple chargers to share a single circuit breaker limit without cloud dependency.

**Key Constraints**:
- No permanent master/slave architecture
- All nodes run deterministic allocation algorithm independently
- Safety-first failsafe behavior on network loss
- Reuse existing primitives (mDNS, WebSocket, Current Shaper)

**Success Criteria**:
- 2–8 chargers can be configured in a group
- Allocation converges deterministically even with peer loss
- Failsafe triggers safely when communication fails
- Web UI allows simple group discovery and configuration

---

## Phase 0: Foundation & Data Model

### Objective
Establish configuration schema, persistence, and basic infrastructure for load sharing.

### Tasks

#### 0.1: Define LoadSharing config schema
- **Description**: Add configuration options to `ConfigJson` schema
- **Files to modify**:
  - `src/app_config.cpp` - implement config accessor/setter methods
  - `models/Config.yaml` - already contains the schema definition (see below)
- **Config fields** (already defined in Config.yaml):
  - `loadsharing_enabled: bool` (default: false)
  - `loadsharing_group_id: string` (user-defined group identifier; empty when disabled)
  - `loadsharing_group_max_current: number` (amps; total circuit limit)
  - `loadsharing_safety_factor: number` (default: 1.0; range 0–1; derating factor)
  - `loadsharing_heartbeat_timeout: int` (seconds; default 30; minimum 5)
  - `loadsharing_failsafe_mode: enum` ("disable" or "safe_current"; default "safe_current")
  - `loadsharing_failsafe_safe_current: number` (amps; default 6.0; used in safe_current mode)
  - `loadsharing_failsafe_peer_assumed_current: number` (amps; default 6.0; conservative offline peer current)
  - `loadsharing_priority: int` (node's priority; lower = higher; default 0; NOT synced)
  - `loadsharing_config_version: int` (readOnly; monotonic counter for config sync; added in Phase 6)
  - `loadsharing_config_updated_at: int` (readOnly; Unix timestamp; added in Phase 6)
- **Note**: Members list is managed separately via `/loadsharing/peers` API (Phase 1)
- **Dependencies**: ConfigJson library (already available)
- **Testing**: Validate via Phase 8 divert_sim integration tests

#### 0.2: Create LoadSharing data structures (C++)
- **Description**: Define in-memory models for peers, group state, allocations; persist peer list
- **Files to create**:
  - `src/loadsharing_types.h` - data structures
    - `class LoadSharingPeer` - hostname, IP, device_id, online status, last_seen, current draw
    - `class LoadSharingGroup` - config + runtime state
    - `class LoadSharingPeerStatus` - cached peer status (current, voltage, vehicle connected, etc.)
    - `class LoadSharingAllocationResult` - per-peer allocation computation results
  - `src/loadsharing_state.h/cpp` - state manager
    - Maintain in-memory peer list, status cache, allocation results
    - Thread-safe access (use existing spinlock/mutex patterns)
    - Persist peer member list to SPIFFS (simple JSON array of hostnames)
    - Load peer list on startup; fall back to empty list if missing
- **Dependencies**: None (config persistence already handled by ConfigJson)
- **Testing**: Validate via Phase 8 divert_sim integration tests

---

## Phase 1: Discovery & Peer Management

### Objective
Implement mDNS-based discovery and peer list management via REST API.

### Tasks

#### 1.1: Wrap existing mDNS discovery
- **Status**: ✅ COMPLETED
- **Description**: Create helper to query mDNS for OpenEVSE peers with async ESP-IDF support
- **Files created**:
  - `src/loadsharing_discovery.h/cpp` - discovery wrapper with caching
- **Implementation**:
  - `LoadSharingDiscovery` class wraps mDNS service discovery
  - Query `_openevse._tcp` service via `MDNS.queryService()`
  - Cache results with TTL (default 60 seconds)
  - Auto-refresh cache when TTL expires
  - Global `loadSharingDiscovery` instance
- **EpoxymDNS Enhancements**:
  - Added async API declarations to EpoxymDNS.h for ESP32-only async queries:
    - `mdns_search_once_t* mdns_query_async_new()` - initiate non-blocking query
    - `bool mdns_query_async_get_results()` - poll for results
    - `void mdns_query_async_delete()` - cleanup query handle
    - `void mdns_query_results_free()` - free result structures
  - These wrap ESP-IDF's mdns_query_async_* functions directly
  - Allows background discovery without blocking HTTP requests (used in Phase 1.6)
  - Forward declarations for `mdns_search_once_t`, `mdns_result_t`, `mdns_txt_item_t` (ESP32-only)
  - Native builds have conditional compilation stubs
- **Dependencies**: ESPmDNS (already used in firmware), EpoxymDNS (wrapper library)
- **Testing**: ✅ Compiles successfully on native build (2.97s)

#### 1.2: Implement `/loadsharing/peers` GET endpoint
- **Status**: ✅ COMPLETED
- **Files created/modified**:
  - `src/web_server_loadsharing.cpp` - all load sharing endpoints
- **Implementation**:
  - Returns combined list of discovered peers (with online status) and configured offline peers (joined=true)
  - Deduplicates results by hostname
  - Tracks which discovered peers are joined to the configured group
- **Response** (array of LoadSharingPeer with `joined` field):
  ```json
  {
    "data": [
      {
        "id": "unknown",
        "name": "openevse-1.local",
        "host": "openevse-1.local",
        "ip": "192.168.1.101",
        "online": true,
        "joined": true
      },
      {
        "id": "unknown",
        "name": "openevse-offline.local",
        "host": "openevse-offline.local",
        "ip": "",
        "online": false,
        "joined": true
      }
    ]
  }
  ```
- **Key Features**:
  - `online: boolean` - current discovery status (from mDNS)
  - `joined: boolean` - whether peer is in configured group (manually added via POST /loadsharing/peers)
  - Shows both discovered and offline configured peers
  - Deduplication removes duplicate hostnames from multiple network interfaces
- **Dependencies**: Phase 0 (config), Phase 1.1 (discovery), Phase 1.6 (background task)
- **Testing**: ✅ Verified with native build compilation and HTTP endpoint testing

#### 1.3: Implement `/loadsharing/peers` POST endpoint (add peer)
- **Status**: ✅ COMPLETED
- **Description**: Add a peer to the configured group
- **Files modified**: `src/web_server_loadsharing.cpp`
- **Implementation**:
  - Validates input (not duplicate, not self, resolvable host format)
  - Adds peer hostname to in-memory `configuredPeers` vector
  - Deduplicates entries
  - Returns success or 400 error with validation message
- **Request body**:
  ```json
  {
    "host": "openevse-2.local"
  }
  ```
- **Response**: `{"msg":"done"}` with status code 200
- **Validation**:
  - Reject duplicate hosts: "Peer already configured"
  - Validate host format (must contain '.' or ':' for domain/IP)
  - Reject empty host
- **Side Effects**:
  - Peer appears in GET /loadsharing/peers with `joined: true`
  - Peer is considered part of group even if not currently discovered
- **Dependencies**: Phase 1.1, 1.2, 1.6
- **Build Status**: ✅ Compiles successfully
- **Testing**: ✅ Validated with HTTP test requests

#### 1.4: Implement `/loadsharing/peers` DELETE endpoint
- **Status**: ✅ COMPLETED
- **Description**: Remove a peer from configured group
- **Files modified**: `src/web_server_loadsharing.cpp`
- **Implementation**:
  - Extracts hostname from URL path parameter `/loadsharing/peers/{host}`
  - Removes peer from `configuredPeers` vector
  - Returns success or 404 if peer not found
  - Handles both hostname and IP address formats
- **URL**: `/loadsharing/peers/{host}` (URL-encoded hostname)
- **Response**: `{"msg":"done"}` with status 200, or `{"msg":"Peer not found"}` with status 404
- **Side effects**:
  - Peer no longer appears in GET /loadsharing/peers (if not discovered)
  - Discovered peer still appears but with `joined: false`
- **Dependencies**: Phase 1.2
- **Build Status**: ✅ Compiles successfully
- **Testing**: ✅ Validated with HTTP test requests

#### 1.5: Implement `/loadsharing/discover` POST endpoint
- **Status**: ✅ COMPLETED
- **Description**: Trigger immediate mDNS discovery (on-demand refresh)
- **Files modified**: `src/web_server_loadsharing.cpp`
- **Implementation**:
  - Calls `loadSharingDiscoveryTask.triggerDiscovery()` to reset timer
  - Forces discovery on next task wake (within 2 seconds)
  - Returns success immediately
  - Non-blocking: returns before query completes
- **URL**: `POST /loadsharing/discover`
- **Response**: `{"msg":"done"}` with status 200
- **Behavior**:
  - Discovery runs asynchronously in background
  - GET /loadsharing/peers will reflect new results when cache is updated
- **Dependencies**: Phase 1.1, 1.6 (background task)
- **Build Status**: ✅ Compiles successfully
- **Testing**: ✅ HTTP endpoint confirmed working

#### 1.6: Add background discovery task
- **Status**: ✅ COMPLETED
- **Description**: Run continuous asynchronous peer discovery in background using MicroTasks scheduler
- **Files created**:
  - `src/loadsharing_discovery_task.h/cpp` - unified background task implementation combining discovery logic + MicroTasks scheduling
- **Implementation**:
  - Unified `LoadSharingDiscoveryTask` class implements both discovery methods and MicroTasks::Task interface
  - Global singleton instance `loadSharingDiscoveryTask` (pattern consistent with other global tasks like timeManager, scheduler)
  - Runs in MicroTasks background scheduler while firmware executes normally
  - Periodic query every 60-120 seconds (configurable via config, default 60 seconds)
  - Non-blocking: HTTP handlers never wait for mDNS results
  - Results cached with TTL (default 60 seconds)
  - Thread-safe cache access
  - POST /loadsharing/discover resets timer to force immediate discovery
- **Key Methods**:
  - `discoverPeers()` - performs mDNS query for `_openevse._tcp` services, caches results
  - `getCachedPeers()` - returns last discovered results immediately without blocking
  - `triggerDiscovery()` - forces discovery on next task wake
  - `isCacheValid()`, `invalidateCache()`, `cacheTimeRemaining()` - cache management
  - `setup()`, `loop()`, `begin()`, `end()` - MicroTasks lifecycle
- **Deduplication**:
  - Detects duplicate hostnames from mDNS results (same device on multiple network interfaces)
  - Tracks seen hostnames, skips duplicates, returns only unique peers per hostname
  - Debug logging shows number of results deduplicated
- **Behavior**:
  - Task wakes every 2 seconds to check if discovery should start
  - When cache TTL expires: initiates new mDNS query
  - Query runs asynchronously; GET /loadsharing/peers always returns cached results immediately
  - No blocking of HTTP request handling
  - Manual discovery (POST /loadsharing/discover) resets timer for immediate refresh
- **Build Status**: ✅ Compiles successfully (3.2 seconds native build)
- **Dependencies**: Phase 1.1 (EpoxymDNS discovery), MicroTasks
- **Testing**: Verified with native build compilation

#### 1.7: Persist configured peers to SPIFFS
- **Status**: ✅ COMPLETED
- **Description**: Save and load configured peer group to persistent storage so peers survive device reboots
- **Files modified**:
  - `src/loadsharing_discovery_task.h/cpp` - storage integrated directly into discovery task (no separate storage file needed)
- **Implementation**:
  - Storage methods added to `LoadSharingDiscoveryTask` class:
    - `loadGroupPeers()` - loads peer list from LittleFS on task startup
    - `saveGroupPeers()` - saves peer list to LittleFS (conditional on dirty flag)
  - Storage location: `/loadsharing_peers.json` in LittleFS root
  - Storage format: JSON array of hostnames
    ```json
    {
      "peers": [
        "openevse-1.local",
        "openevse-2.local",
        "192.168.1.100"
      ]
    }
    ```
  - **Atomic writes**: Write to `/loadsharing_peers.json.tmp` first, then rename to prevent corruption on power loss
  - On load failure (file missing): logs warning and starts with empty list (graceful degradation)
  - On JSON parse error: logs error and starts with empty list
  - Dirty flag (`_groupPeersDirty`) tracks when saves are needed to avoid unnecessary writes
- **Integration**:
  - Internal storage using `std::vector<String> _groupPeers` in discovery task
  - `LoadSharingDiscoveryTask::begin()` calls `loadGroupPeers()` on startup
  - `LoadSharingDiscoveryTask::addGroupPeer()` sets dirty flag and calls `saveGroupPeers()` immediately
  - `LoadSharingDiscoveryTask::removeGroupPeer()` sets dirty flag and calls `saveGroupPeers()` immediately
  - HTTP endpoints (`POST /loadsharing/peers`, `DELETE /loadsharing/peers/{host}`) trigger saves via discovery task methods
- **Migration**:
  - First boot with no storage file: logs "No persisted group peer list found, starting with empty list"
  - Upgrade from earlier version: automatically creates storage file on first peer add
  - No migration needed - clean slate on first deploy
- **Key Features**:
  - Atomic write-rename pattern prevents corruption
  - Immediate persistence on every add/remove operation
  - Dirty flag optimization avoids redundant writes
  - Graceful degradation on corrupted/missing file
  - Debug logging for all load/save operations
- **Dependencies**: Phase 1.2, 1.3, 1.4 (peer management API), Phase 1.6 (discovery task)
- **Build Status**: ✅ Compiles successfully on native build
- **Testing**: ✅ Storage methods integrated and callable via REST API

### Phase 1 Integration Test Suite

**Status**: ✅ COMPLETED - 14 tests, 100% passing (339.88s runtime)

**Location**: `tests/integration/test_loadsharing_peer_management.py` (336 lines)

**Test Infrastructure**:
- **Architecture**: Pytest + Docker + socat
  - Each test spawns paired instances: Docker emulator + native firmware binary
  - TCP-to-PTY bridge (socat) connects emulator's TCP RAPI port to host PTY
  - Solves namespace isolation: each test gets unique real PTYs (`/dev/pts/18`, `/dev/pts/19`, etc.)
  - Isolated persistent storage: each native firmware instance runs in unique temp directory
- **Fixtures** (`conftest.py`, 527 lines):
  - `pytest_configure()` - session initialization with port offset counter
  - `unique_port_offset()` - sequential assignment (0-9) per test
  - `peer_hostname_factory()` - unique hostnames per test offset (prevents NVS persistence collisions)
  - `instance_pair()` - factory for spawning paired emulator+native with auto-cleanup
  - `instance_pair_auto()` - convenience wrapper with auto-assigned unique ports
  - `check_mdns_support()` - mDNS validation + pre-cleanup stuck resources
  - Docker/image management, HTTP readiness polling (30s timeout), aggressive port cleanup (fuser, socat kill)

**Test Coverage** (14 tests):

*Core Peer Management* (9 tests):
- `test_peers_endpoint_initial_state` - GET /loadsharing/peers returns correct structure
- `test_discover_trigger` - POST /loadsharing/discover returns 200 OK
- `test_peer_discovery_mdns[2/3/4]` - mDNS discovery with 2, 3, and 4 simultaneous instances (parametrized)
- `test_add_peer_manual` - POST /loadsharing/peers adds peer with `joined=true`
- `test_add_peer_duplicate_rejection` - POST duplicate peer returns 400
- `test_delete_peer` - DELETE /loadsharing/peers/{host} removes joined status
- `test_delete_nonexistent_peer` - DELETE nonexistent peer returns 404
- `test_discovered_peers_joined_status[2/3/4]` - Verify discovered peers have correct `joined` status (parametrized)

*Response Structure* (2 tests):
- `test_peers_response_structure` - Validate JSON array with required fields (id, name, host, joined, ip, online)
- `test_error_response_structure` - Verify error responses include `msg` or `error` field

**Prerequisites**:
- Docker (emulator image)
- socat (TCP-to-PTY bridge)
- Avahi/mDNS (discovery validation)
- Native firmware build (`.pio/build/native/program`)
- Python 3.7+ with pytest, docker, requests libraries

**CI/CD Integration**:
- GitHub Actions workflow: `.github/workflows/integration_tests.yaml`
- Triggers: after build.yaml completes OR manual workflow_dispatch
- Matrix: parametrizes test runs with 2/3/4 instance counts
- Artifact handling: downloads native binary from build workflow
- Result publishing: publishes test report to PR

**Known Limitations & Workarounds**:
1. Docker/PTY namespace isolation: Solved with socat TCP-to-PTY bridge
2. NVS persistence collisions: Solved with isolated temp directories per instance
3. Port conflicts between tests: Solved with sequential counter-based port assignment
4. mDNS discovery timing: Tests wait up to 30s for peer discovery
5. Cannot parallelize tests: All use fixed port ranges (8000-8003, 8080-8083)

---

## Phase 2: Peer Status Ingestion

### Objective
Collect real-time status from peer OpenEVSE devices via initial HTTP request followed by persistent WebSocket subscriptions.

**Current Status**: ✅ COMPLETED
- All 3 sub-tasks implemented in `src/loadsharing_peer_poller.h/cpp` (295+541 lines)
- HTTP bootstrap, WebSocket client, background task all functional
- `/loadsharing/status` endpoint returns peer status from poller cache
- Integration tests created in `tests/integration/test_loadsharing_peer_status.py`

### Tasks

#### 2.1: Implement WebSocket client for peer `/ws`
- **Status**: ✅ COMPLETED
- **Description**: Maintain persistent WebSocket connection to each peer's `/ws` endpoint for real-time status and config sync metadata
- **Files created**:
  - `src/loadsharing_peer_poller.h` - `PeerConnection` struct with `MongooseWebSocketClient*`, `LoadSharingPeerStatus` cache, connection state tracking
  - `src/loadsharing_peer_poller.cpp` - `startWebSocketConnection()`, `handleWebSocketMessage()`, `checkWebSocketConnection()`
- **WebSocket Payload for Load Sharing**:
  - The peer's `/ws` endpoint sends status updates that include (in addition to standard status fields):
    - `config_version` (int): current config version for sync detection
    - `config_hash` (string): hash of critical group params (`group_id`, `group_max_current`, `safety_factor`, sorted member list)
    - Standard fields: `amp`, `voltage`, `state`, `pilot`, `vehicle`
  - **Initial message** (on connect): Full status snapshot via `buildStatus()` (same as GET /status)
  - **Subsequent messages**: Delta updates (only changed fields) via `web_server_event()`
- **Implementation**:
  - For each peer in the group, establish `ws://{peer_host}/ws` connection
  - `MongooseWebSocketClient` from ArduinoMongoose (`websocket_client` branch) wraps mongoose `mg_connect()` with HTTP upgrade
  - Callbacks registered: `setReceiveTXTcallback()` for message parsing, `setOnOpen()` for connection tracking, `setOnClose()` for failure handling
  - JSON messages parsed with ArduinoJson `DynamicJsonDocument(4096)`
  - Delta merge strategy: each field in message payload overwrites corresponding cache value
  - `last_seen` timestamp updated on every successful message receipt
  - `retryCount` reset to 0 on successful connection
  - PING/PONG keepalive interval: 15s (configurable via `setWsPingInterval()`)
  - Stale connection timeout: 30s (configurable via `setWsStaleTimeout()`)
- **Dependencies**: MongooseWebSocketClient (ArduinoMongoose websocket_client branch), Phase 1 (peer list)
- **Build Status**: ✅ Compiles successfully on native build
- **Testing**: ✅ Validated via Phase 2 integration tests with paired native firmware instances

#### 2.2: Implement initial HTTP GET request for peer `/status`
- **Status**: ✅ COMPLETED
- **Description**: Get initial status snapshot before opening WebSocket as bootstrap/fallback
- **Files created/modified**:
  - `src/loadsharing_peer_poller.cpp` - `startHttpBootstrap()` method
- **Implementation**:
  - Before opening WebSocket for a peer, issues `GET http://{peer_host}/status` via `MongooseHttpClient`
  - Async HTTP request with `onResponse()` and `onClose()` callbacks
  - Parses JSON response and populates `statusCache` with: amp, voltage, pilot, vehicle, state, config_version, config_hash
  - On HTTP success (200): transitions to `WS_CONNECTING` state, sets `hasInitialStatus=true`
  - On HTTP failure: transitions to `HTTP_FAILED` state, increments `retryCount`
  - On HTTP timeout (10s default): aborts request, transitions to `HTTP_FAILED`
  - Once WebSocket connects, WebSocket becomes primary status source
  - HTTP timeout configurable: `setHttpTimeout(ms)`
- **Key Fields Extracted from `/status` Response**:
  - `amp` - current draw (milliamps scaled)
  - `voltage` - supply voltage
  - `state` - EVSE state (J1772)
  - `pilot` - pilot current setpoint
  - `vehicle` - vehicle connection status (0/1)
  - `config_version` - for Phase 6 sync detection
  - `config_hash` - for Phase 6 mismatch detection
- **Dependencies**: MongooseHttpClient (ArduinoMongoose), Phase 2.1
- **Build Status**: ✅ Compiles successfully
- **Testing**: ✅ Validated via Phase 2 integration tests

#### 2.3: Background WebSocket management task
- **Status**: ✅ COMPLETED
- **Description**: Monitor and manage WebSocket connections for all peers; update peer online/offline status based on heartbeat
- **Files created**:
  - `src/loadsharing_peer_poller.h` - `LoadSharingPeerPoller` class extending `MicroTasks::Task`
  - `src/loadsharing_peer_poller.cpp` - full implementation (541 lines)
- **Implementation**:
  - `LoadSharingPeerPoller` runs as MicroTasks background task, wakes every 500ms (configurable)
  - Global instance: `loadSharingPeerPoller`
  - `begin(LoadSharingGroupState& groupState)` - registers with MicroTasks scheduler
  - `syncPeerList()` - synchronizes `_connections` map with authoritative peer list from `LoadSharingGroupState`:
    - Adds new peers (state: DISCONNECTED)
    - Removes deleted peers (disconnects WebSocket, cleans up)
    - Preserves existing connection state for unchanged peers
  - **Connection State Machine per Peer** (implemented in `processPeerConnection()`):
    ```
    DISCONNECTED → HTTP_FETCHING → WS_CONNECTING → WS_CONNECTED
         ↑           ↓                    ↓
         └── HTTP_FAILED ←──────── WS_FAILED
    ```
    - `DISCONNECTED` → initiates HTTP bootstrap (`startHttpBootstrap()`)
    - `HTTP_FETCHING` → waits for async HTTP response; timeout after `_http_timeout_ms`
    - `HTTP_FAILED` → waits for retry delay (`calculateRetryDelay(retryCount)`)
    - `WS_CONNECTING` → initiates WebSocket connection (`startWebSocketConnection()`)
    - `WS_CONNECTED` → monitors heartbeat, calls `wsClient->loop()`, checks staleness
    - `WS_FAILED` → waits for retry delay, then restarts from DISCONNECTED
    - `ERROR` → unrecoverable (e.g., malformed config); does nothing
  - **Heartbeat Logic**:
    - Updates `lastMessageTime` on every WebSocket message received
    - Peer is "stale" if `millis() - lastMessageTime > _heartbeat_timeout_ms` (default: 120s)
    - Stale peers transition to `WS_FAILED` and trigger reconnection
  - **Reconnection Strategy**:
    - Exponential backoff: `delay = min(base_interval * 2^retryCount, max_retry_interval)`
    - Base interval: 1000ms, max interval: 60000ms
    - Max retry count: 5 (configurable)
    - Retry counter reset on successful connection
  - **Statistics Tracking**:
    - `_total_messages_received` - WebSocket messages received across all peers
    - `_total_http_requests` - HTTP bootstrap requests issued
    - `_total_ws_connections` - WebSocket connections established
    - `_total_reconnects` - Reconnection attempts
  - **Public Query Methods**:
    - `getPeerStatus(host, outStatus)` - get cached status for specific peer
    - `getPeerConnectionState(host)` - get connection state enum
    - `isPeerConnected(host)` - check if WS_CONNECTED
    - `getAllOnlinePeerStatuses()` - all connected peers with status
    - `getOnlinePeerCount()` - count of WS_CONNECTED peers
    - `getStatistics(...)` - counters for monitoring/debugging
- **Configuration Methods** (all with sensible defaults):
  - `setPollInterval(ms)` - task wake interval (default 500ms)
  - `setHeartbeatTimeout(ms)` - mark offline threshold (default 120000ms)
  - `setBaseRetryInterval(ms)` - exponential backoff base (default 1000ms)
  - `setMaxRetryInterval(ms)` - exponential backoff cap (default 60000ms)
  - `setHttpTimeout(ms)` - HTTP request timeout (default 10000ms)
  - `setWsStaleTimeout(ms)` - WebSocket stale timeout (default 30000ms)
  - `setWsPingInterval(ms)` - WebSocket PING interval (default 15000ms)
  - `setMaxRetryCount(count)` - max retries before persistent offline (default 5)
- **Dependencies**: Phase 2.1, 2.2, MicroTasks, LoadSharingGroupState
- **Build Status**: ✅ Compiles successfully on native build
- **Testing**: ✅ Validated via Phase 2 integration tests

#### 2.4: `/loadsharing/status` endpoint with peer status
- **Status**: ✅ COMPLETED
- **Description**: Expose peer status data from poller cache via REST API
- **Files modified**: `src/web_server_loadsharing.cpp` - `handleLoadSharingStatus()`
- **Implementation**:
  - Returns JSON with: `enabled`, `group_id`, `computed_at`, `failsafe_active`, `online_count`, `offline_count`
  - `peers` array includes nested `status` object from peer poller cache:
    ```json
    {
      "peers": [{
        "id": "openevse_abc123",
        "host": "openevse-1.local",
        "online": true,
        "joined": true,
        "status": {
          "amp": 16.5,
          "voltage": 240,
          "pilot": 32,
          "vehicle": 1,
          "state": 3
        }
      }],
      "allocations": []
    }
    ```
  - Queries `loadSharingPeerPoller.getPeerStatus()` for each peer
  - Only includes `status` object if poller has cached status for that peer
- **Dependencies**: Phase 2.1-2.3, Phase 1 (peer list)
- **Build Status**: ✅ Compiles successfully
- **Testing**: ✅ Validated via Phase 2 integration tests

### Phase 2 Integration Test Suite

**Status**: ✅ COMPLETED

**Location**: `tests/integration/test_loadsharing_peer_status.py`

**Test Coverage** (planned):

*Core Status Ingestion* (tests):
- `test_status_endpoint_returns_valid_json` - GET /loadsharing/status returns valid JSON structure
- `test_status_endpoint_fields` - Response includes required fields (enabled, peers, allocations)
- `test_native_status_endpoint_has_required_fields` - GET /status on peer returns amp, voltage, pilot, state, vehicle
- `test_native_websocket_sends_status` - WebSocket /ws on peer sends initial status on connect
- `test_peer_status_ingested_after_add[2/3/4]` - After adding peer, /loadsharing/status shows peer with status data (parametrized)
- `test_peer_status_contains_amp_pilot_state` - Ingested peer status contains amp, pilot, state fields

*Peer Online/Offline Tracking* (tests):
- `test_peer_online_after_connection` - Peer marked online after successful connection
- `test_peer_status_multiple_peers[2/3/4]` - Multiple peers tracked simultaneously (parametrized)

*Response Structure* (tests):
- `test_status_response_structure` - Validate LoadSharingStatus JSON structure per api.yml
- `test_peer_status_nested_structure` - Validate nested status object fields

**Prerequisites**:
- Same as Phase 1 tests (Docker, socat, Avahi, native firmware build)
- Native firmware instances connect to each other's `/status` and `/ws` endpoints

---

## Phase 3: Allocation Algorithm

### Objective
Implement deterministic allocation logic that all peers can run independently.

### Tasks

#### 3.1: Implement allocation algorithm
- **Description**: Core "Equal Share with Minimums" algorithm with conservative accounting for offline peers
- **Files to create**:
  - `src/loadsharing_algorithm.h/cpp`
- **Implementation**:
  - Input: list of peers (online/offline, demand status, min/max current, priority)
  - Input: group max current, safety factor, failsafe settings
  - Output: per-peer allocated current
  - Algorithm steps:
    1. **Conservative offline accounting**: For each offline peer, reserve `failsafe_peer_assumed_current` (config) as assumed consumption
    2. Compute `I_avail = group_max_current * safety_factor - sum(offline peer reserves)`
    3. Filter demanding+online peers (ignore offline peers)
    4. If none demanding, return 0 for all
    5. If `I_avail >= sum(min_i for i in demanding)`: allocate min + equal share of remainder, capped by max_i
    6. Else (insufficient): select deterministic subset (by sorted device_id) until minimums fit; others get 0
    7. Return allocation map
  - Use deterministic ordering: sort peers by device_id (stable, consistent across all nodes)
  - Handle edge cases (divide by zero, negative current, etc.)
  - **Key principle**: Conservative behavior ensures group never exceeds circuit limit even if offline peer is still consuming
- **Dependencies**: Phase 0 (data types)
- **Testing**: Validate via Phase 8 divert_sim with various peer configurations

#### 3.2: Add allocation status endpoint `/loadsharing/status`
- **Description**: Expose current allocations, config health, and debug info
- **Files to modify**: `src/web_server_loadsharing.cpp`
- **Response** (LoadSharingStatus from api.yml, with config health additions):
  ```json
  {
    "enabled": true,
    "group_id": "house-garage",
    "computed_at": "2025-01-25T10:30:00Z",
    "failsafe_active": false,
    "online_count": 3,
    "offline_count": 0,
    "config_consistent": true,
    "config_issues": [],
    "peers": [
      {
        "id": "openevse_abc123",
        "name": "openevse-1",
        "host": "openevse-1.local",
        "ip": "192.168.1.101",
        "online": true,
        "last_seen": "2025-01-25T10:30:00Z",
        "status": {"amp": 16.5, "voltage": 240, "pilot": 32, "vehicle": 1, "state": 3}
      }
    ],
    "allocations": [
      {"id": "self", "target_current": 20.0, "reason": "equal share"},
      {"id": "openevse_abc123", "target_current": 20.0, "reason": "equal share"},
      {"id": "openevse_def456", "target_current": 10.0, "reason": "capped at max"}
    ]
  }
  ```
- **New fields** (from load-sharing.md §"Status reporting"):
  - `config_consistent: boolean` - true if all online peers have matching config_version
  - `config_issues: array` - list of detected config mismatches (see Phase 6.2 detection)
    - Each issue: `{peer_id, issue, local_value, peer_value}`
- **Dependencies**: Phase 3.1, Phase 2 (status ingestion), Phase 6 (config sync fields)
- **Testing**: Validate via Phase 8.3 allocation algorithm tests with synthetic peer configurations

#### 3.3: Run allocation computation periodically
- **Description**: Re-compute allocations on each status update or at fixed interval; event-driven on WebSocket messages
- **Files to modify**:
  - Background Validate via Phase 8 divert_sim integration tests
  - `src/loadsharing_state.h/cpp` - expose latest allocation
  - `src/loadsharing_peer_websocket.h/cpp` - trigger recomputation on status update
- **Event flow**:
  1. Phase 2.1 WebSocket receives peer status message → update peer status cache → call allocation recomputation
  2. Phase 2.3 background task detects heartbeat timeout → mark peer offline → call allocation recomputation
  3. Fallback: run allocation recomputation every N seconds (e.g., 5 seconds) even if no events
- **Implementation**:
  - Maintain flag indicating allocations are stale
  - On status change or timeout: set stale flag, trigger recomputation
  - On periodic tick: if stale flag set or forced interval elapsed, recompute
  - Update in-memory allocation state
  - Log significant changes (peer came online, allocation changed by >0.5A, etc.)
- **Dependencies**: Phase 3.1, 3.2, Phase 2
- **Testing**: Verify allocations update on status changes

---

## Phase 4: Current Shaper Integration
alidate via Phase 8 divert_sim integration test
### Objective
Apply computed allocations to constrain local charging current via Current Shaper.

### Tasks

#### 4.1: Design integration with Current Shaper
- **Description**: Understand how Current Shaper's `setMaxPwr` / `setLivePwr` work
- **Reference**: Load sharing doc section "Applying the allocation"
- **Review**:
  - `src/EvseClient_OpenEVSE_Shaper.cpp` - current implementation
  - How claims are made and enforced
  - How `live_pwr` (other devices' consumption) factors into limit
- **Deliverable**: Design doc or inline comments explaining integration

#### 4.2: Implement shaper integration
- **Description**: Feed group allocations into shaper claim path
- **Files to create**:
  - `src/loadsharing_shaper_bridge.h/cpp`
- **Voltage fallback strategy** (per load-sharing.md):
  - **Preferred**: Use peer's measured voltage from status if available
  - **Fallback 1**: Use local node's measured voltage (consistent across group)
  - **Fallback 2**: Use nominal 240V (if local voltage unavailable)
  - **Rationale**: Ensures consistent power calculation; local voltage is most stable approximation
- **Implementation**:
  - Subscribe to allocation updates from Phase 3.1
  - Convert allocated current to power: `P_self = allocated_current * voltage_selected`
  - Convert other peers' consumption: `P_others = sum(peer_amp * peer_voltage)` for each online peer
    - For each peer: use peer's reported voltage if available, else fall back to local voltage
    - Ignore offline peers (already reserved in Phase 3.1 conservative accounting)
  - Call shaper's `setMaxPwr(P_self)` and `setLivePwr(P_others)` with group limits
  - Keep existing shaper failsafe intact (timeout => disable)
- **Dependencies**: Phase 3, existing EvseClient_OpenEVSE_Shaper code
- **Testing**: Validate via Phase 8.6 end-to-end integration tests with OpenEVSE_Emulator

#### 4.3: Test end-to-end allocation → current limiting
- **Description**: Verify that changing allocation reduces charging current
- **Test setup**:
  - Native builValidate via Phase 8 divert_sim integration tests
  - Simulate multiple peers with synthetic status
  - Observe that local charger current is clamped per allocation
- **Dependencies**: Phase 4.2, Phase 3
- **Testing**: Integration test (no hardware required if using native/simulation)

---

## Phase 5: Failsafe & Safety

### Objective
Ensure system remains safe when peers go offline or network fails.

### Tasks

#### 5.1: Implement failsafe logic
- **Description**: Handle offline peer scenarios
- **Files to create**:
  - `src/loadsharing_failsafe.h/cpp`
- **Implementation**:
  - Track heartbeat for each peer (`last_seen` timestamp)
  - If `now - last_seen > heartbeat_timeout`, mark peer offline
  - On failsafe trigger:
    - Mode 1 (strict): stop charging (allocation = 0 for self)
    - Mode 2 (graceful): reduce to safe current (e.g., 6A minimum)
  - When offline peer comes back online, resume normal allocation
  - Log failsafe events
- **Dependencies**: Phase 0 (config), Phase 2 (peer status)
- **Testing**: Validate via Phase 8.4 failsafe behavior tests (peer disappearing, timeout verification, recovery scenarios)

#### 5.2: Add  Validate via Phase 8 divert_sim with simulated peer failureshods for failsafe config
  - `src/loadsharing_failsafe.h/cpp` - use config values
  - `src/loadsharing_algorithm.h/cpp` - use `failsafe_peer_assumed_current` in Phase 3.1 step 2
- **Config fields used** (from Config.yaml):
  - `loadsharing_failsafe_mode: enum` - "disable" or "safe_current"
  - `loadsharing_failsafe_safe_current: float` - safe current in amps when mode is "safe_current"
  - `loadsharing_failsafe_peer_assumed_current: float` - conservative offline peer current (used in Phase 3.1)
  - `loadsharing_heartbeat_timeout: int` - seconds before peer considered offline
- **Integration with allocation**: Phase 3.1 algorithm uses `failsafe_peer_assumed_current` to reserve capacity for offline peers
- **Dependencies**: Phase 0 (config defined), Phase 5.1 (failsafe logic)
- **Testing**: Validate via Phase 8.4 failsafe behavior tests (strict/graceful mode verification)

#### 5.3: Add failsafe mode to `/config` endpoint with config sync trigger
- **Description**: Allow user to change failsafe behavior via API; integrate with Phase 6 config sync
- **Files to modify**: 
  - `src/app_config.cpp` - ensure config POST handler includes load sharing fields
  - `src/loadsharing_config_sync.h/cpp` - increment config_version and trigger sync on POST success (Phase 6)
  - Note: This is part of the main config endpoint, not in `web_server_loadsharing.cpp`
- **Fields contValidate via Phase 8 divert_sim integration tests
  - `loadsharing_enabled`
  - `loadsharing_group_id`
  - `loadsharing_group_max_current`
  - `loadsharing_safety_factor`
  - `loadsharing_heartbeat_timeout`
  - `loadsharing_failsafe_mode`
  - `loadsharing_failsafe_safe_current`
  - `loadsharing_failsafe_peer_assumed_current`
  - `loadsharing_priority`
- **Side effects on POST success** (trigger Phase 6.3):
  - Increment `loadsharing_config_version` (via Phase 6.1)
  - Update `loadsharing_config_updated_at` to current Unix timestamp
  - Recompute `loadsharing_config_hash` (Phase 6.1)
  - Push updated config to all online peers (Phase 6.3)
- **Validation**: 
  - Reject invalid failsafe_mode (must be "disable" or "safe_current")
  - Reject negative currents or out-of-range values
  - Reject heartbeat_timeout < 5 seconds
- **Dependencies**: Phase 0 (config schema), Phase 5 (failsafe logic)
- **Testing**: API tests for valid/invalid failsafe config, schema validation

---

## Phase 6: Configuration Consistency & Synchronization

### Objective
Detect and resolve configuration drift between peers.
Validate via Phase 8 divert_sim integration tests
### Tasks

#### 6.1: Implement config hash / version tracking
- **Description**: Compute fingerprint of critical group config and add sync metadata; integrate with Phase 2.1 WebSocket
- **Files to create**:
  - `src/loadsharing_config_sync.h/cpp`
- **Fields used** (from Config.yaml):
  - `loadsharing_config_version: int` (readOnly; monotonic counter, incremented on update)
  - `loadsharing_config_updated_at: int` (readOnly; Unix timestamp of last update)
- **Config hash algorithm** (deterministic, used for sync detection):
  - Critical fields to hash: `group_id`, `group_max_current`, `safety_factor`, sorted member list (by hostname)
  - Serialization: JSON format with fields in alphabetical order
  - Hash function: SHA-256 (or CRC32 if performance critical)
  - Example: `hash(JSON.stringify({group_id: "...", group_max_current: 50, members: ["...", "...", ...], safety_factor: 1.0}))`
- **Implementation**:
  - On config change (triggered by Phase 5.3), increment `loadsharing_config_version`
  - Update `loadsharing_config_updated_at` to current Unix timestamp
  - Recompute config hash using algorithm above
  - Include version + hash in WebSocket messages from Phase 2.1 (add to `/ws` payload)
  - Include version + hash in `/loadsharing/status` endpoint response
  - Use `config_updated_at` timestamp for conflict resolution when both nodes have same version (higher timestamp wins)
- **Dependencies**: Phase 0 (config defined), Phase 2 (include in status)
- **Testing**: Validate via Phase 8.5 config synchronization tests (hash stability, version increment, conflict resolution)

#### 6.2: Implement config mismatch detection
- **Description**: Detect when peer configs diverge
- **Files to modify**:
  - `src/loadsharing_state.h/cpp` - check peer config versions on status update
  - `src/loadsharing_algorithm.h/cpp` - conservative fallback on mismatch
- **Implementation**:
  - Compare peeValidate via Phase 8 divert_sim integration test
  - If mismatch detected:
    - Log warning
    - Mark config as inconsistent
    - Fetch peer config via `GET /config` for comparison
    - Apply conservative behavior (use min of group_max_current, etc.)
- **Dependencies**: Phase 6.1, Phase 2
- **Testing**: Validate via Phase 8.5 config synchronization tests (mismatch detection, conservative fallback)

#### 6.3: Implement automatic config synchronization
- **Description**: Push config changes to peers, adopt newer config from peer, with retry and partial failure handling
- **Files to create**:
  - `src/loadsharing_config_sync.h/cpp` - sync logic
- **Sync trigger conditions**:
  1. **User config update locally** (Phase 5.3): increment version, push to all online peers
  2. **Peer comValidate via Phase 8 divert_sim integration testtch newer config from peer
  3. **Peer added to group** via `/loadsharing/peers` POST: fetch config from newly added peer
  4. **Mismatch detected during operation** (Phase 6.2): query peer for newer config
- **Implementation**:
  - **Pushing config to peers** (triggered by Phase 5.3):
    - For each online peer: `POST http://{peer}/config` with load sharing fields + `config_version`
    - Retry with exponential backoff (1s, 2s, 4s) for offline peers; don't block UI
    - Log success/failure per peer; show in `/loadsharing/status` sync status
    - If peer is offline, sync will happen when peer comes back online (Phase 6.2 detection)
  - **Adopting newer config from peer**:
    - When peer status received with `config_version > local_version`:
      - Fetch full config via `GET http://{peer}/config`
      - Validate config (basic sanity checks)
      - Merge group-level settings into local config
      - Increment local `config_version` to match peer version
      - Update `config_updated_at` to max(local, peer) timestamp
  - **Conflict resolution** (simultaneous updates):
    - If both nodes updated simultaneously (same version, different hashes):
      - Winner: the node with later `config_updated_at` timestamp
      - Loser adopts winner's config
      - Tiebreaker: use stable node device_id comparison (lexicographic sort)
  - **Partial failure handling**:
    - Some peers unreachable: note in status, retry async
    - Config fetch fails: log error, try again on next status message from peer
    - Invalid config received: reject, log error, don't sync
- **Dependencies**: Phase 6.1, 6.2, Phase 2, web server endpoints
- **Testing**: Validate via Phase 8.5 config synchronization tests (version comparison, automatic sync propagation)

#### 6.4: Extend `/loadsharing/status` to report config health
- **Description**: Add config consistency info to status endpoint
- **Files to modify**: `src/web_server_loadsharing.cpp` (update `/loadsharing/status` response)
- **Response additions** (to LoadSharingStatus):
  - Include `coValidate via Phase 8 divert_sim with multi-node scenarios
  - Log warnings if mismatch detected
- **Example with mismatch detection**:
  ```json
  {
    "enabled": true,
    "group_id": "house-garage",
    "computed_at": "2025-01-25T10:30:00Z",
    "failsafe_active": false,
    "online_count": 2,
    "offline_count": 1,
    "peers": [
      {
        "id": "openevse_abc123",
        "host": "openevse-1.local",
        "online": true,
        "last_seen": "2025-01-25T10:30:00Z"
      }
    ],
    "allocations": [...]
  }
  ```
- **Dependencies**: Phase 6.1, 6.2 (config version tracking)
- **Testing**: Validate via Phase 8.2 (/loadsharing/status endpoint) and Phase 8.5 config sync tests

---

## Phase 7: Web UI / Frontend

### Objective
Provide user-friendly interface for configuring and monitoring load sharing.

### Tasks
- **Testing**: Validate via Phase 8 divert_sim integration tests
  - Peer list with online/offline indicator, allocated current, actual current draw
  - Group utilization gauge (total actual vs group max)
  - Config consistency warnings
  - Last computed timestamp
  - Failsafe status (active/inactive)
- **Dependencies**: Web API for `/loadsharing/status`, Svelte
- **Testing**: Manual testing

#### 7.3: Add load sharing endpoints to API spec
- **Status**: ✅ COMPLETED - Schema updated with `joined` field
- **Description**: Update OpenAPI spec (api.yml) with new endpoints and improved LoadSharingPeer schema
- **Files modified**: `api.yml`
- **Completed Updates**:
  - `LoadSharingPeer` schema enhanced with `joined` field (boolean): Indicates if peer is member of configured group
  - All endpoints documented:
    - `GET /loadsharing/peers` - list discovered and configured peers (with `joined` status)
    - `POST /loadsharing/peers` - add peer to group
    - `DELETE /loadsharing/peers/{host}` - remove peer from group
    - `GET /loadsharing/status` - status and allocations
    - `POST /loadsharing/discover` - trigger discovery
- **Schema Improvements**:
  - Added `joined: boolean` to LoadSharingPeer: "True if this peer is joined to the load sharing group (manually configured)"
  - GET endpoint now shows:
    - Discovered peers with `online: true` and `joined: true/false`
    - Configured offline peers with `online: false` and `joined: true`
- **Testing**: ✅ OpenAPI schema validation passed

---

## Phase 8: Testing & Validation with divert_sim

### Objective
Ensure reliability and safety of load sharing system using divert_sim extended with OpenEVSE_Emulator integration for multi-peer simulation.

### Overview
Since unit testing is challenging for this project, all automated testing will be performed via the divert_sim native build framework. The divert_sim will be extended to:
1. Instantiate multiple OpenEVSE_Emulator instances as virtual peers
2. Simulate network communication (HTTP, WebSocket) between peers
3. Test allocation algorithms, failsafe behavior, and config synchronization
4. Validate end-to-end scenarios without physical hardware

### Tasks

#### 8.1: Extend divert_sim with multi-peer support
- **Description**: Add infrastructure to divert_sim for managing multiple simulated OpenEVSE peers
- **Files to create/modify**:
  - `divert_sim/test_loadsharing.py` - pytest test suite for load sharing scenarios
  - `divert_sim/loadsharing_harness.py` - harness to orchestrate multi-peer simulations
  - `divert_sim/requirements.txt` - add dependencies (requests, websocket-client, etc.)
- **Implementation**:
  - Create Python harness that can:
    - Launch multiple OpenEVSE_Emulator instances on different ports
    - Configure each emulator with different device IDs and status
    - Provide mock HTTP endpoints for `/status`, `/config`, `/loadsharing/*`
    - Simulate WebSocket `/ws` connections with status updates
    - Control peer behavior (go offline, change status, modify config)
  - Integrate with existing divert_sim test framework (run_simulations.py pattern)
  - Use pytest fixtures for setup/teardown of peer instances
- **Dependencies**: OpenEVSE_Emulator repository
- **Testing approach**: Extend existing pytest framework with new test module

#### 8.2: OpenEVSE_Emulator integration layer
- **Description**: Create Python wrapper to use OpenEVSE_Emulator as load sharing peer
- **Files to create**:
  - `divert_sim/emulator_peer.py` - wrapper class for OpenEVSE_Emulator instance
  - `divert_sim/mock_wifi_endpoints.py` - mock WiFi firmware endpoints for load sharing
- **Implementation**:
  - **EmulatorPeer class**:
    - Initialize OpenEVSE_Emulator in-process (import from OpenEVSE_Emulator/src)
    - Configure unique device_id, hostname, IP address
    - Expose HTTP endpoints: GET/POST /status, GET/POST /config, GET/POST/DELETE /loadsharing/*
    - Implement WebSocket /ws server with status streaming
    - Add config_version and config_hash to WebSocket messages
    - Simulate network latency, message loss, connection failures
  - **MockWiFiEndpoints**:
    - Implement load sharing REST API per api.yml spec
    - Maintain in-memory peer list, allocations, config state
    - Return LoadSharingPeer, LoadSharingStatus schemas
    - Allow test code to inject failures (404, timeout, invalid data)
- **Key features**:
  - Each EmulatorPeer runs RAPI protocol (existing OpenEVSE_Emulator feature)
  - EV/EVSE state machine fully functional (charging, vehicle connected, etc.)
  - Can simulate actual current draw based on allocation
  - Test can control EV battery SoC, connection state, charge rate
- **Dependencies**: OpenEVSE_Emulator (git submodule or pip install if packaged)

#### 8.3: Allocation algorithm validation tests
- **Description**: Comprehensive test suite for deterministic allocation with various peer configurations
- **Files to create**:
  - `divert_sim/test_loadsharing_allocation.py`
- **Test scenarios**:
  1. **Single peer demanding (baseline)**:
     - 1 peer, 50A group limit, peer requests 32A
     - Expected: peer allocated 50A (capped by limit)
  2. **Two peers, equal share**:
     - 2 peers, 50A group limit, both demanding 32A
     - Expected: each allocated 25A
  3. **Three peers, sufficient capacity**:
     - 3 peers, 60A group limit, all demanding, min=6A each
     - Expected: each allocated 20A (equal share)
  4. **Insufficient capacity for minimums**:
     - 4 peers, 20A group limit, all demanding, min=6A each (total=24A needed)
     - Expected: deterministic subset gets 6A (3 peers by device_id sort), 1 peer gets 0A
  5. **Mixed demanding and idle peers**:
     - 3 peers, 50A limit: peer1 demanding, peer2/peer3 idle (vehicle=0)
     - Expected: peer1 gets 50A, others get 0A
  6. **Offline peer with conservative accounting**:
     - 3 peers, 50A limit, peer3 offline (failsafe_peer_assumed_current=6A)
     - Expected: peer1 and peer2 share (50-6=44A) = 22A each
  7. **Edge case: zero peers**:
     - No peers in group
     - Expected: no allocations
  8. **Edge case: zero available current**:
     - Group max current = 0A or all offline peers consume full capacity
     - Expected: all peers allocated 0A
  9. **Priority ordering (future)**:
     - 2 peers with different priorities
     - Expected: allocation respects priority (when implemented)
- **Test implementation**:
  - Each test creates peer configurations with specific status
  - Invokes native-build allocation algorithm via divert_sim
  - Verifies allocation map matches expected values
  - Logs allocation reasons for debugging
- **Assertions**:
  - Allocation totals never exceed group_max_current * safety_factor
  - Offline peer reserves are correctly subtracted
  - Allocations are deterministic (same inputs → same outputs)
  - Device_id ordering is consistent

#### 8.4: Failsafe behavior validation tests
- **Description**: Verify system remains safe during fault scenarios
- **Files to create**:
  - `divert_sim/test_loadsharing_failsafe.py`
- **Test scenarios**:
  1. **Strict mode: all peers offline**:
     - Config: `loadsharing_failsafe_mode="disable"`
     - Setup: 3 peers, all go offline (WebSocket timeout)
     - Expected: local allocation = 0A, failsafe_active = true
     - Verify: shaper receives 0A limit
  2. **Graceful mode: all peers offline**:
     - Config: `loadsharing_failsafe_mode="safe_current"`, `loadsharing_failsafe_safe_current=6.0`
     - Setup: 3 peers, all go offline
     - Expected: local allocation = 6A, failsafe_active = true
  3. **Partial peer loss**:
     - Setup: 3 peers active, peer3 goes offline (heartbeat timeout)
     - Expected: allocation recomputed for peer1 and peer2, reserves peer3's assumed current
     - Verify: online peers share remaining capacity
  4. **Peer recovery**:
     - Setup: peer3 offline, then comes back online (WebSocket reconnects)
     - Expected: failsafe deactivates, allocation restored to include peer3
     - Verify: smooth transition without charging interruption
  5. **Heartbeat timeout edge case**:
     - Setup: peer's last_seen exactly at timeout boundary
     - Expected: deterministic online/offline decision
  6. **Rapid offline/online cycling**:
     - Setup: peer goes offline and online repeatedly (network instability)
     - Expected: exponential backoff prevents thrashing, allocation remains safe
- **Test implementation**:
  - Use EmulatorPeer to simulate WebSocket disconnect
  - Advance simulated time to trigger heartbeat timeout
  - Verify failsafe state transitions
  - Check shaper integration receives correct limits
- **Safety validation**:
  - Total group current never exceeds physical limit
  - Failsafe always errs on the side of caution (under-allocate vs. over-allocate)

#### 8.5: Configuration synchronization tests
- **Description**: Verify config sync, mismatch detection, and conservative fallback behavior
- **Files to create**:
  - `divert_sim/test_loadsharing_config_sync.py`
- **Test scenarios**:
  1. **Config mismatch detection**:
     - Setup: peer1 has group_max_current=50A, peer2 has 60A
     - Expected: both peers detect mismatch (config_version or config_hash differs)
     - Verify: `/loadsharing/status` reports config_consistent=false, config_issues populated
  2. **Conservative fallback on mismatch**:
     - Setup: same as above (50A vs 60A)
     - Expected: both peers use min(50, 60) = 50A for allocation
     - Verify: neither peer exceeds the lower limit
  3. **Automatic config sync: newer version propagates**:
     - Setup: peer1 config_version=2, peer2 config_version=1
     - Expected: peer2 fetches config from peer1 (GET /config)
     - Verify: peer2 updates to version=2, config_consistent=true
  4. **User config update propagates**:
     - Setup: user POSTs new config to peer1 (change group_max_current from 50A to 55A)
     - Expected: peer1 increments config_version, pushes to peer2 (POST /config)
     - Verify: peer2 adopts new config, allocations update
  5. **Peer offline during config update**:
     - Setup: peer3 offline when peer1 pushes config update
     - Expected: peer1 logs sync failure for peer3
     - When peer3 comes online: peer3 detects higher config_version, fetches newer config
     - Verify: peer3 eventually syncs
  6. **Conflict resolution (simultaneous updates)**:
     - Setup: peer1 and peer2 both update config simultaneously (same version, different values)
     - Expected: winner determined by config_updated_at timestamp (later wins)
     - Verify: loser adopts winner's config
  7. **Config hash stability**:
     - Setup: compute config_hash for group_id="test", max_current=50, members=["a","b","c"]
     - Recompute hash with same values
     - Expected: hashes match (deterministic)
     - Recompute with members in different order: ["c","b","a"]
     - Expected: hashes match (sorted before hashing)
- **Test implementation**:
  - Mock GET /config and POST /config endpoints
  - Simulate config_version and config_hash exchange in WebSocket messages
  - Verify config sync triggers and results
  - Test retry logic and backoff on sync failures

#### 8.6: End-to-end integration tests
- **Description**: Full workflow from discovery to charging with allocation, including config sync and fault scenarios
- **Files to create**:
  - `divert_sim/test_loadsharing_e2e.py`
- **Test scenarios**:
  1. **Happy path: 3-peer group with stable charging**:
     - Setup:
       - Launch 3 EmulatorPeer instances on ports 8001, 8002, 8003
       - Configure each with unique device_id (openevse_001, _002, _003)
       - Set group_id="test_group", group_max_current=60A on all peers
       - Connect EV to each EVSE (vehicle=1, state=2 or 3)
     - Actions:
       - Peer1 discovers peer2 and peer3 via mDNS (mocked)
       - Peer1 adds peer2 and peer3 to group (POST /loadsharing/peers)
       - Peer1 subscribes to peer2 and peer3 WebSocket /ws
       - Allocations computed: each peer gets ~20A
       - Verify shaper integration: peer1 limited to 20A
       - EV charging progresses on all 3 peers
     - Assertions:
       - Total current draw ≤ 60A at all times
       - Each peer reports correct allocation in /loadsharing/status
       - config_consistent=true across all peers
   2. **Peer failure and recovery**:
     - Continuing from scenario 1
     - Actions:
       - Disconnect peer3's WebSocket (simulate network loss)
       - Wait for heartbeat_timeout (30s simulated)
       - Verify failsafe activates: allocation recomputed for peer1 and peer2
       - peer1 and peer2 share (60 - 6) = 54A (reserves peer3's assumed 6A)
       - Reconnect peer3's WebSocket
       - Verify allocation restores: all 3 peers back to ~20A
     - Assertions:
       - During peer3 offline: total actual draw + reserved < 60A
       - Smooth transition on peer3 recovery
       - No charging interruption on peer1 and peer2
   3. **Config mismatch and sync**:
     - Setup: peer1 and peer2 have config_version=1, group_max_current=60A
     - Actions:
       - Manually change peer3's config to group_max_current=50A (config_version=1, different hash)
       - peer1 detects mismatch when receiving peer3's WebSocket message
       - peer1 logs warning, uses conservative min(60, 50) = 50A
       - Verify `/loadsharing/status` shows config_consistent=false
       - Trigger config sync: peer3 fetches config from peer1 (higher timestamp)
       - peer3 updates to 60A, config_consistent=true
     - Assertions:
       - During mismatch: all peers limit to 50A (conservative)
       - After sync: all peers use 60A
   4. **Discovery and dynamic peer addition**:
     - Setup: peer1 and peer2 in group
     - Actions:
       - Launch peer3 (not yet in group)
       - peer1 triggers discovery (POST /loadsharing/discover)
       - peer3 appears in discovery results
       - peer1 adds peer3 (POST /loadsharing/peers with peer3's hostname)
       - peer1 establishes WebSocket to peer3
       - Allocations recomputed to include peer3
     - Assertions:
       - peer3 seamlessly joins group
       - Allocations converge deterministically
   5. **Stress test: rapid status changes**:
     - Setup: 3 peers in group
     - Actions:
       - Simulate rapid EV state changes (connect/disconnect, start/stop charging)
       - Each peer's current draw fluctuates (0A → 32A → 16A → 0A)
       - WebSocket messages sent for each change
       - Verify allocations update correctly
     - Assertions:
       - No allocation glitches or race conditions
       - Total group current always under limit
       - Allocation computation is performant (< 100ms per update)
- **Test infrastructure**:
  - Use pytest fixtures for peer lifecycle management
  - Mock mDNS discovery responses
  - Capture WebSocket message traffic for debugging
  - Simulate time advancement (control heartbeat timeout without waiting 30s real-time)
  - Measure allocation computation latency

#### 8.7: Performance and stress tests
- **Description**: Validate system performance under load
- **Files to create**:
  - `divert_sim/test_loadsharing_performance.py`
- **Test scenarios**:
  1. **8-peer maximum group size**:
     - Setup: 8 peers all demanding
     - Verify: allocation computation completes in < 200ms
     - Verify: deterministic allocation across all peers
  2. **High-frequency status updates**:
     - Setup: 3 peers sending WebSocket messages every 1 second
     - Run for 600 simulated seconds (10 minutes)
     - Verify: no memory leaks, allocation remains stable
  3. **Network instability simulation**:
     - Setup: 4 peers with random WebSocket disconnects/reconnects
     - Simulate packet loss (10% of messages dropped)
     - Verify: system remains safe, reconnection logic works
  4. **Config sync storm**:
     - Setup: user changes config 10 times in 10 seconds
     - Verify: all peers eventually converge to latest config
     - Verify: no race conditions or config corruption

#### 8.8: CI integration and test result reporting
- **Description**: Integrate comprehensive test suite into existing CI/CD pipeline and leverage existing test result visualization
- **Existing Infrastructure**:
  - GitHub Actions workflow (`divert_sim.yaml`) already builds and runs divert_sim native build
  - pytest JUnit XML output written to `output/test_results.xml`
  - EnricoMi `publish-unit-test-result-action` already publishes results as GitHub checks on PRs
  - Web viewer (`view.html` + `server.py`) provides interactive simulation result visualization
  - Existing `run_simulations.py` harness generates CSV results in `output/` directory
- **Load Sharing Test Integration**:
  - New test modules: `test_loadsharing_*.py` (Phases 8.3-8.7)
  - Tests run automatically via pytest in CI (same workflow as existing divert tests)
  - JUnit XML output combines all load sharing test results
  - Performance metrics extracted from pytest output: `allocation_time_ms`, `peer_discovery_ms`, `config_sync_time_ms`
  - Optional: extend `run_simulations.py` to include load sharing test scenarios (CSV output for web viewer)
- **Result Reporting**:
  - GitHub PR check displays: pass/fail counts, execution time, failure details
  - Artifacts uploaded: `output/test_results.xml`, `output/summary_*.csv` (if using run_simulations)
  - Web viewer available in artifacts: interactive graphs, scenario-by-scenario breakdown
  - Full pytest output in GitHub check details: test names, assertions, timings
- **CI Workflow** (no changes needed to existing divert_sim.yaml):
  1. GitHub Actions checks out repository and dependencies (already done)
  2. Builds divert_sim native executable (already done)
  3. Runs `pytest -v --color=yes --junit-xml=output/test_results.xml` (existing pattern)
  4. EnricoMi action publishes test results as GitHub check on PR (existing integration)
  5. Artifacts uploaded: test results and web viewer (existing pattern)
  6. Load sharing tests included automatically in pytest discovery
- **No separate tool needed**: Reuses existing infrastructure; tests integrate seamlessly into existing CI/CD pipeline

---

## Phase 9: Documentation & Release

### Objective
Provide clear documentation and prepare for production release.

### Tasks

#### 9.1: Write user guide
- **Description**: Explain load sharing feature to end users
- **Files to create**: `docs/load-sharing-user-guide.md`
- **Content**:
  - What is load sharing and why it's useful
  - Prerequisites (devices on same LAN, etc.)
  - Step-by-step setup: discover, add, configure, monitor
  - Best practices (static IPs, failsafe modes, safety considerations)
  - Troubleshooting (config mismatch, peers not discovered, etc.)
- **Testing**: Peer review by non-technical user

#### 9.2: Write developer guide / architecture doc
- **Description**: Explain design decisions and code organization
- **Files to update/create**: `docs/load-sharing-dev-guide.md`
- **Content**:
  - High-level architecture (discovery, status, allocation, shaper integration)
  - Data structures and state management
  - Allocation algorithm deep dive
  - Configuration sync protocol
  - Failsafe mechanisms
  - Testing approach
  - Future extensions (priority-first, FIFO, multiple groups)
- **Testing**: Technical review

#### 9.3: API documentation updates
- **Description**: Ensure OpenAPI spec and endpoint docs are complete
- **Files to modify**: `api.yml`, `docs/api-guide.md`
- **Content**:
  - All new endpoints documented with schemas
  - Request/response examples
  - Error codes
  - Authentication (if applicable)
- **Testing**: Generate Swagger UI, verify all endpoints present

#### 9.4: Testing report & release notes
- **Description**: Document test results and known limitations
- **Files to create**: `LOAD_SHARING_RELEASE_NOTES.md`
- **Content**:
  - Test coverage summary (unit, integration, e2e)
  - Known issues / limitations
  - Future roadmap (priority-first, multiple profiles, etc.)
  - Migration guide (if any breaking changes)
- **Testing**: Included in release PR

#### 9.5: Code review & cleanup
- **Description**: Final review, remove debug code, optimize
- **Tasks**:
  - Code review across all load sharing files
  - Remove debug logging (or make it configurable)
  - Performance review (memory, CPU, network)
  - Ensure error handling is complete
  - Verify thread safety
- **Testing**: Static analysis, stress tests

---

## Implementation Sequence & Dependencies

### Critical Path (Minimal MVP)

1. **Phase 0**: Foundation & config (required by all phases)
2. **Phase 1**: Discovery & peer mgmt (UI needs this)
3. **Phase 2a**: HTTP polling (simplest status ingestion)
4. **Phase 3**: Allocation algorithm (core logic)
5. **Phase 4**: Shaper integration (make it work)
6. **Phase 5**: Failsafe (safety critical)
7. **Phase 7**: Web UI (user-facing)
8. **Phase 8**: Testing (validation)
9. **Phase 9**: Documentation & release

### Optional / Can be deferred

- Phase 2b (WebSocket polling) → can add later for efficiency
- Phase 6 (Config sync) → can simplify initially (no auto-sync, manual config)
- Advanced allocation profiles (Phase 3 extension) → future

### Estimated Timeline

| Phase | Tasks | Est. Hours | Notes |
|-------|-------|-----------|-------|
| 0 | Config + data structures | 8–12 | Foundation |
| 1 | Discovery + peer API | 12–16 | REST endpoints |
| 2a | HTTP polling | 8–12 | Background task |
| 3 | Allocation algorithm | 12–16 | Core logic |
| 4 | Shaper integration | 6–10 | Integration with existing code |
| 5 | Failsafe | 8–12 | Safety-critical |
| 6 | Config sync (optional for MVP) | 12–16 | Deferred or simplified |
| 7 | Web UI | 16–24 | Svelte components |
| 8 | divert_sim test infrastructure | 24–32 | OpenEVSE_Emulator integration, comprehensive test suite |
| 9 | Docs & release | 8–12 | Release prep |
| **Total** | | **114–162 hours** | ~3–4 weeks at 8h/day |

---

## Testing Strategy

### Overview
All automated testing is performed via the **divert_sim** native build framework, extended with OpenEVSE_Emulator for multi-peer simulation. This approach avoids the complexity of unit testing ESP32 firmware while providing comprehensive validation.

### Testing Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     divert_sim (native build)                │
│  ┌─────────────────────────────────────────────────────┐    │
│  │         Load Sharing Test Harness (Python)          │    │
│  │  - Orchestrates multi-peer scenarios                │    │
│  │  - Controls peer lifecycle                          │    │
│  │  - Simulates network conditions                     │    │
│  └────────────┬────────────────────────────────────────┘    │
│               │                                              │
│  ┌────────────┴─────────────────────────────────────────┐   │
│  │         OpenEVSE_Emulator Integration Layer          │   │
│  │  - EmulatorPeer wrapper class                        │   │
│  │  - Mock WiFi endpoints (/loadsharing/*, /config)    │   │
│  │  - WebSocket /ws simulation                          │   │
│  └─┬──────────┬──────────┬──────────┬──────────────────┘   │
│    │          │          │          │                       │
│  ┌─┴─┐      ┌─┴─┐      ┌─┴─┐      ┌─┴─┐                    │
│  │P1 │      │P2 │      │P3 │      │P4 │  (Emulator Peers)  │
│  │EV │      │EV │      │EV │      │EV │  (with EVSE+EV)    │
│  └───┘      └───┘      └───┘      └───┘                    │
└─────────────────────────────────────────────────────────────┘
```

### Test Levels

1. **Allocation Algorithm Tests** (Phase 8.3)
   - Pure logic validation with synthetic peer configurations
   - Deterministic allocation verification
   - Edge case coverage (zero peers, insufficient capacity, offline peers)
   - Conservative accounting validation

2. **Failsafe Behavior Tests** (Phase 8.4)
   - Strict mode (disable on peer loss)
   - Graceful mode (safe current fallback)
   - Heartbeat timeout verification
   - Peer recovery testing

3. **Configuration Sync Tests** (Phase 8.5)
   - Mismatch detection
   - Conservative fallback behavior
   - Automatic sync propagation
   - Conflict resolution

4. **End-to-End Integration Tests** (Phase 8.6)
   - Full workflow: discovery → configuration → allocation → failsafe → recovery
   - Multi-peer scenarios (3-8 peers)
   - Network failure simulation
   - Config sync during operation

5. **Performance & Stress Tests** (Phase 8.7)
   - 8-peer maximum group size
   - High-frequency status updates
   - Network instability
   - Config sync storms

### Testing Without Hardware

- **Native build executable**: Compile entire ESP32 firmware for native Linux/Mac execution
- **OpenEVSE_Emulator**: Python-based RAPI + HTTP + WebSocket simulation
- **Mock network layer**: Simulate HTTP requests, WebSocket connections, mDNS discovery
- **Time control**: Fast-forward simulated time for heartbeat timeout tests
- **Deterministic execution**: No race conditions, reproducible results

### Key Testing Features

- **Full EVSE/EV simulation**: Each peer has realistic charging behavior
- **Network condition control**: Inject latency, packet loss, disconnections
- **State inspection**: Query internal state (allocations, failsafe status, config) at any time
- **Automated assertions**: Verify safety properties (never exceed group limit)
- **Test reporting**: Generate comprehensive test report with pass/fail status

### Continuous Integration

- **CI pipeline**: Run all load sharing tests on each PR
- **Test duration**: Complete test suite runs in ~5-10 minutes (native build is fast)
- **Artifacts**: Test reports archived for debugging
- **Coverage tracking**: Monitor test scenario coverage over time

### Manual Testing (Optional)

While automated testing covers all scenarios, manual testing with real hardware can validate:
- Multiple ESP32 devices on same network
- Real WiFi network conditions
- Physical EVSE/EV behavior
- Web UI usability

However, **manual hardware testing is not required for PR approval** since divert_sim provides comprehensive validation.

---

## Open Questions / Decisions Needed

1. **Phase 2b (WebSocket Client)**: ✅ RESOLVED - `MongooseWebSocketClient` implemented in ArduinoMongoose `websocket_client` branch
   - Wraps mongoose `mg_connect()` with HTTP upgrade handshake for WebSocket
   - Callbacks: `setReceiveTXTcallback()`, `setOnOpen()`, `setOnClose()`
   - Supports PING/PONG keepalive, configurable stale timeout, automatic reconnection
   - Used by `LoadSharingPeerPoller` for all peer WebSocket connections

2. **Phase 6 (Config Sync)**: Should this be automatic or manual for MVP? (Recommend: optional/manual for v1)

3. **Authentication**: Do peers need to authenticate each other? (Assumed: trusted LAN for MVP)

4. **Multiple groups**: Can a node be in multiple groups? (Assumed: single group per node for MVP; multi-group is future)

5. **Web UI location**: Where should load sharing config live? (Recommend: new "Load Sharing" tab in settings)

6. **Hardware testing**: Can feature be tested with multiple ESP32s on same network? (Recommend: yes, but not required for initial PR)

---

## Comprehensive Testing Strategy

### Phase-by-Phase Testing Plan

**Phase 1: Discovery & Peer Management** ✅ COMPLETED
- **Unit Tests** (conftest.py fixtures):
  - Port offset uniqueness (14 tests)
  - PTY bridge creation & cleanup
  - Peer hostname factory (test-offset based)
- **Integration Tests** (test_loadsharing_peer_management.py):
  - Peer discovery via mDNS (2/3/4 instances)
  - REST API endpoints (GET/POST/DELETE /loadsharing/peers)
  - Response structure validation
  - Duplicate rejection & 404 handling
- **Coverage**: 14 tests, 339.88s runtime, 100% passing
- **Scope**: All Phase 1 endpoints working end-to-end

**Phase 2: Peer Status Ingestion** ✅ COMPLETED
- **Integration Tests** (`tests/integration/test_loadsharing_peer_status.py`):
  - GET /loadsharing/status endpoint validation (structure, fields)
  - Native /status endpoint has required load sharing fields (amp, voltage, pilot, state, vehicle)
  - Native /ws WebSocket sends initial status on connect
  - Peer status ingestion after adding peer (HTTP bootstrap + WebSocket subscription)
  - Multi-peer (2/3/4) concurrent status tracking (parametrized)
  - Response structure compliance with api.yml schema
- **Test Scenarios**:
  - Single peer status ingestion (HTTP → WebSocket → cache)
  - Multi-peer (2/3/4) simultaneous status tracking
  - WebSocket initial message contains expected status fields
  - Nested peer status object in /loadsharing/status response
- **Fixtures Used**: `instance_pair_auto`, `multi_instance_group`, `unique_port_offset`
- **Coverage**: 10+ tests covering HTTP bootstrap, WebSocket, status cache, response structure
- **Expected Runtime**: ~120-180s

**Phase 3: Allocation & Dispatch** (PLANNED)
- **Unit Tests**:
  - Allocation algorithm with synthetic peer configs
  - Edge cases: zero peers, zero capacity, all offline
  - Deterministic ordering (sorted by priority)
  - Current limiting & rounding
- **Integration Tests**:
  - Allocation result published to `/allocations` endpoint
  - Peer current limit updated upon state change
  - Failsafe triggering on allocation failure
- **Test Scenarios**:
  - 2-peer equal split
  - 3-peer unequal capacity
  - Priority-based allocation
  - Current rounding edge cases
  - Insufficient capacity for all peers
- **Expected Runtime**: ~60s

**Phase 4: Failsafe & Error Recovery** (PLANNED)
- **Integration Tests**:
  - Strict mode: disable on any peer loss
  - Graceful mode: safe current fallback
  - Recovery when peer comes back online
  - Config mismatch detection & fallback
- **Test Scenarios**:
  - Single peer disconnect (detect via heartbeat timeout)
  - Multiple peer simultaneous disconnect
  - Peer reconnection after timeout
  - Config hash mismatch (trigger conservative mode)
- **Simulations**:
  - Kill peer process → detect in 35s
  - Pause peer (no status updates) → heartbeat timeout
  - Modify peer config → hash mismatch detection
- **Expected Runtime**: ~200s (includes timeout waits)

**Phase 5: Web UI/Config UI** (PLANNED)
- **UI Snapshot Tests**:
  - Group configuration dialog rendering
  - Peer list display with online/offline status
  - Allocation display (current per peer)
  - Failsafe indicator
- **Manual Testing** (recommended):
  - Add peer via discovery
  - Edit group max current
  - Observe allocation updates
  - Trigger failsafe & recovery
- **Automated Tests**:
  - Configuration API validation
  - Config persistence across restart
  - Invalid config rejection

**Phase 6: Config Sync** (PLANNED)
- **Integration Tests**:
  - Config version increment on change
  - Config hash computation & embedding in WebSocket
  - Peer config mismatch detection
  - Automatic sync via WebSocket
- **Test Scenarios**:
  - One peer modifies group max current
  - Detect mismatch on other peers
  - Sync mechanism propagates change
  - Verification peers now have matching config
- **Message Injection**:
  - Mock peer with old config version
  - Send status with mismatched hash
  - Verify conservative behavior triggered
- **Expected Runtime**: ~80s

**Phase 7: State Machine & Transitions** (PLANNED)
- **Unit Tests**:
  - State transition table validation
  - Invalid transition rejection
  - State-specific behavior correctness
- **Integration Tests**:
  - Full state machine workout (all transitions)
  - Error recovery paths
  - Initialization sequence
  - Shutdown sequence
- **Test Scenarios**:
  - Disabled → Discovering → Ready → Allocating → Active
  - Active → Offline (peer loss) → Failsafe
  - Failsafe → Recovering → Active
  - Config mismatch → Conservative → Synced
- **Expected Runtime**: ~120s

**Phase 8: End-to-End & Stress Tests** (PLANNED)
- **Full Workflow Tests**:
  - 8-peer concurrent operation
  - Configuration changes during operation
  - Progressive peer addition (start with 2, add to 8)
  - Progressive peer removal (8 → 2)
- **Stress Tests**:
  - High-frequency status updates (100/sec)
  - Network instability (latency, jitter, packet loss)
  - Config sync storms (multiple config changes)
  - Extended operation (5+ minutes)
- **Failure Scenarios**:
  - Cascading peer failures
  - Peer recovery during allocation
  - Config conflicts + simultaneous failures
- **Fixtures to Add**:
  - `network_chaos()` - inject latency/loss
  - `rapid_config_changes()` - stress config sync
  - `peer_lifecycle()` - add/remove peers over time
- **Expected Runtime**: 600s+ (10+ minutes for realistic scenarios)

### Test Infrastructure Recommendations

1. **Continue Docker + socat approach** (Phase 1 proven working)
   - Reuse `instance_pair()` fixture for all integration tests
   - Extend parametrization (2/3/4/8 instances)
   - Add network chaos injection via `tc` (traffic control)

2. **Add Python-based simulation layer** for Phase 8
   - Mock HTTP/WebSocket clients
   - Time acceleration (10x) for heartbeat timeouts
   - Deterministic failure injection

3. **Measurement instrumentation**
   - Track allocation computation time (ms)
   - Measure WebSocket message latency
   - Monitor memory usage with 8 peers
   - Log state machine transitions for post-analysis

4. **Test data management**
   - Store test results in `.../tests/integration/output/`
   - Generate allure reports for trending
   - Archive failing test logs with timestamps

### Success Criteria for Full Test Suite

- ✅ Phase 1: 14 integration tests passing (COMPLETE)
- ✅ Phase 2: 15+ tests covering WebSocket, polling, timeouts
- ✅ Phase 3: 10+ allocation algorithm tests + 5+ integration scenarios
- ✅ Phase 4: Failsafe + recovery tests (strict & graceful modes)
- ✅ Phase 5-7: Cumulative coverage 50+ integration tests
- ✅ Phase 8: 10+ end-to-end scenarios + 5 stress tests
- **Total Target**: 100+ integration tests, all green before Phase 1 PR merge

---

## Next Steps

1. **Assign Phase 0 tasks** to establish foundation
2. **Review this plan** with team; prioritize any changes
3. **Create GitHub issue** linking to this plan
4. **Begin Phase 0 development**
5. **Set up test harness** early (divert_sim extensions)
6. **Iterate in 2–3 week sprints** with testing at each phase

