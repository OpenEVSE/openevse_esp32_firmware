/*
 * MIT License
 * Copyright (c) 2025 Jeremy Poulter
 *
 * Load Sharing Discovery Task - Background mDNS discovery via MicroTasks
 * Performs non-blocking peer discovery in the background using async mDNS APIs
 */

#ifndef LOADSHARING_DISCOVERY_TASK_H
#define LOADSHARING_DISCOVERY_TASK_H

#include <MicroTasks.h>
#include <MicroTasksTask.h>
#include <vector>
#include <map>
#include <Arduino.h>

class LoadSharingGroupState;

/**
 * @brief Discovered OpenEVSE peer information
 */
struct DiscoveredPeer {
  String hostname;      // Fully qualified hostname (e.g., "openevse-7856.local")
  String serviceName;   // Service instance name (e.g., "openevse-7856")
  String ipAddress;     // IP address as string
  uint16_t port;        // Service port
  std::map<String, String> txtRecords;  // TXT records (version, type, id, etc.)
  unsigned long discoveredAt;  // Timestamp when discovered (millis())
};

/**
 * @brief Background discovery task for load sharing
 *
 * Combines mDNS peer discovery with background task scheduling.
 * Runs periodic mDNS queries in the background to discover OpenEVSE peers.
 * Uses asynchronous mDNS APIs to avoid blocking HTTP request handlers.
 *
 * Architecture:
 * - Task wakes every poll_interval_ms (default 2 seconds)
 * - On cache TTL expiry, initiates async mDNS query
 * - Task polls query status and processes results when ready
 * - HTTP handlers always get immediate cached results
 * - No blocking of main request handling
 */
class LoadSharingDiscoveryTask : public MicroTasks::Task {
private:
  // Discovery cache state
  std::vector<DiscoveredPeer> _cachedPeers;
  unsigned long _lastDiscovery;  // Timestamp of last successful discovery
  unsigned long _cacheTtl;       // Cache time-to-live in milliseconds
  bool _cacheValid;              // Whether cache has been populated

  // Query timing configuration
  unsigned long _poll_interval_ms;           // How often task wakes (default 2000ms)
  unsigned long _discovery_interval_ms;      // How often to start new queries (default 60000ms)
  unsigned long _last_discovery_time;        // When last query was initiated
  unsigned long _query_timeout_ms;           // How long to wait for query results (default 5000ms)

  // Async query state
  void* _active_query;                       // Opaque mdns_search_once_t handle (void* for compatibility)
  unsigned long _query_start_time;           // When current async query started
  bool _query_in_progress;                   // True while query is running

  // Group state reference (set via begin)
  LoadSharingGroupState* _groupState;

  // Statistics
  unsigned long _discovery_count;            // Number of discovery iterations completed
  unsigned long _last_result_count;          // Peer count from last successful query

  /**
   * @brief Initiate a new async mDNS query for OpenEVSE peers
   */
  void startAsyncQuery();

  /**
   * @brief Poll the status of the active async query
   *
   * @return true if query completed and results were processed
   */
  bool pollAsyncQuery();

  /**
   * @brief Process results from completed async query
   *
   * @param peers Vector of discovered peers from query result
   */
  void processQueryResults(const std::vector<DiscoveredPeer>& peers);

  /**
   * @brief Cleanup active query resources
   */
  void cleanupQuery();

protected:
  void setup();
  unsigned long loop(MicroTasks::WakeReason reason);

public:
  /**
   * @brief Initialize the background discovery task
   *
   * @param cacheTtl Time-to-live for cached results in milliseconds (default: 60000ms)
   * @param poll_interval_ms Task wake interval in milliseconds (default: 2000ms)
   * @param discovery_interval_ms How often to start new queries (default: 10000ms)
   * @param query_timeout_ms Query timeout in milliseconds (default: 5000ms)
   */
  LoadSharingDiscoveryTask(unsigned long cacheTtl = 60000,
                           unsigned long poll_interval_ms = 2000,
                           unsigned long discovery_interval_ms = 10000,
                           unsigned long query_timeout_ms = 5000);

  /**
   * @brief Begin the task (starts in MicroTasks scheduler)
   *
   * @param groupState Reference to group state for discovery result notification.
   *                   Discovery will call groupState.onDiscoveryComplete() after
   *                   each mDNS query, and set up the discovered peers pointer.
   */
  void begin(LoadSharingGroupState& groupState);

  /**
   * @brief End the task (stops in MicroTasks scheduler)
   */
  void end();

  /**
   * @brief Manually trigger discovery on next task wake
   *
   * Used by POST /loadsharing/discover API endpoint
   */
  void triggerDiscovery();

  /**
   * @brief Get the currently cached peer list
   *
   * @return Vector of cached peers (may be empty or stale)
   */
  const std::vector<DiscoveredPeer>& getCachedPeers() const;

  /**
   * @brief Check if cached results are still valid
   *
   * @return true if cache is valid and within TTL
   */
  bool isCacheValid() const;

  /**
   * @brief Force cache refresh on next query
   */
  void invalidateCache();

  /**
   * @brief Get time remaining on cache TTL
   *
   * @return Milliseconds remaining, or 0 if cache is expired
   */
  unsigned long cacheTimeRemaining() const;

  /**
   * @brief Get the number of discovery iterations completed
   */
  unsigned long getDiscoveryCount() const {
    return _discovery_count;
  }

  /**
   * @brief Check if a query is currently in progress
   */
  bool isQueryInProgress() const {
    return _query_in_progress;
  }

  /**
   * @brief Get the last result count from a successful query
   */
  unsigned long getLastResultCount() const {
    return _last_result_count;
  }
};

/**
 * @brief Global instance of the background discovery task
 */
extern LoadSharingDiscoveryTask loadSharingDiscoveryTask;

#endif // LOADSHARING_DISCOVERY_TASK_H
