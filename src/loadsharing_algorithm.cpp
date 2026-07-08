/*
 * MIT License
 * Copyright (c) 2025 Jeremy Poulter
 * Load Sharing Allocation Algorithm - "Equal Share with Minimums"
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LOADSHARING_ALGORITHM)
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "loadsharing_algorithm.h"
#include <algorithm>

namespace {

constexpr double kUnderuseThresholdA = 0.1;
constexpr double kNoDrawThresholdA = 0.01;
constexpr double kUnderuseHeadroomA = 1.0;

bool hasObservedDemand(const AllocationInput& member)
{
  if (!member.online || !member.demanding) {
    return false;
  }

  if (!member.observed_current_valid) {
    return true;
  }

  return member.actual_current > kNoDrawThresholdA;
}

double computeEffectiveMaxCurrent(const AllocationInput& member)
{
  double configured_max = member.max_current;
  if (configured_max < member.min_current) {
    configured_max = member.min_current;
  }

  if (!member.online || !member.demanding || !member.observed_current_valid) {
    return configured_max;
  }

  if (member.offered_current <= 0.0 || member.actual_current < 0.0) {
    return configured_max;
  }

  const double offered = std::min(member.offered_current, configured_max);
  const double unused = offered - member.actual_current;
  if (unused <= kUnderuseThresholdA) {
    return configured_max;
  }

  const double reclaimed_cap = std::max(member.min_current,
                                        member.actual_current + kUnderuseHeadroomA);
  return std::min(reclaimed_cap, configured_max);
}

} // namespace

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
    if (hasObservedDemand(members[i])) {
      demanding_indices.push_back(i);
    } else if (members[i].online && members[i].demanding) {
      result[i].setReason("no_draw");
    }
  }

  if (demanding_indices.empty()) {
    for (size_t i = 0; i < members.size(); i++) {
      if (!members[i].online) {
        result[i].setReason("offline");
      }
    }
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
    total_min += members[idx].min_current;
  }

  DBUGF("LoadSharing: %u demanding member(s), total_min=%.1fA, I_avail=%.1fA",
        (unsigned int)demanding_indices.size(), total_min, I_avail);

  std::vector<double> effective_max(members.size(), 0.0);
  const bool apply_observed_reclaim = members.size() > 1;
  for (size_t idx : demanding_indices) {
    effective_max[idx] = apply_observed_reclaim
      ? computeEffectiveMaxCurrent(members[idx])
      : std::max(members[idx].max_current, members[idx].min_current);
  }

  if (I_avail >= total_min) {
    // Enough for all minimums - assign min + equal share of remainder
    double remainder = I_avail - total_min;

    // Iterative distribution: distribute remainder equally, cap at max, redistribute
    std::vector<double> allocations(members.size(), 0.0);

    // First pass: assign minimums
    for (size_t idx : demanding_indices) {
      allocations[idx] = members[idx].min_current;
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
        if (proposed > effective_max[idx]) {
          leftover += proposed - effective_max[idx];
          allocations[idx] = effective_max[idx];
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
      if (budget >= members[idx].min_current) {
        result[idx].setTargetCurrent(members[idx].min_current);
        result[idx].setReason("min_subset");
        budget -= members[idx].min_current;
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
