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
    _heartbeat_timeout_ms(120000),
    _base_retry_interval_ms(1000),
    _max_retry_interval_ms(60000),
    _http_timeout_ms(10000),
    _ws_stale_timeout_ms(30000),
    _ws_ping_interval_ms(15000),
    _max_retry_count(5),
    _total_messages_received(0),
    _total_http_requests(0),
    _total_ws_connections(0),
    _total_reconnects(0),
    _groupState(nullptr)
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
  // Sync peer list from authoritative source
  syncPeerList();

  // Process each peer connection state machine
  for (auto& pair : _connections) {
    processPeerConnection(pair.first, pair.second);
  }

  // Wake again after poll interval
  return _poll_interval_ms;
}

void LoadSharingPeerPoller::syncPeerList() {
  if (_groupState == nullptr) {
    return;  // No group state configured yet
  }

  const auto& peerList = _groupState->getPeers();

  // Build set of current hosts
  std::vector<String> currentHosts;
  for (const auto& peer : peerList) {
    currentHosts.push_back(peer.getHost());
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
      if (conn.httpPending && now - conn.lastHttpTime > _http_timeout_ms) {
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
      if (now - conn.lastReconnectTime >= retryDelay) {
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
      if (now - conn.lastReconnectTime >= retryDelay) {
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
    if (now - conn.lastReconnectTime > _http_timeout_ms) {
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
  return (now - conn.lastMessageTime) > _heartbeat_timeout_ms;
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
