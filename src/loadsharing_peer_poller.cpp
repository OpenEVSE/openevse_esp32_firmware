/*
 * MIT License
 * Copyright (c) 2025 Jeremy Poulter
 *
 * Load Sharing Peer Poller - WebSocket-based peer status ingestion
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LOADSHARING_POLLER)
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "loadsharing_peer_poller.h"
#include "loadsharing_algorithm.h"
#include "app_config.h"
#include "current_shaper.h"
#include "evse_man.h"
#include "input.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MongooseHttpClient.h>
#include <MongooseHttp.h>

// Global HTTP client for all peer HTTP requests
static MongooseHttpClient httpClient;

// Global instance
LoadSharingPeerPoller loadSharingPeerPoller;

LoadSharingPeerPoller::LoadSharingPeerPoller()
  : MicroTasks::Task(),
    _poll_interval_ms(500),
    _heartbeat_timeout_ms(30000),
    _base_retry_interval_ms(1000),
    _max_retry_interval_ms(60000),
    _http_timeout_ms(10000),
    _ws_stale_timeout_ms(30000),
    _ws_ping_interval_ms(15000),
    _total_messages_received(0),
    _total_http_requests(0),
    _total_ws_connections(0),
    _total_reconnects(0),
    _groupState(nullptr),
    _configPushPending(false),
    _lastAllocationTime(0)
{
}

LoadSharingPeerPoller::~LoadSharingPeerPoller() {
  // Cleanup all connections
  for (auto& pair : _connections) {
    disconnectPeer(pair.second);
  }
  _connections.clear();
}

void LoadSharingPeerPoller::begin(LoadSharingGroupState& groupState) {
  _groupState = &groupState;

  // Initial sync of peer list
  syncPeerList();

  MicroTask.startTask(this);
  DBUGF("LoadSharingPeerPoller: Started peer poller task");
}

void LoadSharingPeerPoller::setup() {
  // Task is ready to run
  DBUGF("LoadSharingPeerPoller: Setup complete");
}

unsigned long LoadSharingPeerPoller::loop(MicroTasks::WakeReason reason) {
  _heartbeat_timeout_ms = loadsharing_heartbeat_timeout * 1000UL;
  if (_groupState) {
    _groupState->setEnabled(loadsharing_enabled);
    _groupState->setGroupId(loadsharing_group_id);
    _groupState->setGroupMaxCurrent(loadsharing_group_max_current);
    _groupState->setSafetyFactor(loadsharing_safety_factor);
  }

  // Sync peer list from authoritative source
  syncPeerList();

  // Process each peer connection state machine
  for (auto& pair : _connections) {
    processPeerConnection(pair.first, pair.second);
  }
  if (_groupState) {
    _groupState->updateCounts();
  }

  // Clear config push pending flag after all peers processed
  _configPushPending = false;

  if (!loadsharing_enabled) {
    shaper.clearLoadSharingLimit();
  }

  // Periodic allocation recomputation (controller only)
  if (_groupState && _groupState->isController()) {
    unsigned long now = millis();
    if ((long)(now - _lastAllocationTime) >= 5000 || _lastAllocationTime == 0) {
      recomputeAndPushAllocations();
      _lastAllocationTime = now;
    }
  }

  // Member failsafe timeout check + local enforcement
  if (_groupState && _groupState->isMember()) {
    _groupState->checkMemberFailsafe();

    if (_groupState->isFailsafeActive()) {
      if (!_failsafeLimitApplied) {
        // Enforce the failsafe on the local EVSE through the same shaper
        // override the allocation path uses, so it outranks a manual override.
        // Note: a change to loadsharing_failsafe_safe_current while failsafe is
        // already engaged takes effect on the next engage, not mid-engagement
        // (the limit is set once here and not re-issued while it holds).
        if (loadsharing_failsafe_mode == "disable" ||
            loadsharing_failsafe_safe_current <= 0) {
          shaper.setLoadSharingLimit(0, true);
        } else {
          shaper.setLoadSharingLimit(loadsharing_failsafe_safe_current);
        }
        _failsafeLimitApplied = true;
        DBUGF("LoadSharing: member failsafe limit applied (%s)",
              loadsharing_failsafe_mode.c_str());
      }
    } else if (_failsafeLimitApplied) {
      // Failsafe cleared: a fresh allocation has been received, and the
      // allocation handler (web_server.cpp onWsFrame) has already replaced
      // this limit with the allocation limit. Just drop our marker.
      _failsafeLimitApplied = false;
    }
  } else if (_failsafeLimitApplied) {
    // Left member role with a failsafe limit outstanding: release it.
    shaper.clearLoadSharingLimit();
    _failsafeLimitApplied = false;
  }

  // Wake again after poll interval
  return _poll_interval_ms;
}

void LoadSharingPeerPoller::syncPeerList() {
  if (_groupState == nullptr) {
    return;  // No group state configured yet
  }

  const auto& peerList = _groupState->getPeers();

  // Build set of current host:port keys
  std::vector<String> currentHosts;
  for (const auto& peer : peerList) {
    currentHosts.push_back(peer.getHostPort());
  }

  // Add new peers
  for (const String& host : currentHosts) {
    if (_connections.find(host) == _connections.end()) {
      // New peer discovered
      DBUGF("LoadSharingPeerPoller: Adding new peer %s", host.c_str());
      PeerConnection conn;
      conn.host = host;
      conn.state = PeerConnectionState::DISCONNECTED;
      _connections[host] = conn;
    }
  }

  // Remove deleted peers
  std::vector<String> toRemove;
  for (const auto& pair : _connections) {
    bool found = false;
    for (const String& host : currentHosts) {
      if (pair.first == host) {
        found = true;
        break;
      }
    }
    if (!found) {
      toRemove.push_back(pair.first);
    }
  }

  for (const String& host : toRemove) {
    DBUGF("LoadSharingPeerPoller: Removing peer %s (no longer in list)", host.c_str());
    disconnectPeer(_connections[host]);
    _connections.erase(host);
  }
}

void LoadSharingPeerPoller::processPeerConnection(const String& host, PeerConnection& conn) {
  unsigned long now = millis();

  switch (conn.state) {
    case PeerConnectionState::DISCONNECTED: {
      // Start initial HTTP bootstrap
      DBUGF("LoadSharingPeerPoller: [%s] State: DISCONNECTED -> HTTP_FETCHING", host.c_str());
      startHttpBootstrap(host, conn);
      break;
    }

    case PeerConnectionState::HTTP_FETCHING: {
      // HTTP request is in flight, state transitions happen in callbacks
      // Check for timeout
      if (conn.httpPending &&
          (long)(now - (conn.lastHttpTime + _http_timeout_ms)) >= 0) {
        DBUGF("LoadSharingPeerPoller: [%s] HTTP timeout after %lu ms", host.c_str(), _http_timeout_ms);
        conn.state = PeerConnectionState::HTTP_FAILED;
        conn.retryCount++;
        conn.httpPending = false;
        if (conn.httpRequest != nullptr) {
          conn.httpRequest->abort();
          conn.httpRequest = nullptr;
        }
      }
      break;
    }

    case PeerConnectionState::HTTP_FAILED: {
      // Wait for retry delay before attempting reconnect
      unsigned long retryDelay = calculateRetryDelay(conn.retryCount);
      if ((long)(now - (conn.lastReconnectTime + retryDelay)) >= 0) {
        DBUGF("LoadSharingPeerPoller: [%s] Retrying HTTP after %lu ms delay (attempt %d)",
              host.c_str(), retryDelay, conn.retryCount + 1);
        conn.state = PeerConnectionState::DISCONNECTED;  // Restart from HTTP
        conn.lastReconnectTime = now;
        _total_reconnects++;
      }
      break;
    }

    case PeerConnectionState::WS_CONNECTING: {
      // Start WebSocket connection if not already initiated
      if (conn.wsClient == nullptr) {
        startWebSocketConnection(host, conn);
      }

      // Check WebSocket connection status
      checkWebSocketConnection(host, conn);
      break;
    }

    case PeerConnectionState::WS_CONNECTED: {
      // Push config to newly connected peer if controller and not yet pushed
      if (_groupState && _groupState->isController() && !conn.configPushed) {
        pushConfigToPeer(host, conn);
      }

      // Re-push config if a config change was flagged
      if (_groupState && _groupState->isController() && _configPushPending && conn.configPushed) {
        conn.configPushed = false;  // Force re-push
        pushConfigToPeer(host, conn);
      }

      // Monitor for stale connection
      if (isPeerStale(conn)) {
        DBUGF("LoadSharingPeerPoller: [%s] Connection stale (no messages for %lu ms)",
              host.c_str(), now - conn.lastMessageTime);
        conn.state = PeerConnectionState::WS_FAILED;
        conn.retryCount++;
        disconnectPeer(conn);
      } else {
        // Connection healthy, call loop() to process messages
        if (conn.wsClient != nullptr) {
          conn.wsClient->loop();
        }
      }
      break;
    }

    case PeerConnectionState::WS_FAILED: {
      // Wait for retry delay before attempting reconnect
      unsigned long retryDelay = calculateRetryDelay(conn.retryCount);
      if ((long)(now - (conn.lastReconnectTime + retryDelay)) >= 0) {
        DBUGF("LoadSharingPeerPoller: [%s] Retrying connection after %lu ms delay (attempt %d)",
              host.c_str(), retryDelay, conn.retryCount + 1);
        conn.state = PeerConnectionState::DISCONNECTED;  // Restart from HTTP bootstrap
        conn.lastReconnectTime = now;
        _total_reconnects++;
      }
      break;
    }

    case PeerConnectionState::ERROR: {
      // Unrecoverable error, do nothing
      break;
    }
  }
}

void LoadSharingPeerPoller::startHttpBootstrap(const String& host, PeerConnection& conn) {
  // Build URL
  String url = "http://" + host + "/status";

  DBUGF("LoadSharingPeerPoller: [%s] Starting HTTP GET %s", host.c_str(), url.c_str());

  // Initiate async GET request with callbacks
  conn.httpRequest = httpClient.beginRequest(url.c_str());
  conn.httpRequest->setMethod(HTTP_GET);

  conn.httpRequest->onResponse([this, host](MongooseHttpClientResponse* response) {
    // Find connection by host
    auto it = this->_connections.find(host);
    if (it == this->_connections.end()) {
      return;
    }

    PeerConnection& conn = it->second;

    // Check HTTP status code
    if (response->respCode() != 200) {
      DBUGF("LoadSharingPeerPoller: [%s] HTTP failed with code %d", host.c_str(), response->respCode());
      conn.state = PeerConnectionState::HTTP_FAILED;
      conn.retryCount++;
      conn.httpPending = false;
      return;
    }

    // Parse JSON response
    MongooseString bodyStr = response->body();
    String body = bodyStr.toString();

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      DBUGF("LoadSharingPeerPoller: [%s] JSON parse error: %s", host.c_str(), error.c_str());
      conn.state = PeerConnectionState::HTTP_FAILED;
      conn.retryCount++;
      conn.httpPending = false;
      return;
    }

    // Extract status fields and populate cache
    if (doc.containsKey("amp")) {
      conn.statusCache.setAmp(doc["amp"].as<double>());
    }
    if (doc.containsKey("voltage")) {
      conn.statusCache.setVoltage(doc["voltage"].as<double>());
    }
    if (doc.containsKey("pilot")) {
      conn.statusCache.setPilot(doc["pilot"].as<double>());
    }
    if (doc.containsKey("vehicle")) {
      conn.statusCache.setVehicle(doc["vehicle"].as<uint8_t>());
    }
    if (doc.containsKey("state")) {
      conn.statusCache.setState(doc["state"].as<uint8_t>());
    }
    if (doc.containsKey("loadsharing_min_current")) {
      conn.statusCache.setMinCurrent(doc["loadsharing_min_current"].as<double>());
    }
    if (doc.containsKey("loadsharing_max_current")) {
      conn.statusCache.setMaxCurrent(doc["loadsharing_max_current"].as<double>());
    }
    if (doc.containsKey("loadsharing_priority")) {
      conn.statusCache.setPriority(doc["loadsharing_priority"].as<int>());
    }
    if (doc.containsKey("config_version")) {
      conn.statusCache.setConfigVersion(doc["config_version"].as<uint32_t>());
    }
    if (doc.containsKey("config_hash")) {
      conn.statusCache.setConfigHash(doc["config_hash"].as<String>());
    }

    conn.lastMessageTime = millis();
    conn.hasInitialStatus = true;
    conn.retryCount = 0;  // Reset retry counter on success
    conn.state = PeerConnectionState::WS_CONNECTING;
    conn.httpPending = false;

    DBUGF("LoadSharingPeerPoller: [%s] HTTP bootstrap successful (amp=%.1f, pilot=%.1f, state=%d)",
          host.c_str(), conn.statusCache.getAmp(), conn.statusCache.getPilot(), conn.statusCache.getState());
  });

  conn.httpRequest->onClose([this, host]() {
    // HTTP request closed (timeout or error)
    auto it = this->_connections.find(host);
    if (it != this->_connections.end()) {
      PeerConnection& conn = it->second;
      if (conn.httpPending && conn.state == PeerConnectionState::HTTP_FETCHING) {
        DBUGF("LoadSharingPeerPoller: [%s] HTTP connection closed without response", host.c_str());
        conn.state = PeerConnectionState::HTTP_FAILED;
        conn.retryCount++;
        conn.httpPending = false;
      }
    }
  });

  // Send the request
  httpClient.send(conn.httpRequest);

  conn.state = PeerConnectionState::HTTP_FETCHING;
  conn.lastHttpTime = millis();
  conn.httpPending = true;
  _total_http_requests++;
}

void LoadSharingPeerPoller::startWebSocketConnection(const String& host, PeerConnection& conn) {
  // Allocate WebSocket client if needed
  if (conn.wsClient == nullptr) {
    conn.wsClient = new MongooseWebSocketClient();
  }

  // Configure WebSocket client
  conn.wsClient->setPingInterval(_ws_ping_interval_ms);
  conn.wsClient->setStaleTimeout(_ws_stale_timeout_ms);
  conn.wsClient->setReconnectInterval(_base_retry_interval_ms);

  // Register message callback (flags parameter indicates opcode)
  conn.wsClient->setReceiveTXTcallback([this, host](int flags, const uint8_t* data, size_t len) {
    // Find connection by host
    auto it = this->_connections.find(host);
    if (it != this->_connections.end()) {
      this->handleWebSocketMessage(host, it->second, data, len);
    }
  });

  // Register open callback
  conn.wsClient->setOnOpen([this, host](MongooseWebSocketClient* client) {
    DBUGF("LoadSharingPeerPoller: [%s] WebSocket connection established", host.c_str());
    auto it = this->_connections.find(host);
    if (it != this->_connections.end()) {
      it->second.state = PeerConnectionState::WS_CONNECTED;
      it->second.lastConnectedTime = millis();
      it->second.lastMessageTime = millis();
      it->second.retryCount = 0;  // Reset retry counter on success
      this->_total_ws_connections++;
    }
  });

  // Register close callback
  conn.wsClient->setOnClose([this, host](int code, const char* reason) {
    DBUGF("LoadSharingPeerPoller: [%s] WebSocket closed (code=%d, reason=%s)",
          host.c_str(), code, reason ? reason : "");
    auto it = this->_connections.find(host);
    if (it != this->_connections.end()) {
      it->second.state = PeerConnectionState::WS_FAILED;
      it->second.retryCount++;
      it->second.lastReconnectTime = millis();
    }
  });

  // Build WebSocket URL
  String wsUrl = "ws://" + host + "/ws";

  DBUGF("LoadSharingPeerPoller: [%s] Connecting to WebSocket %s", host.c_str(), wsUrl.c_str());

  // Initiate WebSocket connection (need to pass c_str() instead of String)
  conn.wsClient->connect(wsUrl.c_str());
  conn.lastReconnectTime = millis();
}

void LoadSharingPeerPoller::checkWebSocketConnection(const String& host, PeerConnection& conn) {
  if (conn.wsClient == nullptr) {
    return;
  }

  // Call loop() to process WebSocket client events
  conn.wsClient->loop();

  // Check connection status
  if (conn.wsClient->isConnectionOpen()) {
    // Connection established
    if (conn.state != PeerConnectionState::WS_CONNECTED) {
      DBUGF("LoadSharingPeerPoller: [%s] WebSocket handshake complete", host.c_str());
      conn.state = PeerConnectionState::WS_CONNECTED;
      conn.lastConnectedTime = millis();
      conn.lastMessageTime = millis();
      conn.retryCount = 0;
      _total_ws_connections++;
    }
  } else {
    // Check for connection failure (timeout)
    unsigned long now = millis();
    if ((long)(now - (conn.lastReconnectTime + _http_timeout_ms)) >= 0) {
      DBUGF("LoadSharingPeerPoller: [%s] WebSocket connection timeout", host.c_str());
      conn.state = PeerConnectionState::WS_FAILED;
      conn.retryCount++;
      conn.lastReconnectTime = now;
      disconnectPeer(conn);
    }
  }
}

void LoadSharingPeerPoller::handleWebSocketMessage(const String& host, PeerConnection& conn,
                                                     const uint8_t* data, size_t len) {
  DBUGF("LoadSharingPeerPoller: [%s] Received WebSocket message (%d bytes)", host.c_str(), len);

  // Parse JSON message
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    DBUGF("LoadSharingPeerPoller: [%s] JSON parse error: %s", host.c_str(), error.c_str());
    return;
  }

  // Merge fields into status cache (delta update)
  if (doc.containsKey("amp")) {
    conn.statusCache.setAmp(doc["amp"].as<double>());
  }
  if (doc.containsKey("voltage")) {
    conn.statusCache.setVoltage(doc["voltage"].as<double>());
  }
  if (doc.containsKey("pilot")) {
    conn.statusCache.setPilot(doc["pilot"].as<double>());
  }
  if (doc.containsKey("vehicle")) {
    conn.statusCache.setVehicle(doc["vehicle"].as<uint8_t>());
  }
  if (doc.containsKey("state")) {
    conn.statusCache.setState(doc["state"].as<uint8_t>());
  }
  if (doc.containsKey("loadsharing_min_current")) {
    conn.statusCache.setMinCurrent(doc["loadsharing_min_current"].as<double>());
  }
  if (doc.containsKey("loadsharing_max_current")) {
    conn.statusCache.setMaxCurrent(doc["loadsharing_max_current"].as<double>());
  }
  if (doc.containsKey("loadsharing_priority")) {
    conn.statusCache.setPriority(doc["loadsharing_priority"].as<int>());
  }
  if (doc.containsKey("config_version")) {
    conn.statusCache.setConfigVersion(doc["config_version"].as<uint32_t>());
  }
  if (doc.containsKey("config_hash")) {
    conn.statusCache.setConfigHash(doc["config_hash"].as<String>());
  }

  conn.lastMessageTime = millis();
  _total_messages_received++;

  DBUGF("LoadSharingPeerPoller: [%s] Status updated (amp=%.1f, pilot=%.1f, state=%d)",
        host.c_str(), conn.statusCache.getAmp(), conn.statusCache.getPilot(), conn.statusCache.getState());
}

bool LoadSharingPeerPoller::isPeerStale(const PeerConnection& conn) const {
  unsigned long now = millis();
  return (long)(now - (conn.lastMessageTime + _heartbeat_timeout_ms)) >= 0;
}

unsigned long LoadSharingPeerPoller::calculateRetryDelay(uint16_t retryCount) const {
  // Exponential backoff: delay = base * 2^retry
  unsigned long delay = _base_retry_interval_ms * (1UL << retryCount);

  // Cap at max interval
  if (delay > _max_retry_interval_ms) {
    delay = _max_retry_interval_ms;
  }

  return delay;
}

void LoadSharingPeerPoller::disconnectPeer(PeerConnection& conn) {
  // Cleanup WebSocket client
  if (conn.wsClient != nullptr) {
    conn.wsClient->disconnect();
    delete conn.wsClient;
    conn.wsClient = nullptr;
  }

  // Abort HTTP request if in progress
  if (conn.httpRequest != nullptr) {
    conn.httpRequest->abort();
    conn.httpRequest = nullptr;
  }
  conn.httpPending = false;

  conn.state = PeerConnectionState::DISCONNECTED;
}

// Public status query methods

bool LoadSharingPeerPoller::getPeerStatus(const String& host, LoadSharingPeerStatus& outStatus) const {
  auto it = _connections.find(host);
  if (it == _connections.end()) {
    return false;
  }

  if (!it->second.hasInitialStatus) {
    return false;  // No status cached yet
  }

  outStatus = it->second.statusCache;
  return true;
}

PeerConnectionState LoadSharingPeerPoller::getPeerConnectionState(const String& host) const {
  auto it = _connections.find(host);
  if (it == _connections.end()) {
    return PeerConnectionState::ERROR;
  }
  return it->second.state;
}

bool LoadSharingPeerPoller::isPeerConnected(const String& host) const {
  auto it = _connections.find(host);
  if (it == _connections.end()) {
    return false;
  }
  return it->second.state == PeerConnectionState::WS_CONNECTED;
}

std::vector<std::pair<String, LoadSharingPeerStatus>> LoadSharingPeerPoller::getAllOnlinePeerStatuses() const {
  std::vector<std::pair<String, LoadSharingPeerStatus>> result;

  for (const auto& pair : _connections) {
    if (pair.second.state == PeerConnectionState::WS_CONNECTED && pair.second.hasInitialStatus) {
      result.push_back(std::make_pair(pair.first, pair.second.statusCache));
    }
  }

  return result;
}

size_t LoadSharingPeerPoller::getOnlinePeerCount() const {
  size_t count = 0;
  for (const auto& pair : _connections) {
    if (pair.second.state == PeerConnectionState::WS_CONNECTED) {
      count++;
    }
  }
  return count;
}

void LoadSharingPeerPoller::getStatistics(unsigned long& outTotalMessages,
                                           unsigned long& outTotalHttpRequests,
                                           unsigned long& outTotalWsConnections,
                                           unsigned long& outTotalReconnects) const {
  outTotalMessages = _total_messages_received;
  outTotalHttpRequests = _total_http_requests;
  outTotalWsConnections = _total_ws_connections;
  outTotalReconnects = _total_reconnects;
}

void LoadSharingPeerPoller::pushConfigToPeer(const String& host, PeerConnection& conn) {
  if (conn.configPushed) {
    return;  // Already pushed
  }

  DBUGF("LoadSharingPeerPoller: [%s] Pushing load sharing config", host.c_str());

  // Build config JSON
  DynamicJsonDocument doc(1024);
  doc["loadsharing_enabled"] = true;
  doc["loadsharing_role"] = "member";
  doc["loadsharing_controller_host"] = _groupState->getLocalHostname();
  doc["loadsharing_group_id"] = loadsharing_group_id;
  doc["loadsharing_group_max_current"] = loadsharing_group_max_current;
  doc["loadsharing_safety_factor"] = loadsharing_safety_factor;
  doc["loadsharing_heartbeat_timeout"] = loadsharing_heartbeat_timeout;
  doc["loadsharing_failsafe_mode"] = loadsharing_failsafe_mode;
  doc["loadsharing_failsafe_safe_current"] = loadsharing_failsafe_safe_current;
  doc["loadsharing_failsafe_peer_assumed_current"] = loadsharing_failsafe_peer_assumed_current;

  String* body = new String();
  serializeJson(doc, *body);

  // POST to member's /config endpoint
  String* url = new String("http://" + host + "/config");

  MongooseHttpClientRequest* req = httpClient.beginRequest(url->c_str());
  req->setMethod(HTTP_POST);
  req->setContentType("application/json");
  req->setContent(body->c_str());

  req->onResponse([this, host](MongooseHttpClientResponse* response) {
    if (response->respCode() == 200) {
      DBUGF("LoadSharingPeerPoller: [%s] Config push successful", host.c_str());
      auto it = this->_connections.find(host);
      if (it != this->_connections.end()) {
        it->second.configPushed = true;
      }
    } else {
      DBUGF("LoadSharingPeerPoller: [%s] Config push failed with code %d", host.c_str(), response->respCode());
    }
  });

  req->onClose([host, url, body]() {
    DBUGF("LoadSharingPeerPoller: [%s] Config push HTTP connection closed", host.c_str());
    delete url;
    delete body;
  });

  httpClient.send(req);
}

void LoadSharingPeerPoller::pushConfigResetToPeer(const String& host) {
  DBUGF("LoadSharingPeerPoller: [%s] Pushing config reset (removing from group)", host.c_str());

  // Build reset config JSON
  DynamicJsonDocument doc(256);
  doc["loadsharing_enabled"] = false;
  doc["loadsharing_role"] = "";
  doc["loadsharing_controller_host"] = "";

  String* body = new String();
  serializeJson(doc, *body);

  // POST to member's /config endpoint
  String* url = new String("http://" + host + "/config");

  MongooseHttpClientRequest* req = httpClient.beginRequest(url->c_str());
  req->setMethod(HTTP_POST);
  req->setContentType("application/json");
  req->setContent(body->c_str());

  req->onResponse([host](MongooseHttpClientResponse* response) {
    DBUGF("LoadSharingPeerPoller: [%s] Config reset response: %d", host.c_str(), response->respCode());
  });

  req->onClose([host, url, body]() {
    DBUGF("LoadSharingPeerPoller: [%s] Config reset HTTP connection closed", host.c_str());
    delete url;
    delete body;
  });

  httpClient.send(req);
}

void LoadSharingPeerPoller::pushConfigToAllPeers() {
  if (!_groupState || !_groupState->isController()) {
    return;
  }

  DBUGLN("LoadSharingPeerPoller: Flagging config push to all connected peers");
  _configPushPending = true;
}

std::vector<AllocationInput> LoadSharingPeerPoller::buildAllocationInputs() {
  std::vector<AllocationInput> inputs;

  if (!_groupState) {
    return inputs;
  }

  auto wasSuppressed = [this](const String& id) -> bool {
    for (const auto& allocation : _groupState->getAllocations()) {
      if (allocation.getId() == id) {
        return allocation.getReason() == "insufficient";
      }
    }
    return false;
  };

  // Add self (controller) as first member
  {
    AllocationInput self;
    self.id = _groupState->getLocalHostname();
    self.host = _groupState->getLocalHostname();
    self.online = true;
    // Controller is demanding if it has a vehicle connected and not sleeping/disabled
    uint8_t state = evse.getEvseState();
    self.demanding = (evse.isVehicleConnected() && state != OPENEVSE_STATE_SLEEPING);
    self.charging = (state == OPENEVSE_STATE_CHARGING) ||
                    wasSuppressed(self.id);
    self.min_current = evse.getMinCurrent();
    self.max_current = evse.getMaxConfiguredCurrent();
    self.priority = loadsharing_priority;
    inputs.push_back(self);
  }

  // Add each configured peer
  for (const auto& pair : _connections) {
    AllocationInput input;
    input.id = pair.first;
    input.host = pair.first;
    input.online = (pair.second.state == PeerConnectionState::WS_CONNECTED);
    // Peer is demanding if vehicle connected and state indicates charging/connected
    input.demanding = input.online &&
                      pair.second.statusCache.getVehicle() == 1 &&
                      pair.second.statusCache.getState() != 0 &&    // Not in idle state
                      pair.second.statusCache.getState() != 254;    // Not in sleep state
    input.charging = input.online &&
                     (pair.second.statusCache.getState() == OPENEVSE_STATE_CHARGING ||
                      wasSuppressed(input.id));
    input.min_current = pair.second.statusCache.getMinCurrent() > 0
                          ? pair.second.statusCache.getMinCurrent()
                          : evse.getMinCurrent();
    input.max_current = pair.second.statusCache.getMaxCurrent() > 0
                          ? pair.second.statusCache.getMaxCurrent()
                          : evse.getMaxCurrent();
    input.priority = pair.second.statusCache.getPriority();
    inputs.push_back(input);
  }

  size_t demanding_count = 0;
  for (const auto& input : inputs) {
    if (input.online && input.demanding) {
      demanding_count++;
    }
  }

  if (demanding_count > 1) {
    inputs[0].max_current = capLoadSharingMaxCurrent(inputs[0].max_current, evse.getAmps(), evse.getChargeCurrent());

    size_t index = 1;
    for (const auto& pair : _connections) {
      inputs[index].max_current = capLoadSharingMaxCurrent(
        inputs[index].max_current,
        pair.second.statusCache.getAmp(),
        pair.second.statusCache.getPilot());
      index++;
    }
  }

  return inputs;
}

void LoadSharingPeerPoller::recomputeAndPushAllocations() {
  if (!_groupState || !_groupState->isController()) {
    return;
  }

  // Build inputs
  std::vector<AllocationInput> inputs = buildAllocationInputs();

  if (inputs.empty()) {
    return;
  }

  // Compute allocations
  bool failsafe_active = false;
  std::vector<LoadSharingAllocation> allocations = computeAllocations(
    inputs,
    loadsharing_group_max_current,
    loadsharing_safety_factor,
    loadsharing_failsafe_peer_assumed_current,
    loadsharing_failsafe_mode,
    failsafe_active,
    _rotationState,
    millis(),
    loadsharing_rotation_interval * 1000UL
  );

  // Update group state
  _groupState->getAllocations() = allocations;
  _groupState->setComputedAt(millis());
  _groupState->setFailsafeActive(failsafe_active);

  // Apply local allocation (first entry is always self)
  if (!allocations.empty()) {
    double selfAllocation = allocations[0].getTargetCurrent();
    String selfReason = allocations[0].getReason();

    shaper.setLoadSharingLimit(selfAllocation, selfReason == "failsafe_disabled");
  }

  // Push allocations to connected peers
  for (size_t i = 1; i < allocations.size() && i < inputs.size(); i++) {
    const String& host = inputs[i].host;
    auto it = _connections.find(host);
    if (it != _connections.end() && it->second.state == PeerConnectionState::WS_CONNECTED) {
      sendAllocationToPeer(host, it->second, allocations[i]);
    }
  }
}

void LoadSharingPeerPoller::sendAllocationToPeer(const String& host, PeerConnection& conn,
                                                   const LoadSharingAllocation& alloc) {
  if (conn.wsClient == nullptr || !conn.wsClient->isConnectionOpen()) {
    return;
  }

  // Build allocation JSON message
  DynamicJsonDocument doc(256);
  JsonObject ls = doc.createNestedObject("loadsharing");
  ls["target_current"] = alloc.getTargetCurrent();
  ls["reason"] = alloc.getReason();

  String msg;
  serializeJson(doc, msg);

  DBUGF("LoadSharingPeerPoller: [%s] Sending allocation: %.1fA (%s)",
        host.c_str(), alloc.getTargetCurrent(), alloc.getReason().c_str());

  conn.wsClient->sendTXT(msg.c_str(), msg.length());
}
