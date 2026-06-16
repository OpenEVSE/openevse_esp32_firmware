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
 * @brief Compute load sharing allocations using "Equal Share with Minimums" algorithm.
 *
 * Algorithm steps:
 * 1. Reserve failsafe_peer_assumed_current for each offline member
 * 2. Compute I_avail = group_max_current * safety_factor - offline reserves
 * 3. Determine demanding online members
 * 4. If no demand: all get 0
 * 5. If enough for all minimums: allocate min + equal share of remainder (capped by max)
 * 6. If insufficient: select deterministic subset (sorted by id) until minimums fit
 *
 * @param members Input list of all group members (including self)
 * @param group_max_current Total circuit limit (amps)
 * @param safety_factor Safety multiplier (0-1)
 * @param failsafe_peer_assumed_current Conservative current for offline peers (amps)
 * @param failsafe_mode "safe_current" or "disable"
 * @param[out] failsafe_active Set to true if failsafe mode is engaged
 * @return Per-member allocation results
 */
std::vector<LoadSharingAllocation> computeAllocations(
    const std::vector<AllocationInput>& members,
    double group_max_current,
    double safety_factor,
    double failsafe_peer_assumed_current,
    const String& failsafe_mode,
    bool& failsafe_active
);

#endif // LOADSHARING_ALGORITHM_H
