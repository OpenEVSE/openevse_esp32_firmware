# Load Sharing Implementation Plan

This document breaks down the load-sharing feature into concrete implementation phases, with dependencies and estimated scope per phase.

**Current Status**: Phase 2 (Peer Status Ingestion) ✅ COMPLETED

* Phase 0 (Foundation & Data Model) ✅ fully complete
* Phase 1 (Discovery & Peer Management) ✅ fully complete (14 integration tests passing)
* Phase 2 implementation complete:
  * WebSocket client connections to peers via `MongooseWebSocketClient` (ArduinoMongoose `websocket_client` branch)
  * HTTP GET `/status` bootstrap for initial peer status cache population
  * Background `LoadSharingPeerPoller` MicroTasks task with connection state machine
  * Per-peer connection tracking: DISCONNECTED → HTTP\_FETCHING → WS\_CONNECTING → WS\_CONNECTED
  * Exponential backoff reconnection (1s base, 60s cap, 5 max retries)
  * Heartbeat monitoring (120s default timeout, configurable)
  * Status cache with delta updates from WebSocket messages (amp, voltage, pilot, vehicle, state)
  * `/loadsharing/status` endpoint returns per-peer status from poller cache
* Ready for Phase 3 (Controller/Member Roles & Config Push)
  **Architecture Change (March 2026)**: Shifted from fully distributed
  (every node independently computes allocations) to a **controller/member
  model** for the MVP. See `docs/load-sharing.md` for design rationale.

## Project Overview

**Goal**: Implement local load sharing for OpenEVSE ESP32 WiFi firmware, allowing multiple chargers to share a single circuit breaker limit without cloud dependency. One device acts as the **controller** (configured by the user), computes allocations, and pushes current limits to **member** devices.
**Key Constraints**:

* Controller/member architecture for MVP (distributed sync deferred to future)
* Controller computes allocations and pushes to members via WebSocket
* Members apply allocations via existing Current Shaper claim system
* Safety-first failsafe leveraging existing Current Shaper timeout (120s)
* Reuse existing primitives (mDNS, WebSocket, Current Shaper claims)

**Success Criteria**:

* 2–8 chargers can be configured in a group from a single controller
* Controller computes and pushes allocations to all members
* Members' claims expire safely if controller goes offline (120s timeout)
* Web UI on controller allows group discovery, configuration, and monitoring
* Web UI on members shows read-only status

***

## Phase 0: Foundation & Data Model

### Objective

Establish configuration schema, persistence, and basic infrastructure for load sharing.

### Tasks

#### 0.1: Define LoadSharing config schema

* **Description**: Add configuration options to `ConfigJson` schema
* **Files to modify**:
  * `src/app_config.cpp` - implement config accessor/setter methods
  * `models/Config.yaml` - already contains the schema definition (see below)
* **Config fields** (already defined in Config.yaml):
  * `loadsharing_enabled: bool` (default: false)
  * `loadsharing_group_id: string` (user-defined group identifier; empty when disabled)
  * `loadsharing_group_max_current: number` (amps; total circuit limit)
  * `loadsharing_safety_factor: number` (default: 1.0; range 0–1; derating factor)
  * `loadsharing_heartbeat_timeout: int` (seconds; default 30; minimum 5)
  * `loadsharing_failsafe_mode: enum` ("disable" or "safe\_current"; default "safe\_current")
  * `loadsharing_failsafe_safe_current: number` (amps; default 6.0; used in safe\_current mode)
  * `loadsharing_failsafe_peer_assumed_current: number` (amps; default 6.0; conservative offline peer current)
  * `loadsharing_priority: int` (node's priority; lower = higher; default 0; NOT synced)
  * `loadsharing_config_version: int` (readOnly; monotonic counter for config sync; added in Phase 6)
  * `loadsharing_config_updated_at: int` (readOnly; Unix timestamp; added in Phase 6)
* **Note**: Members list is managed separately via `/loadsharing/peers` API (Phase 1)
* **Dependencies**: ConfigJson library (already available)
* **Testing**: Validate via Phase 8 divert\_sim integration tests

#### 0.2: Create LoadSharing data structures (C++)

* **Description**: Define in-memory models for peers, group state, allocations; persist peer list
* **Files to create**:
  * `src/loadsharing_types.h` - data structures
    * `class LoadSharingPeer` - hostname, IP, device\_id, online status,
    * `class LoadSharingGroup` - config + runtime state
    * `class LoadSharingPeerStatus` - cached peer status (current, voltage, vehicle connected, etc.)
    * `class LoadSharingAllocationResult` - per-peer allocation computation results
  * `src/loadsharing_state.h/cpp` - state manager
    * Maintain in-memory peer list, status cache, allocation results
    * Thread-safe access (use existing spinlock/mutex patterns)
    * Persist peer member list to SPIFFS (simple JSON array of hostnames)
    * Load peer list on startup; fall back to empty list if missing
* **Dependencies**: None (config persistence already handled by ConfigJson)
* **Testing**: Validate via Phase 8 divert\_sim integration tests

***

## Phase 1: Discovery & Peer Management

### Objective

Implement mDNS-based discovery and peer list management via REST API.

### Tasks

#### 1.1: Wrap existing mDNS discovery

* **Status**: ✅ COMPLETED
* **Description**: Create helper to query mDNS for OpenEVSE peers with async ESP-IDF support
* **Files created**:
  * `src/loadsharing_discovery.h/cpp` - discovery wrapper with caching
  * **Implementation**:
  * `LoadSharingDiscovery` class wraps mDNS service discovery
    * Query `_openevse._tcp` service via `MDNS.queryService()`
    * Cache results with TTL (default 60 seconds)
    * Auto-refresh cache when TTL expires
    * Global `loadSharingDiscovery` instance
  * **EpoxymDNS Enhancements**:
  * Added async API declarations to EpoxymDNS.h for ESP32-only async queries:
    * `mdns_search_once_t* mdns_query_async_new()` - initiate non-blocking query
      * `bool mdns_query_async_get_results()` - poll for results
      * `void mdns_query_async_delete()` - cleanup query handle
      * `void mdns_query_results_free()` - free result structures
    * These wrap ESP-IDF's mdns\_query\_async\_\* functions directly
    * Allows background discovery without blocking HTTP requests (used in Phase 1.6)
    * Forward declarations for `mdns_search_once_t`, `mdns_result_t`, `mdns_txt_item_t` (ESP32-only)
    * Native builds have conditional compilation stubs
  * **Dependencies**: ESPmDNS (already used in firmware), EpoxymDNS (wrapper library)
* **Testing**: ✅ Compiles successfully on native build (2.97s)

#### 1.2: Implement `/loadsharing/peers` GET endpoint

* **Status**: ✅ COMPLETED
* **Files created/modified**:
  * `src/web_server_loadsharing.cpp` - all load sharing endpoints
  * **Implementation**:
  * Returns combined list of discovered peers (with online status) and configured offline peers (joined=true)
    * Deduplicates results by hostname
    * Tracks which discovered peers are joined to the configured group
  * **Response** (array of LoadSharingPeer with `joined` field):
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
  * **Key Features**:
  * `online: boolean` - current discovery status (from mDNS)
    * `joined: boolean` - whether peer is in configured group (manually added via POST /loadsharing/peers)
    * Shows both discovered and offline configured peers
    * Deduplication removes duplicate hostnames from multiple network interfaces
  * **Dependencies**: Phase 0 (config), Phase 1.1 (discovery), Phase 1.6 (background task)
* **Testing**: ✅ Verified with native build compilation and HTTP endpoint testing

#### 1.3: Implement `/loadsharing/peers` POST endpoint (add peer)

* **Status**: ✅ COMPLETED
* **Description**: Add a peer to the configured group
* **Files modified**: `src/web_server_loadsharing.cpp`
* **Implementation**:
  * Validates input (not duplicate, not self, resolvable host format)
    * Adds peer hostname to in-memory `configuredPeers` vector
    * Deduplicates entries
    * Returns success or 400 error with validation message
  * **Request body**:
  ```json
    {
        "host": "openevse-2.local"
    }
  ```
  * **Response**: `{"msg":"done"}` with status code 200
* **Validation**:
  * Reject duplicate hosts: "Peer already configured"
    * Validate host format (must contain '.' or ':' for domain/IP)
    * Reject empty host
  * **Side Effects**:
  * Peer appears in GET /loadsharing/peers with `joined: true`
    * Peer is considered part of group even if not currently discovered
  * **Dependencies**: Phase 1.1, 1.2, 1.6
* **Build Status**: ✅ Compiles successfully
* **Testing**: ✅ Validated with HTTP test requests

#### 1.4: Implement `/loadsharing/peers` DELETE endpoint

* **Status**: ✅ COMPLETED
* **Description**: Remove a peer from configured group
* **Files modified**: `src/web_server_loadsharing.cpp`
* **Implementation**:
  * Extracts hostname from URL path parameter `/loadsharing/peers/{host}`
    * Removes peer from `configuredPeers` vector
    * Returns success or 404 if peer not found
    * Handles both hostname and IP address formats
  * **URL**: `/loadsharing/peers/{host}` (URL-encoded hostname)
* **Response**: `{"msg":"done"}` with status 200, or `{"msg":"Peer not found"}` with status 404
* **Side effects**:
  * Peer no longer appears in GET /loadsharing/peers (if not discovered)
    * Discovered peer still appears but with `joined: false`
  * **Dependencies**: Phase 1.2
* **Build Status**: ✅ Compiles successfully
* **Testing**: ✅ Validated with HTTP test requests

#### 1.5: Implement `/loadsharing/discover` POST endpoint

* **Status**: ✅ COMPLETED
* **Description**: Trigger immediate mDNS discovery (on-demand refresh)
* **Files modified**: `src/web_server_loadsharing.cpp`
* **Implementation**:
  * Calls `loadSharingDiscoveryTask.triggerDiscovery()` to reset timer
    * Forces discovery on next task wake (within 2 seconds)
    * Returns success immediately
    * Non-blocking: returns before query completes
  * **URL**: `POST /loadsharing/discover`
* **Response**: `{"msg":"done"}` with status 200
* **Behavior**:
  * Discovery runs asynchronously in background
    * GET /loadsharing/peers will reflect new results when cache is updated
  * **Dependencies**: Phase 1.1, 1.6 (background task)
* **Build Status**: ✅ Compiles successfully
* **Testing**: ✅ HTTP endpoint confirmed working

#### 1.6: Add background discovery task

* **Status**: ✅ COMPLETED
* **Description**: Run continuous asynchronous peer discovery in background using MicroTasks scheduler
* **Files created**:
  * `src/loadsharing_discovery_task.h/cpp` - unified background task implementation combining discovery logic + MicroTasks scheduling
  * **Implementation**:
  * Unified `LoadSharingDiscoveryTask` class implements both discovery methods and MicroTasks::Task interface
    * Global singleton instance `loadSharingDiscoveryTask` (pattern consistent with other global tasks like timeManager, scheduler)
    * Runs in MicroTasks background scheduler while firmware executes normally
    * Periodic query every 60-120 seconds (configurable via config, default 60 seconds)
    * Non-blocking: HTTP handlers never wait for mDNS results
    * Results cached with TTL (default 60 seconds)
    * Thread-safe cache access
    * POST /loadsharing/discover resets timer to force immediate discovery
  * **Key Methods**:
  * `discoverPeers()` - performs mDNS query for `_openevse._tcp` services, caches results
    * `getCachedPeers()` - returns last discovered results immediately without blocking
    * `triggerDiscovery()` - forces discovery on next task wake
    * `isCacheValid()`, `invalidateCache()`, `cacheTimeRemaining()` - cache management
    * `setup()`, `loop()`, `begin()`, `end()` - MicroTasks lifecycle
  * **Deduplication**:
  * Detects duplicate hostnames from mDNS results (same device on multiple network interfaces)
    * Tracks seen hostnames, skips duplicates, returns only unique peers per hostname
    * Debug logging shows number of results deduplicated
  * **Behavior**:
  * Task wakes every 2 seconds to check if discovery should start
    * When cache TTL expires: initiates new mDNS query
    * Query runs asynchronously; GET /loadsharing/peers always returns cached results immediately
    * No blocking of HTTP request handling
    * Manual discovery (POST /loadsharing/discover) resets timer for immediate refresh
  * **Build Status**: ✅ Compiles successfully (3.2 seconds native build)
* **Dependencies**: Phase 1.1 (EpoxymDNS discovery), MicroTasks
* **Testing**: Verified with native build compilation

#### 1.7: Persist configured peers to SPIFFS

* **Status**: ✅ COMPLETED
* **Description**: Save and load configured peer group to persistent storage so peers survive device reboots
* **Files modified**:
  * `src/loadsharing_discovery_task.h/cpp` - storage integrated directly into discovery task (no separate storage file needed)
  * **Implementation**:
  * Storage methods added to `LoadSharingDiscoveryTask` class:
    * `loadGroupPeers()` - loads peer list from LittleFS on task startup
      * `saveGroupPeers()` - saves peer list to LittleFS (conditional on dirty flag)
    * Storage location: `/loadsharing_peers.json` in LittleFS root
    * Storage format: JSON array of hostnames
      ```json
        {
            "peers": [
                "openevse-1.local",
        "openevse-2.local",
        "192.168.1.100"
        ]
      }
      ```
    ```
      - **Atomic writes**: Write to `/loadsharing_peers.json.tmp` first, then rename to prevent corruption on power loss
    - On load failure (file missing): logs warning and starts with empty list (graceful degradation)
    - On JSON parse error: logs error and starts with empty list
    - Dirty flag (`_groupPeersDirty`) tracks when saves are needed to avoid unnecessary writes
    ```
  * **Integration**:
  * Internal storage using `std::vector<String> _groupPeers` in discovery task
    * `LoadSharingDiscoveryTask::begin()` calls `loadGroupPeers()` on startup
    * `LoadSharingDiscoveryTask::addGroupPeer()` sets dirty flag and calls `saveGroupPeers()` immediately
    * `LoadSharingDiscoveryTask::removeGroupPeer()` sets dirty flag and calls `saveGroupPeers()` immediately
    * HTTP endpoints (`POST /loadsharing/peers`, `DELETE /loadsharing/peers/{host}`) trigger saves via discovery task methods
  * **Migration**:
  * First boot with no storage file: logs "No persisted group peer list found, starting with empty list"
    * Upgrade from earlier version: automatically creates storage file on first peer add
    * No migration needed - clean slate on first deploy
  * **Key Features**:
  * Atomic write-rename pattern prevents corruption
    * Immediate persistence on every add/remove operation
    * Dirty flag optimization avoids redundant writes
    * Graceful degradation on corrupted/missing file
    * Debug logging for all load/save operations
  * **Dependencies**: Phase 1.2, 1.3, 1.4 (peer management API), Phase 1.6 (discovery task)
* **Build Status**: ✅ Compiles successfully on native build
* **Testing**: ✅ Storage methods integrated and callable via REST API

### Phase 1 Integration Test Suite

**Status**: ✅ COMPLETED - 14 tests, 100% passing (339.88s runtime)
**Location**: `tests/integration/test_loadsharing_peer_management.py` (336 lines)
**Test Infrastructure**:

* **Architecture**: Pytest + Docker + socat
  * Each test spawns paired instances: Docker emulator + native firmware binary
    * TCP-to-PTY bridge (socat) connects emulator's TCP RAPI port to host PTY
    * Solves namespace isolation: each test gets unique real PTYs (`/dev/pts/18`, `/dev/pts/19`, etc.)
    * Isolated persistent storage: each native firmware instance runs in unique temp directory
  * **Fixtures** (`conftest.py`, 527 lines):
  * `pytest_configure()` - session initialization with port offset counter
    * `unique_port_offset()` - sequential assignment (0-9) per test
    * `peer_hostname_factory()` - unique hostnames per test offset (prevents NVS persistence collisions)
    * `instance_pair()` - factory for spawning paired emulator+native with auto-cleanup
    * `instance_pair_auto()` - convenience wrapper with auto-assigned unique ports
    * `check_mdns_support()` - mDNS validation + pre-cleanup stuck resources
    * Docker/image management, HTTP readiness polling (30s timeout), aggressive port cleanup (fuser, socat kill)

**Test Coverage** (14 tests):
*Core Peer Management* (9 tests):

* `test_peers_endpoint_initial_state` - GET /loadsharing/peers returns correct structure
* `test_discover_trigger` - POST /loadsharing/discover returns 200 OK
* `test_peer_discovery_mdns[2/3/4]` - mDNS discovery with 2, 3, and 4 simultaneous instances (parametrized)
* `test_add_peer_manual` - POST /loadsharing/peers adds peer with `joined=true`
* `test_add_peer_duplicate_rejection` - POST duplicate peer returns 400
* `test_delete_peer` - DELETE /loadsharing/peers/{host} removes joined status
* `test_delete_nonexistent_peer` - DELETE nonexistent peer returns 404
* `test_discovered_peers_joined_status[2/3/4]` - Verify discovered peers have correct `joined` status (parametrized)
  *Response Structure* (2 tests):
* `test_peers_response_structure` - Validate JSON array with required fields (id, name, host, joined, ip, online)
* `test_error_response_structure` - Verify error responses include `msg` or `error` field
  **Prerequisites**:
* Docker (emulator image)
* socat (TCP-to-PTY bridge)
* Avahi/mDNS (discovery validation)
* Native firmware build (`.pio/build/native/program`)
* Python 3.7+ with pytest, docker, requests libraries
  **CI/CD Integration**:
* GitHub Actions workflow: `.github/workflows/integration_tests.yaml`
* Triggers: after build.yaml completes OR manual workflow\_dispatch
* Matrix: parametrizes test runs with 2/3/4 instance counts
* Artifact handling: downloads native binary from build workflow
* Result publishing: publishes test report to PR
  **Known Limitations & Workarounds**:

1. Docker/PTY namespace isolation: Solved with socat TCP-to-PTY bridge
2. NVS persistence collisions: Solved with isolated temp directories per instance
3. Port conflicts between tests: Solved with sequential counter-based port assignment
4. mDNS discovery timing: Tests wait up to 30s for peer discovery
5. Cannot parallelize tests: All use fixed port ranges (8000-8003, 8080-8083)

***

## Phase 2: Peer Status Ingestion

### Objective

Collect real-time status from peer OpenEVSE devices via initial HTTP request followed by persistent WebSocket subscriptions.
**Current Status**: ✅ COMPLETED

* All 3 sub-tasks implemented in `src/loadsharing_peer_poller.h/cpp` (295+541 lines)
* HTTP bootstrap, WebSocket client, background task all functional
* `/loadsharing/status` endpoint returns peer status from poller cache
* Integration tests created in `tests/integration/test_loadsharing_peer_status.py`

### Tasks

#### 2.1: Implement WebSocket client for peer `/ws`

* **Status**: ✅ COMPLETED
* **Description**: Maintain persistent WebSocket connection to each peer's `/ws` endpoint for real-time status and config sync metadata
* **Files created**:
  * `src/loadsharing_peer_poller.h` - `PeerConnection` struct with `MongooseWebSocketClient*`, `LoadSharingPeerStatus` cache, connection state tracking
    * `src/loadsharing_peer_poller.cpp` - `startWebSocketConnection()`, `handleWebSocketMessage()`, `checkWebSocketConnection()`
  * **WebSocket Payload for Load Sharing**:
  * The peer's `/ws` endpoint sends status updates that include (in addition to standard status fields):
    * `config_version` (int): current config version (future use)
      * `config_hash` (string): hash of critical group params (future use)
      * Standard fields: `amp`, `voltage`, `state`, `pilot`, `vehicle`
    * **Initial message** (on connect): Full status snapshot via `buildStatus()` (same as GET /status)
    * **Subsequent messages**: Delta updates (only changed fields) via `web_server_event()`
  * **Implementation**:
  * For each peer in the group, establish `ws://{peer_host}/ws` connection
    * `MongooseWebSocketClient` from ArduinoMongoose (`websocket_client` branch) wraps mongoose `mg_connect()` with HTTP upgrade
    * Callbacks registered: `setReceiveTXTcallback()` for message parsing, `setOnOpen()` for connection tracking, `setOnClose()` for failure handling
    * JSON messages parsed with ArduinoJson `DynamicJsonDocument(4096)`
    * Delta merge strategy: each field in message payload overwrites corresponding cache value
    * `last_seen` timestamp updated on every successful message receipt
    * `retryCount` reset to 0 on successful connection
    * PING/PONG keepalive interval: 15s (configurable via `setWsPingInterval()`)
    * Stale connection timeout: 30s (configurable via `setWsStaleTimeout()`)
  * **Dependencies**: MongooseWebSocketClient (ArduinoMongoose websocket\_client branch), Phase 1 (peer list)
* **Build Status**: ✅ Compiles successfully on native build
* **Testing**: ✅ Validated via Phase 2 integration tests with paired native firmware instances

#### 2.2: Implement initial HTTP GET request for peer `/status`

* **Status**: ✅ COMPLETED
* **Description**: Get initial status snapshot before opening WebSocket as bootstrap/fallback
* **Files created/modified**:
  * `src/loadsharing_peer_poller.cpp` - `startHttpBootstrap()` method
  * **Implementation**:
  * Before opening WebSocket for a peer, issues `GET http://{peer_host}/status` via `MongooseHttpClient`
    * Async HTTP request with `onResponse()` and `onClose()` callbacks
    * Parses JSON response and populates `statusCache` with: amp, voltage, pilot, vehicle, state, config\_version, config\_hash
    * On HTTP success (200): transitions to `WS_CONNECTING` state, sets `hasInitialStatus=true`
    * On HTTP failure: transitions to `HTTP_FAILED` state, increments `retryCount`
    * On HTTP timeout (10s default): aborts request, transitions to `HTTP_FAILED`
    * Once WebSocket connects, WebSocket becomes primary status source
    * HTTP timeout configurable: `setHttpTimeout(ms)`
  * **Key Fields Extracted from `/status` Response**:
  * `amp` - current draw (milliamps scaled)
    * `voltage` - supply voltage
    * `state` - EVSE state (J1772)
    * `pilot` - pilot current setpoint
    * `vehicle` - vehicle connection status (0/1)
    * `config_version` - tracked in cache (future use)
    * `config_hash` - tracked in cache (future use)
  * **Dependencies**: MongooseHttpClient (ArduinoMongoose), Phase 2.1
* **Build Status**: ✅ Compiles successfully
* **Testing**: ✅ Validated via Phase 2 integration tests

#### 2.3: Background WebSocket management task

* **Status**: ✅ COMPLETED
* **Description**: Monitor and manage WebSocket connections for all peers; update peer online/offline status based on heartbeat
* **Files created**:
  * `src/loadsharing_peer_poller.h` - `LoadSharingPeerPoller` class extending `MicroTasks::Task`
    * `src/loadsharing_peer_poller.cpp` - full implementation (541 lines)
  * **Implementation**:
  * `LoadSharingPeerPoller` runs as MicroTasks background task, wakes every 500ms (configurable)
    * Global instance: `loadSharingPeerPoller`
    * `begin(LoadSharingGroupState& groupState)` - registers with MicroTasks scheduler
    * `syncPeerList()` - synchronizes `_connections` map with authoritative peer list from `LoadSharingGroupState`:
      * Adds new peers (state: DISCONNECTED)
        * Removes deleted peers (disconnects WebSocket, cleans up)
        * Preserves existing connection state for unchanged peers
      * **Connection State Machine per Peer** (implemented in `processPeerConnection()`):
      ````
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
      ````
    * **Configuration Methods** (all with sensible defaults):
  * `setPollInterval(ms)` - task wake interval (default 500ms)
    * `setHeartbeatTimeout(ms)` - mark offline threshold (default 120000ms)
    * `setBaseRetryInterval(ms)` - exponential backoff base (default 1000ms)
    * `setMaxRetryInterval(ms)` - exponential backoff cap (default 60000ms)
    * `setHttpTimeout(ms)` - HTTP request timeout (default 10000ms)
    * `setWsStaleTimeout(ms)` - WebSocket stale timeout (default 30000ms)
    * `setWsPingInterval(ms)` - WebSocket PING interval (default 15000ms)
    * `setMaxRetryCount(count)` - max retries before persistent offline (default 5)
  * **Dependencies**: Phase 2.1, 2.2, MicroTasks, LoadSharingGroupState
* **Build Status**: ✅ Compiles successfully on native build
* **Testing**: ✅ Validated via Phase 2 integration tests

#### 2.4: `/loadsharing/status` endpoint with peer status

* **Status**: ✅ COMPLETED
* **Description**: Expose peer status data from poller cache via REST API
* **Files modified**: `src/web_server_loadsharing.cpp` - `handleLoadSharingStatus()`
* **Implementation**:
  * Returns JSON with: `enabled`, `group_id`, `computed_at`, `failsafe_active`, `online_count`, `offline_count`
    * `peers` array includes nested `status` object from peer poller cache:
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
    ```
      - Queries `loadSharingPeerPoller.getPeerStatus()` for each peer
    - Only includes `status` object if poller has cached status for that peer
    ```
  * **Dependencies**: Phase 2.1-2.3, Phase 1 (peer list)
* **Build Status**: ✅ Compiles successfully
* **Testing**: ✅ Validated via Phase 2 integration tests

### Phase 2 Integration Test Suite

**Status**: ✅ COMPLETED
**Location**: `tests/integration/test_loadsharing_peer_status.py`
**Test Coverage** (planned):
*Core Status Ingestion* (tests):

* `test_status_endpoint_returns_valid_json` - GET /loadsharing/status returns valid JSON structure
* `test_status_endpoint_fields` - Response includes required fields (enabled, peers, allocations)
* `test_native_status_endpoint_has_required_fields` - GET /status on peer returns amp, voltage, pilot, state, vehicle
* `test_native_websocket_sends_status` - WebSocket /ws on peer sends initial status on connect
* `test_peer_status_ingested_after_add[2/3/4]` - After adding peer, /loadsharing/status shows peer with status data (parametrized)
* `test_peer_status_contains_amp_pilot_state` - Ingested peer status contains amp, pilot, state fields
  *Peer Online/Offline Tracking* (tests):
* `test_peer_online_after_connection` - Peer marked online after successful connection
* `test_peer_status_multiple_peers[2/3/4]` - Multiple peers tracked simultaneously (parametrized)
  *Response Structure* (tests):
* `test_status_response_structure` - Validate LoadSharingStatus JSON structure per api.yml
* `test_peer_status_nested_structure` - Validate nested status object fields
  **Prerequisites**:
* Same as Phase 1 tests (Docker, socat, Avahi, native firmware build)
* Native firmware instances connect to each other's `/status` and `/ws` endpoints

***

## Phase 3: Controller/Member Roles & Config Push

### Objective

Add role designation (controller vs member), config push from controller to members, and lock out config writes on member devices.

### Tasks

#### 3.1: Add role config variables

* **Status**: ⏳ NOT STARTED
* **Description**: Add `loadsharing_role` and `loadsharing_controller_host` config fields
* **Files to modify**:
  * `src/app_config.h` - add extern declarations
  * `src/app_config.cpp` - add variable definitions and ConfigOptDefinition entries
  * `models/Config.yaml` - add schema entries
* **New config fields**:
  * `loadsharing_role: string` (enum: `""`, `"controller"`, `"member"`; default `""`)
  * `loadsharing_controller_host: string` (on members: controller's hostname/IP; default `""`)
* **Dependencies**: Phase 0 (existing config infrastructure)
* **Testing**: Validate via integration tests (Phase 8)

#### 3.2: Add role awareness to LoadSharingGroupState

* **Status**: ⏳ NOT STARTED
* **Description**: Add role field and helper methods to `LoadSharingGroupState`
* **Files to modify**:
  * `src/loadsharing_types.h` - add `_role` field, `isController()`, `isMember()`, `isEnabled()` methods
* **Implementation**:
  * Role enum or string: `""` (disabled), `"controller"`, `"member"`
  * `isController()` returns true if role is "controller"
  * `isMember()` returns true if role is "member"
  * Role transitions: `"" → controller` (on first peer add), `"" → member` (on receiving config from controller)
* **Dependencies**: Phase 3.1

#### 3.3: Lock out config writes on member devices

* **Status**: ⏳ NOT STARTED
* **Description**: When `loadsharing_role == "member"`, reject writes to loadsharing config fields
* **Files to modify**:
  * `src/web_server_loadsharing.cpp` - return 403 for POST/DELETE on `/loadsharing/peers`
  * `src/app_config.cpp` - reject POST to loadsharing config fields when role is member
* **Implementation**:
  * Check `loadSharingGroupState.isMember()` at start of POST/DELETE handlers
  * Return `{"msg":"Managed by controller"}` with HTTP 403
  * `/loadsharing/peers` GET and `/loadsharing/status` GET remain available on members
* **Dependencies**: Phase 3.2

#### 3.4: Implement config push from controller to members

* **Status**: ⏳ NOT STARTED
* **Description**: When the controller adds a peer and establishes a WebSocket connection, push loadsharing config to the member
* **Files to modify**:
  * `src/loadsharing_peer_poller.cpp` - add config push after WS\_CONNECTED state
  * `src/web_server_loadsharing.cpp` - trigger config push on peer add
* **Implementation**:
  * After peer poller reaches `WS_CONNECTED` for a new peer, issue `POST http://{member}/config` with:
    * `loadsharing_enabled: true`
    * `loadsharing_role: "member"`
    * `loadsharing_controller_host: {self_hostname}`
    * All group-level settings: `group_id`, `group_max_current`, `safety_factor`, `heartbeat_timeout`, `failsafe_mode`, `failsafe_safe_current`, `failsafe_peer_assumed_current`
  * On config change on the controller, re-push to all online members
  * On member removal (DELETE), push `loadsharing_enabled: false, loadsharing_role: ""` to reset member
* **Dependencies**: Phase 3.3, Phase 2 (peer poller with WS connections)

#### 3.5: Member accepts config from controller

* **Status**: ⏳ NOT STARTED
* **Description**: When a device receives a config POST that sets `loadsharing_role=member`, accept the loadsharing fields and transition to member role
* **Files to modify**:
  * `src/app_config.cpp` - config POST handler: detect role=member transition, accept loadsharing fields
* **Implementation**:
  * When POST `/config` includes `loadsharing_role: "member"`:
    * Accept all loadsharing fields from the request
    * Store `loadsharing_controller_host`
    * Transition to member role (further local loadsharing config changes rejected)
  * This is the mechanism by which the controller "enrolls" a member
* **Dependencies**: Phase 3.3

#### 3.6: Pause discovery on members once controller-connected

* **Status**: ⏳ NOT STARTED
* **Description**: Stop mDNS discovery on members once they have received config from a controller
* **Files to modify**:
  * `src/loadsharing_discovery_task.h/cpp` - add `pause()` / `resume()` methods
  * `src/main.cpp` - wire role transitions to discovery pause
* **Implementation**:
  * When device transitions to `role=member`: pause discovery task
  * When controller connection is lost (claim timeout): resume discovery task
  * Saves resources on member devices
* **Dependencies**: Phase 3.5

***

## Phase 4: Allocation Algorithm (Controller Only)

### Objective

Implement the allocation algorithm on the controller and push results to members via WebSocket.

### Tasks

#### 4.1: Implement allocation algorithm

* **Status**: ⏳ NOT STARTED
* **Description**: Core "Equal Share with Minimums" algorithm with conservative accounting for offline members
* **Files to create**:
  * `src/loadsharing_algorithm.h/cpp`
* **Implementation**:
  * Input: list of members (online/offline, demand status, min/max current, priority) from peer poller cache
  * Input: group max current, safety factor, failsafe settings
  * Output: per-member `LoadSharingAllocation` (target\_current + reason)
  * Algorithm steps:
    1. **Conservative offline accounting**: For each offline member, reserve `failsafe_peer_assumed_current` (config) as assumed consumption
    2. Compute `I_avail = group_max_current * safety_factor - sum(offline member reserves)`
    3. Filter demanding+online members (ignore offline members)
    4. If none demanding, return 0 for all
    5. If `I_avail >= sum(min_i for i in demanding)`: allocate min + equal share of remainder, capped by max\_i
    6. Else (insufficient): select deterministic subset (by sorted device\_id) until minimums fit; others get 0
    7. Return allocation map
  * Use deterministic ordering: sort members by device\_id (stable)
  * Handle edge cases (divide by zero, negative current, etc.)
* **Dependencies**: Phase 0 (data types), Phase 2 (peer status cache)
* **Testing**: Validate via Phase 8 allocation tests

#### 4.2: Trigger allocation recomputation on controller

* **Status**: ⏳ NOT STARTED
* **Description**: Re-compute allocations when peer status changes, peers go online/offline, or periodically
* **Files to modify**:
  * `src/loadsharing_peer_poller.cpp` - trigger recomputation on status update (controller only)
  * `src/loadsharing_types.h` - store allocation results in `LoadSharingGroupState`
* **Event flow**:
  1. WebSocket receives member status message → update status cache → call allocation recomputation
  2. Background task detects heartbeat timeout → mark member offline → call allocation recomputation
  3. Fallback: run allocation recomputation every 5 seconds even if no events
* **Implementation**:
  * Only runs when `loadSharingGroupState.isController()` is true
  * Maintain stale flag; on status change or timeout: set stale, trigger recomputation
  * Update in-memory allocation state in `LoadSharingGroupState`
  * Log significant changes (member came online, allocation changed by >0.5A, etc.)
* **Dependencies**: Phase 4.1, Phase 3.2, Phase 2

#### 4.3: Push allocations to members via WebSocket

* **Status**: ⏳ NOT STARTED
* **Description**: After each allocation computation, controller sends each member's allocation via the existing WebSocket connection
* **Files to modify**:
  * `src/loadsharing_peer_poller.cpp` - send allocation message after recomputation
* **Implementation**:
  * After allocation computed, for each online member with a WS\_CONNECTED connection:
    * Send JSON: `{"loadsharing": {"target_current": 16.5, "reason": "equal_share"}}`
    * Use the existing `MongooseWebSocketClient` connection from Phase 2
  * Controller also applies its own allocation locally (step 4.4)
* **Message format**:
  ```json
  {"loadsharing": {"target_current": 16.5, "reason": "equal_share"}}
  ```
  * **Dependencies**: Phase 4.2, Phase 2 (WebSocket connections to members)

#### 4.4: Apply allocations via EvseManager claim system

* **Status**: ⏳ NOT STARTED
* **Description**: Both controller and members apply their allocation as a claim in the EvseManager
* **Files to create/modify**:
  * `src/web_server.cpp` - add WebSocket receive handler for allocation messages from controller (member side)
  * `src/loadsharing_peer_poller.cpp` - apply local allocation on controller
* **Implementation**:
  * **On controller**: After computing allocation for self, apply as `EvseClient_LoadSharing` claim at `Priority_Limit`
  * **On members**: When WebSocket `/ws` receives a message containing `loadsharing.target_current`:
    * Parse allocation from JSON
    * Apply as `EvseClient_LoadSharing` claim at `Priority_Limit`
    * Track `last_allocation_time` for timeout failsafe
  * **Claim client**: Create new `EvseClient_LoadSharing` enum value in `evse_man.h` to avoid conflicts with existing shaper or manual override claims
  * Member's `/ws` endpoint currently only sends; adding receive capability for allocation messages is a new pattern
* **Dependencies**: Phase 4.3, existing `EvseManager` claim system

***

## Phase 5: Failsafe & Safety

### Objective

Ensure system remains safe when the controller goes offline or members become unreachable. Leverages existing Current Shaper timeout mechanism.

### Tasks

#### 5.1: Member failsafe via claim timeout

* **Status**: ⏳ NOT STARTED
* **Description**: Members' load sharing claims expire if the controller stops sending updates
* **Files to modify**:
  * Claim setup in Phase 4.4 - configure claim with timeout matching `current_shaper_data_maxinterval` (120s)
* **Implementation**:
  * The `EvseClient_LoadSharing` claim on members has a configurable timeout
  * If no allocation update received within the timeout period, the claim automatically expires
  * When claim expires: member reverts to normal operation (no load sharing limit)
  * This leverages the **existing Current Shaper timeout mechanism** - no new failsafe code needed
  * Timeout is configurable via `current_shaper_data_maxinterval` (default 120 seconds)
* **Dependencies**: Phase 4.4
* **Testing**: Kill controller process → verify member claim expires → member resumes normal charging

#### 5.2: Controller failsafe for offline members

* **Status**: ⏳ NOT STARTED
* **Description**: Controller's allocation algorithm handles offline members with conservative accounting
* **Files to modify**:
  * `src/loadsharing_algorithm.h/cpp` - use `failsafe_peer_assumed_current` for offline members (already designed in Phase 4.1)
* **Implementation**:
  * Already part of the allocation algorithm (Phase 4.1, step 1):
    * For each offline member, reserve `failsafe_peer_assumed_current` (default 6A)
    * Reduces available current for online members
  * Failsafe modes (configurable):
    * **"safe\_current" (default)**: Reserve assumed current for offline members, continue charging online members
    * **"disable" (strict)**: If any configured member is offline, set all allocations to 0
  * When offline member comes back online, resume normal allocation
  * Log failsafe events for debugging
* **Dependencies**: Phase 4.1

#### 5.3: Validate failsafe config

* **Status**: ⏳ NOT STARTED
* **Description**: Ensure failsafe config fields have valid values
* **Files to modify**:
  * `src/app_config.cpp` - validation on config POST
* **Config fields validated**:
  * `loadsharing_failsafe_mode`: must be "disable" or "safe\_current"
  * `loadsharing_failsafe_safe_current`: must be >= 0
  * `loadsharing_failsafe_peer_assumed_current`: must be >= 0
  * `loadsharing_heartbeat_timeout`: must be >= 5 seconds
* **Dependencies**: Phase 0 (config schema)

***

## Phase 6: Configuration Consistency & Synchronization ⏸️ DEFERRED

> **This phase is deferred to future development.** The controller/member
> model eliminates the need for distributed configuration synchronization in
> the MVP. The controller is the single source of truth and pushes config to
> members directly (Phase 3.4).

### Deferred features

* Config hash / version tracking (`loadsharing_config_version`, `loadsharing_config_updated_at`)
* Config hash exchange in WebSocket status messages
* Automatic config mismatch detection
* Conservative fallback on config mismatch (use min values)
* Automatic config sync between peers
* Conflict resolution for simultaneous updates

### Migration path

The `loadsharing_config_version` and `loadsharing_config_updated_at` config
fields remain defined in the schema but are unused in the MVP. They serve
as extension points for implementing distributed sync in the future.
When/if distributed sync is implemented, it would allow any node in the
group to be configured (not just the controller), with changes automatically
propagating to all peers.
-------------------------

## Phase 7: Web UI / Frontend

### Objective

Provide user-friendly interface for configuring and monitoring load sharing.

### Tasks

#### 7.1: Controller UI - Configuration panel

* **Description**: Configuration UI for the controller device
* **Files to create**: `gui-v2/src/` - Svelte components for load sharing
* **Features**:
  * Enable/disable load sharing (sets role to controller on first enable)
  * Group settings: group\_id, group\_max\_current, safety\_factor
  * Peer discovery: trigger mDNS scan, show discovered peers
  * Peer management: add/remove peers from group
  * Failsafe settings: mode, safe\_current, peer\_assumed\_current, heartbeat\_timeout
* **Dependencies**: Phase 3, API endpoints

#### 7.2: Controller UI - Monitoring dashboard

* **Description**: Real-time monitoring dashboard on controller
* **Features**:
  * Peer list with online/offline indicator and connection state
  * Per-peer allocated current vs actual current draw
  * Group utilization gauge (total actual vs group max)
  * Failsafe status (active/inactive)
  * Last computed timestamp
* **Dependencies**: Phase 4 (/loadsharing/status endpoint with allocations)

#### 7.3: Member UI - Read-only status display

* **Description**: Status-only UI on member devices
* **Features**:
  * Role indicator: "Member - managed by {controller\_host}"
  * Current allocation (target\_current) received from controller
  * Actual current draw
  * Controller connection status (connected/disconnected)
  * Load sharing config fields shown but greyed out (read-only)
* **Dependencies**: Phase 3 (role awareness), Phase 4 (allocation delivery)

#### 7.4: Add load sharing endpoints to API spec

* **Status**: ✅ COMPLETED - Schema updated with `joined` field
* **Description**: Update OpenAPI spec (api.yml) with new endpoints and improved LoadSharingPeer schema
* **Files modified**: `api.yml`
* **Completed Updates**:
  * `LoadSharingPeer` schema enhanced with `joined` field (boolean)
  * All endpoints documented
* **TODO**: Update api.yml with controller/member role fields and allocation delivery schema

***

## Phase 8: Testing & Validation with divert\_sim

### Objective

Ensure reliability and safety of load sharing system using divert\_sim extended with OpenEVSE\_Emulator integration for multi-peer simulation.

### Overview

All automated testing is performed via the divert\_sim native build framework, extended with OpenEVSE\_Emulator for multi-peer simulation. Tests validate the controller/member architecture end-to-end.

### Tasks

#### 8.1: Extend divert\_sim with multi-peer support

* **Description**: Add infrastructure to divert\_sim for managing multiple simulated OpenEVSE peers (one controller + N members)
* **Files to create/modify**:
  * `divert_sim/test_loadsharing.py` - pytest test suite for load sharing scenarios
  * `divert_sim/loadsharing_harness.py` - harness to orchestrate multi-peer simulations
  * `divert_sim/requirements.txt` - add dependencies (requests, websocket-client, etc.)
* **Implementation**:
  * Create Python harness that can:
    * Launch multiple OpenEVSE\_Emulator instances on different ports
    * Designate one instance as controller, others as members
    * Configure controller with group settings and add members
    * Verify config push to members
    * Simulate WebSocket connections and status updates
    * Control peer behavior (go offline, change status)
  * Integrate with existing divert\_sim test framework
* **Dependencies**: OpenEVSE\_Emulator repository

#### 8.2: OpenEVSE\_Emulator integration layer

* **Description**: Create Python wrapper to use OpenEVSE\_Emulator as load sharing peer
* **Files to create**:
  * `divert_sim/emulator_peer.py` - wrapper class for OpenEVSE\_Emulator instance
* **Implementation**:
  * **EmulatorPeer class**: Initialize OpenEVSE\_Emulator with unique device\_id, hostname
  * Expose HTTP endpoints and WebSocket /ws
  * Simulate realistic EV/EVSE behavior (charging, vehicle connected, etc.)
  * Allow test code to inject failures (disconnect, timeout)
* **Dependencies**: OpenEVSE\_Emulator

#### 8.3: Allocation algorithm validation tests

* **Description**: Comprehensive test suite for allocation algorithm
* **Files to create**:
  * `divert_sim/test_loadsharing_allocation.py`
* **Test scenarios**:
  1. **Single member demanding**: 1 member, 50A limit → member gets 50A
  2. **Two members, equal share**: 2 members, 50A limit → 25A each
  3. **Three members, sufficient capacity**: 3 members, 60A limit, min=6A → 20A each
  4. **Insufficient capacity for minimums**: 4 members, 20A limit, min=6A → deterministic subset gets 6A, one gets 0A
  5. **Mixed demanding and idle**: 3 members, 50A limit, only 1 demanding → demanding gets 50A
  6. **Offline member, conservative accounting**: 3 members, 50A limit, 1 offline → online members share (50-6)=44A
  7. **Edge cases**: zero members, zero available current
* **Assertions**: Totals never exceed limit, offline reserves correct, deterministic ordering

#### 8.4: Controller/member flow integration tests

* **Description**: Test the complete controller → member lifecycle
* **Files to create**:
  * `divert_sim/test_loadsharing_controller_member.py`
* **Test scenarios**:
  1. **Config push**: Controller adds member → member receives config → role transitions to "member"
  2. **Config lockout**: Member rejects POST to loadsharing config fields (403)
  3. **Allocation delivery**: Controller computes allocation → pushes to member via WS → member applies claim
  4. **Config update propagation**: Controller changes group\_max\_current → updated config pushed to members
  5. **Member removal**: Controller DELETEs member → member receives reset config → role transitions to ""
  #### 8.5: Failsafe behavior validation tests
* **Description**: Verify system remains safe during fault scenarios
* **Files to create**:
  * `divert_sim/test_loadsharing_failsafe.py`
* **Test scenarios**:
  1. **Controller offline → member claim expires**: Kill controller → wait for claim timeout (120s) → member resumes normal operation
  2. **Graceful mode, member offline**: 3 members, 1 goes offline → controller recomputes, reserves 6A for offline member
  3. **Strict mode, member offline**: `failsafe_mode="disable"` → controller sets all allocations to 0
  4. **Member recovery**: Offline member comes back → normal allocation resumes
  5. **Rapid offline/online cycling**: Network instability → exponential backoff prevents thrashing
  #### 8.6: End-to-end integration tests
* **Description**: Full workflow from discovery to charging in controller/member model
* **Files to create**:
  * `divert_sim/test_loadsharing_e2e.py`
* **Test scenarios**:
  1. **Happy path: 3-member group**:
     * Controller discovers 2 peers, adds them, pushes config
     * Members transition to role=member
     * Controller monitors status, computes allocations, pushes to members
     * All members apply claims, total current stays under limit
  2. **Member failure and recovery**:
     * Member goes offline → controller recomputes with conservative accounting
     * Member comes back → normal allocation resumes
  3. **Dynamic peer addition**:
     * Controller has 2 members, discovers and adds a 3rd
     * Config pushed to new member, allocations recomputed for all 3
  4. **Controller failure**:
     * Controller goes offline → member claims expire after 120s
     * Members resume normal operation (no load sharing)
     #### 8.7: Performance and stress tests
* **Description**: Validate system performance under load
* **Files to create**:
  * `divert_sim/test_loadsharing_performance.py`
* **Test scenarios**:
  1. **8-member maximum group size**: Allocation completes in <200ms
  2. **High-frequency status updates**: 3 members, 1 update/sec for 10 min → stable
  3. **Network instability**: Random disconnects → safe behavior maintained
  #### 8.8: CI integration and test result reporting
* **Description**: Integrate test suite into existing CI/CD pipeline
* **Existing Infrastructure**:
  * GitHub Actions workflow (`divert_sim.yaml`) already runs divert\_sim
  * pytest JUnit XML output, EnricoMi test result publishing
* **Load Sharing Test Integration**:
  * New test modules included automatically in pytest discovery
  * Same CI workflow, no changes needed
  * Test results published as GitHub checks on PRs
  ***

## Phase 9: Documentation & Release

### Objective

Provide clear documentation and prepare for production release.

### Tasks

#### 9.1: Write user guide

* **Description**: Explain load sharing setup from the controller device
* **Files to create**: `docs/load-sharing-user-guide.md`
* **Content**:
  * What is load sharing and why it's useful
  * Prerequisites (devices on same LAN, etc.)
  * Step-by-step setup: choose controller, discover peers, add to group, verify allocations
  * Understanding the controller vs member roles
  * Failsafe behavior and what happens when controller goes offline
  * Troubleshooting (members not discovered, config not pushed, etc.)
  #### 9.2: Write developer guide
* **Description**: Explain architecture and code organization
* **Files to create**: `docs/load-sharing-dev-guide.md`
* **Content**:
  * Controller/member architecture overview
  * Data structures and state management
  * Allocation algorithm walkthrough
  * WebSocket message formats
  * Claim system integration
  * Failsafe mechanisms
  * Future extensions (distributed sync, controller election)
  #### 9.3: API documentation updates
* **Description**: Ensure OpenAPI spec is complete for controller/member model
* **Files to modify**: `api.yml`
* **Content**:
  * Role-specific endpoint behavior (403 on members)
  * Allocation delivery WebSocket message schema
  * Config push payload
  * Updated LoadSharingStatus with role and allocation fields
  #### 9.4: Code review & cleanup
* **Description**: Final review, remove debug code, optimize
* **Tasks**:
  * Code review across all load sharing files
  * Remove excessive debug logging
  * Performance review
  * Thread safety verification
  ***

## Implementation Sequence & Dependencies

### Critical Path (MVP)

```
Phase 0 (✅) → Phase 1 (✅) → Phase 2 (✅)
    → Phase 3 (Roles & Config Push)
        → Phase 4 (Allocation Algorithm + Delivery)
            → Phase 5 (Failsafe)
                → Phase 7 (Web UI)
                    → Phase 8 (Testing)
                        → Phase 9 (Docs)
```

### Phase 6 (Config Sync) → DEFERRED to future

### Parallel work

* Phase 7 (Web UI) can begin in parallel with Phase 4-5 (API exists from Phase 3)
* Phase 8.3 (allocation algorithm tests) can begin with Phase 4.1
* Phase 9 (documentation) can begin with Phase 7

***

## Testing Strategy

### Overview

All automated testing is performed via the **divert\_sim** native build framework, extended with OpenEVSE\_Emulator for multi-peer simulation. Tests validate the **controller/member architecture** end-to-end.

### Testing Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     divert_sim (native build)                │
│  ┌─────────────────────────────────────────────────────┐    │
│  │     Load Sharing Test Harness (Python)              │    │
│  │  - Orchestrates controller + member scenarios       │    │
│  │  - Controls peer lifecycle                          │    │
│  │  - Simulates network conditions                     │    │
│  └────────────┬────────────────────────────────────────┘    │
│               │                                              │
│  ┌────────────┴─────────────────────────────────────────┐   │
│  │         OpenEVSE_Emulator Integration Layer          │   │
│  │  - EmulatorPeer wrapper class                        │   │
│  │  - Controller / Member role simulation               │   │
│  │  - WebSocket /ws with allocation delivery            │   │
│  └─┬──────────┬──────────┬──────────┬──────────────────┘   │
│    │          │          │          │                       │
│  ┌─┴──┐    ┌─┴──┐    ┌─┴──┐    ┌─┴──┐                     │
│  │CTRL│    │MBR1│    │MBR2│    │MBR3│                      │
│  │ EV │    │ EV │    │ EV │    │ EV │                      │
│  └────┘    └────┘    └────┘    └────┘                      │
└─────────────────────────────────────────────────────────────┘
```

### Test Levels

1. **Allocation Algorithm Tests** (Phase 8.3)
   * Pure logic validation with synthetic peer configurations
   * Edge case coverage (zero members, insufficient capacity, offline members)
   * Conservative accounting validation
   2. **Controller/Member Flow Tests** (Phase 8.4)
   * Config push from controller to member
   * Config lockout on members
   * Allocation delivery and claim application
   3. **Failsafe Behavior Tests** (Phase 8.5)
   * Controller offline → member claim expires
   * Member offline → controller reserves capacity
   * Recovery scenarios
   4. **End-to-End Integration Tests** (Phase 8.6)
   * Full workflow: discovery → config push → allocation → failsafe → recovery
   * Multi-member scenarios (3-8 members)
   * Network failure simulation
   5. **Performance & Stress Tests** (Phase 8.7)
   * 8-member maximum group size
   * High-frequency status updates
   * Network instability
   ### Testing Without Hardware

* **Native build executable**: Compile entire ESP32 firmware for native Linux/Mac execution
* **OpenEVSE\_Emulator**: Python-based RAPI + HTTP + WebSocket simulation
* **Mock network layer**: Simulate HTTP requests, WebSocket connections, mDNS discovery
* **Time control**: Fast-forward simulated time for heartbeat timeout tests

### Continuous Integration

* **CI pipeline**: Run all load sharing tests on each PR
* **Artifacts**: Test reports archived for debugging
* **Existing infrastructure**: Reuses divert\_sim.yaml workflow, pytest, JUnit output

***

## Open Questions / Decisions Needed

1. **Phase 2b (WebSocket Client)**: ✅ RESOLVED - `MongooseWebSocketClient` implemented in ArduinoMongoose `websocket_client` branch
2. **Architecture model**: ✅ RESOLVED (March 2026) - **Controller/member model** for MVP
   * Controller designated implicitly (device where user configures load sharing)
   * Controller pushes config to members, computes allocations, pushes via WebSocket
   * Members apply allocations as claims, existing shaper timeout provides failsafe
   * Distributed config sync (Phase 6) deferred to future
   3. **Allocation delivery mechanism**: ✅ RESOLVED - Controller pushes via WebSocket
   * Controller connects to members' `/ws` endpoint (reuses Phase 2 peer poller code)
   * Controller sends `{"loadsharing": {"target_current": N, "reason": "..."}}` messages
   * Members add receive handler to parse allocation and apply as claim
   4. **Failsafe mechanism**: ✅ RESOLVED - Existing Current Shaper timeout (120s)
   * Members' `EvseClient_LoadSharing` claims have configurable timeout
   * No allocation update within 120s → claim expires → normal operation
   * No custom failsafe code needed for MVP
   5. **Claim client identity**: Use dedicated `EvseClient_LoadSharing` client ID
   * Avoids conflicts if both load sharing and solar divert are active simultaneously
   * Claim priority: `Priority_Limit` (coexists with manual overrides)
   6. **Controller failover**: ⏳ DEFERRED - Manual reconfiguration required if controller goes offline permanently. Automatic controller election is a future enhancement.
