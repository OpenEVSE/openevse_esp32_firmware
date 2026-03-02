/*
 * MIT License
 * Copyright (c) 2025 Jeremy Poulter
 *
 * Load Sharing Peer Poller - WebSocket-based peer status ingestion via MicroTasks
 * Maintains WebSocket connections to each peer and ingests real-time status updates
 */

#ifndef LOADSHARING_PEER_POLLER_H
#define LOADSHARING_PEER_POLLER_H

#include <MicroTasks.h>
#include <MicroTasksTask.h>
#include <MongooseWebSocketClient.h>
#include <MongooseHttpClient.h>
#include <map>
#include <vector>
#include <Arduino.h>
#include "loadsharing_types.h"

/**
 * @brief Connection states for a peer
 */
enum class PeerConnectionState {
  DISCONNECTED,      // Not connected, no active connection attempts
  HTTP_FETCHING,     // Performing initial HTTP GET /status
  HTTP_FAILED,       // HTTP failed, waiting for retry
  WS_CONNECTING,     // WebSocket handshake in progress
  WS_CONNECTED,      // WebSocket established and receiving data
  WS_FAILED,         // WebSocket failed, waiting for retry
  ERROR              // Unrecoverable error (malformed peer config, etc.)
};

/**
 * @brief Per-peer connection tracking
 */
struct PeerConnection {
  String host;                        // Hostname or IP address
  PeerConnectionState state;          // Current connection state
  MongooseWebSocketClient* wsClient;  // WebSocket client instance (nullptr if not allocated)
  MongooseHttpClientRequest* httpRequest;  // HTTP request for bootstrap (nullptr if not in progress)
  LoadSharingPeerStatus statusCache;  // Latest received status
  unsigned long lastConnectedTime;    // millis() when last successfully connected
  unsigned long lastMessageTime;      // millis() when last message received
  unsigned long lastReconnectTime;    // millis() when last reconnect attempt started
  unsigned long lastHttpTime;         // millis() when last HTTP request started
  uint16_t retryCount;                // Number of consecutive connection failures
  bool hasInitialStatus;              // True after first successful status fetch
  bool httpPending;                   // True while HTTP request is in flight

  PeerConnection() :
    host(""),
    state(PeerConnectionState::DISCONNECTED),
    wsClient(nullptr),
    httpRequest(nullptr),
    statusCache(),
    lastConnectedTime(0),
    lastMessageTime(0),
    lastReconnectTime(0),
    lastHttpTime(0),
    retryCount(0),
    hasInitialStatus(false),
    httpPending(false)
  {}

  ~PeerConnection() {
    if (wsClient != nullptr) {
      delete wsClient;
      wsClient = nullptr;
    }
    // httpRequest is managed by MongooseHttpClient, don't delete manually
    httpRequest = nullptr;
  }
};

/**
 * @brief Background poller task for peer status ingestion
 *
 * Maintains WebSocket connections to all configured peers in the load sharing group.
 * For each peer:
 * 1. Bootstrap: Initial HTTP GET /status to populate cache
 * 2. WebSocket: Connect to ws://{peer}/ws for real-time updates
 * 3. Heartbeat: Monitor last message time, mark offline if stale
 * 4. Reconnection: Exponential backoff on connection failures
 *
 * Architecture:
 * - Task wakes every poll_interval_ms (default 500ms)
 * - Checks each peer connection state and transitions as needed
 * - WebSocket messages parsed and merged into status cache
 * - HTTP handlers get immediate cached status (no blocking)
 * - Failsafe: Marks peers offline after heartbeat_timeout_ms (default 30s)
 *
 * Connection state machine:
 * DISCONNECTED -> HTTP_FETCHING -> HTTP_FAILED (retry) or WS_CONNECTING
 * WS_CONNECTING -> WS_CONNECTED or WS_FAILED (retry back to HTTP_FETCHING)
 * WS_CONNECTED -> DISCONNECTED (on close/error)
 *
 * Reconnection strategy:
 * - Exponential backoff: retry_delay = min(base_interval * 2^retryCount, max_retry_interval)
 * - Base interval: 1000ms, max interval: 60000ms
 * - After 5 consecutive failures, mark peer as persistently offline
 * - Reset retry counter on successful connection
 */
class LoadSharingPeerPoller : public MicroTasks::Task {
private:
  // Peer connections (keyed by host)
  std::map<String, PeerConnection> _connections;

  // Configuration
  unsigned long _poll_interval_ms;           // Task wake interval (default 500ms)
  unsigned long _heartbeat_timeout_ms;       // Mark offline if no message (default 120000ms)
  unsigned long _base_retry_interval_ms;     // Base retry delay (default 1000ms)
  unsigned long _max_retry_interval_ms;      // Max retry delay cap (default 60000ms)
  unsigned long _http_timeout_ms;            // HTTP request timeout (default 10000ms)
  unsigned long _ws_stale_timeout_ms;        // WebSocket stale timeout (default 30000ms)
  unsigned long _ws_ping_interval_ms;        // WebSocket PING interval (default 15000ms)
  uint16_t _max_retry_count;                 // Max retries before persistent offline (default 5)

  // Statistics
  unsigned long _total_messages_received;    // Total WebSocket messages received
  unsigned long _total_http_requests;        // Total HTTP bootstrap requests
  unsigned long _total_ws_connections;       // Total WebSocket connections established
  unsigned long _total_reconnects;           // Total reconnection attempts

  // Reference to group state (provides peer list and status)
  LoadSharingGroupState* _groupState;

  /**
   * @brief Process a single peer connection state machine
   *
   * @param host Peer hostname/IP
   * @param conn PeerConnection reference
   */
  void processPeerConnection(const String& host, PeerConnection& conn);

  /**
   * @brief Initiate HTTP GET /status bootstrap for a peer
   *
   * @param host Peer hostname/IP
   * @param conn PeerConnection reference
   */
  void startHttpBootstrap(const String& host, PeerConnection& conn);

  /**
   * @brief Initiate WebSocket connection to a peer
   *
   * @param host Peer hostname/IP
   * @param conn PeerConnection reference
   */
  void startWebSocketConnection(const String& host, PeerConnection& conn);

  /**
   * @brief Check WebSocket connection status and handle messages
   *
   * @param host Peer hostname/IP
   * @param conn PeerConnection reference
   */
  void checkWebSocketConnection(const String& host, PeerConnection& conn);

  /**
   * @brief Parse incoming WebSocket message (JSON) and update status cache
   *
   * @param host Peer hostname/IP
   * @param conn PeerConnection reference
   * @param data Message payload
   * @param len Message length
   */
  void handleWebSocketMessage(const String& host, PeerConnection& conn, const uint8_t* data, size_t len);

  /**
   * @brief Check if peer should be marked offline due to stale data
   *
   * @param conn PeerConnection reference
   * @return true if peer is stale (no messages within heartbeat_timeout_ms)
   */
  bool isPeerStale(const PeerConnection& conn) const;

  /**
   * @brief Calculate retry delay using exponential backoff
   *
   * @param retryCount Number of consecutive failures
   * @return Delay in milliseconds before next retry
   */
  unsigned long calculateRetryDelay(uint16_t retryCount) const;

  /**
   * @brief Cleanup and disconnect a peer connection
   *
   * @param conn PeerConnection reference
   */
  void disconnectPeer(PeerConnection& conn);

  /**
   * @brief Sync peer list from discovery task or config
   *
   * Adds new peers, removes deleted peers, preserves existing connection state.
   */
  void syncPeerList();

public:
  LoadSharingPeerPoller();
  virtual ~LoadSharingPeerPoller();

  /**
   * @brief Initialize the poller task
   *
   * @param groupState Reference to group state (provides authoritative peer list)
   */
  void begin(LoadSharingGroupState& groupState);

  /**
   * @brief MicroTasks setup() - called once when task is added
   */
  virtual void setup() override;

  /**
   * @brief MicroTasks loop() - called on each wake event
   *
   * @param reason Wake reason (WakePeriodic, WakeEvent, etc.)
   * @return Time until next desired wake (milliseconds)
   */
  virtual unsigned long loop(MicroTasks::WakeReason reason) override;

  // Configuration methods
  void setPollInterval(unsigned long ms) { _poll_interval_ms = ms; }
  void setHeartbeatTimeout(unsigned long ms) { _heartbeat_timeout_ms = ms; }
  void setBaseRetryInterval(unsigned long ms) { _base_retry_interval_ms = ms; }
  void setMaxRetryInterval(unsigned long ms) { _max_retry_interval_ms = ms; }
  void setHttpTimeout(unsigned long ms) { _http_timeout_ms = ms; }
  void setWsStaleTimeout(unsigned long ms) { _ws_stale_timeout_ms = ms; }
  void setWsPingInterval(unsigned long ms) { _ws_ping_interval_ms = ms; }
  void setMaxRetryCount(uint16_t count) { _max_retry_count = count; }

  // Status query methods

  /**
   * @brief Get cached status for a specific peer
   *
   * @param host Peer hostname/IP
   * @param outStatus Output parameter for status
   * @return true if peer exists and has cached status
   */
  bool getPeerStatus(const String& host, LoadSharingPeerStatus& outStatus) const;

  /**
   * @brief Get connection state for a specific peer
   *
   * @param host Peer hostname/IP
   * @return Connection state enum
   */
  PeerConnectionState getPeerConnectionState(const String& host) const;

  /**
   * @brief Check if peer has active WebSocket connection
   *
   * @param host Peer hostname/IP
   * @return true if WS_CONNECTED state
   */
  bool isPeerConnected(const String& host) const;

  /**
   * @brief Get all online peers with cached status
   *
   * @return Vector of <host, status> pairs for peers in WS_CONNECTED state
   */
  std::vector<std::pair<String, LoadSharingPeerStatus>> getAllOnlinePeerStatuses() const;

  /**
   * @brief Count peers in WS_CONNECTED state
   *
   * @return Number of peers currently connected
   */
  size_t getOnlinePeerCount() const;

  /**
   * @brief Get statistics for monitoring/debugging
   *
   * @param outTotalMessages Total messages received
   * @param outTotalHttpRequests Total HTTP bootstrap requests
   * @param outTotalWsConnections Total WebSocket connections
   * @param outTotalReconnects Total reconnection attempts
   */
  void getStatistics(unsigned long& outTotalMessages,
                     unsigned long& outTotalHttpRequests,
                     unsigned long& outTotalWsConnections,
                     unsigned long& outTotalReconnects) const;
};

/**
 * @brief Global instance of the peer poller task
 */
extern LoadSharingPeerPoller loadSharingPeerPoller;

#endif // LOADSHARING_PEER_POLLER_H
