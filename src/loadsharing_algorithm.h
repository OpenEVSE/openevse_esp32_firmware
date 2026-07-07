#ifndef LOADSHARING_ALGORITHM_H
#define LOADSHARING_ALGORITHM_H

#include <vector>
#include <Arduino.h>
#include "loadsharing_types.h"

/**
 * @brief Input for a single member in the allocation computation.
 */
struct AllocationInput {
  String id;           // Device ID or hostname
  String host;         // Hostname for allocation delivery
  bool online;         // Is peer currently reachable?
  bool demanding;      // Wants current (vehicle connected and charging enabled)
  double min_current;  // Minimum current for this EVSE (amps)
  double max_current;  // Maximum current (pilot limit, amps)
  int priority;        // Priority (lower = higher priority)
};

/**
 * @brief Rotation state for time-slicing equal-priority members under scarcity.
 *
 * Persisted by the caller across allocation computations. The offset advances
 * once per rotation_interval and reorders each equal-priority run of the
 * demanding list so the same peer does not starve indefinitely.
 */
struct LoadSharingRotationState {
  uint32_t offset = 0;
  bool initialized = false;
  unsigned long last_rotation_ms = 0;
};

/**
 * @brief Compute load sharing allocations using "Equal Share with Minimums" algorithm.
 *
 * Algorithm steps:
 * 1. Reserve failsafe_peer_assumed_current for each offline member
 * 2. Compute I_avail = group_max_current * safety_factor - offline reserves
 * 3. Determine demanding online members
 * 4. If no demand: all get 0
 * 5. If enough for all minimums: allocate min + equal share of remainder (capped by max)
 * 6. If insufficient: select subset sorted by priority (lower value = higher
 *    priority), then id, until minimums fit
 * 7. Under scarcity, rotate the front of each equal-priority run every
 *    rotation_interval_ms so equal-priority members time-slice instead of the
 *    lowest id starving the rest (rotation_interval_ms == 0 disables this).
 *
 * @param members Input list of all group members (including self)
 * @param group_max_current Total circuit limit (amps)
 * @param safety_factor Safety multiplier (0-1)
 * @param failsafe_peer_assumed_current Conservative current for offline peers (amps)
 * @param failsafe_mode "safe_current" or "disable"
 * @param[out] failsafe_active Set to true if failsafe mode is engaged
 * @param[in,out] rotation Rotation state persisted across calls (see below)
 * @param now_ms Current time in milliseconds (millis() in firmware, sim time in tests)
 * @param rotation_interval_ms Rotation period in ms; 0 disables rotation
 * @return Per-member allocation results
 *
 * @note Rotation only reorders the demanding list; the allocation math is
 *       unchanged. With ample budget the reorder is harmless (equal-share is
 *       order-insensitive). rotation_interval_ms == 0 disables rotation.
 */
std::vector<LoadSharingAllocation> computeAllocations(
    const std::vector<AllocationInput>& members,
    double group_max_current,
    double safety_factor,
    double failsafe_peer_assumed_current,
    const String& failsafe_mode,
    bool& failsafe_active,
    LoadSharingRotationState& rotation,
    unsigned long now_ms,
    unsigned long rotation_interval_ms
);

#endif // LOADSHARING_ALGORITHM_H
