/*
 * MIT License
 * Copyright (c) 2025 Jeremy Poulter
 *
 * Load Sharing Allocation Algorithm - "Equal Share with Minimums"
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LOADSHARING_ALGORITHM)
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "loadsharing_algorithm.h"
#include <algorithm>

std::vector<LoadSharingAllocation> computeAllocations(
    const std::vector<AllocationInput>& members,
    double group_max_current,
    double safety_factor,
    double failsafe_peer_assumed_current,
    const String& failsafe_mode,
    bool& failsafe_active)
{
  std::vector<LoadSharingAllocation> result;
  failsafe_active = false;

  if (members.empty() || group_max_current <= 0) {
    return result;
  }

  // Count offline members and compute offline reserve
  int offline_count = 0;
  double offline_reserve = 0.0;

  for (const auto& m : members) {
    LoadSharingAllocation alloc;
    alloc.setId(m.id);
    alloc.setTargetCurrent(0.0);
    alloc.setReason("idle");
    result.push_back(alloc);

    if (!m.online) {
      offline_count++;
    }
  }

  // If failsafe_mode is "disable" and any member is offline, set all to 0
  if (offline_count > 0 && failsafe_mode == "disable") {
    failsafe_active = true;
    for (auto& alloc : result) {
      alloc.setTargetCurrent(0.0);
      alloc.setReason("failsafe_disabled");
    }
    DBUGF("LoadSharing: Failsafe DISABLE mode - %d offline member(s), all allocations set to 0", offline_count);
    return result;
  }

  // Reserve current for offline members (conservative accounting)
  if (offline_count > 0) {
    offline_reserve = offline_count * failsafe_peer_assumed_current;
    failsafe_active = true;
    DBUGF("LoadSharing: Reserving %.1fA for %d offline member(s)", offline_reserve, offline_count);
  }

  // Compute available current
  double I_avail = (group_max_current * safety_factor) - offline_reserve;
  if (I_avail < 0) {
    I_avail = 0;
  }

  DBUGF("LoadSharing: I_avail=%.1fA (max=%.1f, safety=%.2f, offline_reserve=%.1f)",
        I_avail, group_max_current, safety_factor, offline_reserve);

  // Build list of demanding online members (indices into result/members)
  std::vector<size_t> demanding_indices;
  for (size_t i = 0; i < members.size(); i++) {
    if (members[i].online && members[i].demanding) {
      demanding_indices.push_back(i);
    }
  }

  if (demanding_indices.empty()) {
    DBUGLN("LoadSharing: No demanding members, all allocations 0");
    return result;
  }

  // Sort demanding members by id for deterministic ordering
  std::sort(demanding_indices.begin(), demanding_indices.end(),
    [&members](size_t a, size_t b) {
      return members[a].id < members[b].id;
    });

  // Compute total minimum current needed
  double total_min = 0.0;
  for (size_t idx : demanding_indices) {
    total_min += std::min(members[idx].min_current, members[idx].max_current);
  }

  DBUGF("LoadSharing: %u demanding member(s), total_min=%.1fA, I_avail=%.1fA",
        (unsigned int)demanding_indices.size(), total_min, I_avail);

  if (I_avail >= total_min) {
    // Enough for all minimums - assign min + equal share of remainder
    double remainder = I_avail - total_min;
    size_t num_demanding = demanding_indices.size();

    // Iterative distribution: distribute remainder equally, cap at max, redistribute
    std::vector<double> allocations(members.size(), 0.0);

    // First pass: assign minimums
    for (size_t idx : demanding_indices) {
      allocations[idx] = std::min(members[idx].min_current, members[idx].max_current);
    }

    // Distribute remainder iteratively (handles max capping)
    double remaining = remainder;
    std::vector<size_t> uncapped = demanding_indices;

    while (remaining > 0.01 && !uncapped.empty()) {
      double share = remaining / uncapped.size();
      double leftover = 0.0;
      std::vector<size_t> still_uncapped;

      for (size_t idx : uncapped) {
        double proposed = allocations[idx] + share;
        if (proposed > members[idx].max_current) {
          leftover += proposed - members[idx].max_current;
          allocations[idx] = members[idx].max_current;
        } else {
          allocations[idx] = proposed;
          still_uncapped.push_back(idx);
        }
      }

      remaining = leftover;
      uncapped = still_uncapped;
    }

    // Write results
    for (size_t idx : demanding_indices) {
      result[idx].setTargetCurrent(allocations[idx]);
      result[idx].setReason("equal_share");
    }

  } else {
    // Not enough for all minimums - select deterministic subset
    DBUGLN("LoadSharing: Insufficient capacity for all minimums, selecting subset");

    double budget = I_avail;
    for (size_t idx : demanding_indices) {
      double required_min = std::min(members[idx].min_current, members[idx].max_current);
      if (budget >= required_min) {
        result[idx].setTargetCurrent(required_min);
        result[idx].setReason("min_subset");
        budget -= required_min;
      } else {
        result[idx].setTargetCurrent(0.0);
        result[idx].setReason("insufficient");
      }
    }
  }

  // Mark offline members
  for (size_t i = 0; i < members.size(); i++) {
    if (!members[i].online) {
      result[i].setReason("offline");
    }
  }

  return result;
}
