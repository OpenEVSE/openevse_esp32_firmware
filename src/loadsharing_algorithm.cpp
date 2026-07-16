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

namespace {

constexpr double kDemandDetectMarginA = 0.25;
constexpr double kDemandRecoveryMarginA = 0.25;
constexpr uint8_t kDemandProbeInterval = 6;
constexpr uint8_t kDemandOfferIncreaseHoldoff = 6;

void resetLoadSharingDemandState(LoadSharingDemandState& state) {
  state.active = false;
  state.demand_cap = 0.0;
  state.probe_cycles = 0;
  state.last_probe_measured = 0.0;
  state.detection_holdoff = 0;
}

} // namespace

double applyLoadSharingDemandCap(LoadSharingDemandState& state,
                                 double configured_max,
                                 double measured_current,
                                 double pilot_current,
                                 double offered_current,
                                 double min_current,
                                 bool charging,
                                 bool demanding)
{
  if (!demanding || configured_max <= 0) {
    resetLoadSharingDemandState(state);
    state.was_charging = false;
    return configured_max;
  }

  if (charging != state.was_charging) {
    resetLoadSharingDemandState(state);
  }
  state.was_charging = charging;

  if (!charging) {
    state.last_offered = 0.0;
    return configured_max;
  }

  if (state.last_offered > 0 &&
      offered_current > (state.last_offered + 0.5)) {
    state.detection_holdoff = kDemandOfferIncreaseHoldoff;
  }
  state.last_offered = offered_current;

  bool detect_allowed = true;
  if (state.detection_holdoff > 0) {
    state.detection_holdoff--;
    detect_allowed = false;
  }

  if (detect_allowed &&
      pilot_current > min_current &&
      measured_current > 0 &&
      pilot_current > 0 &&
      measured_current < (pilot_current - kDemandDetectMarginA)) {
    if (!state.active || measured_current < state.demand_cap) {
      state.demand_cap = measured_current;
    }
    state.active = true;
    state.probe_cycles = 0;
    state.last_probe_measured = measured_current;
  }

  if (state.active && state.demand_cap > 0) {
    state.probe_cycles++;
    if (state.probe_cycles >= kDemandProbeInterval) {
      state.probe_cycles = 0;
      if (measured_current >= (state.demand_cap + kDemandRecoveryMarginA)) {
        resetLoadSharingDemandState(state);
        return configured_max;
      }
      if (measured_current > (state.last_probe_measured + kDemandRecoveryMarginA)) {
        state.demand_cap = measured_current;
        state.last_probe_measured = measured_current;
        resetLoadSharingDemandState(state);
        return configured_max;
      }
      state.last_probe_measured = measured_current;
    } else if (measured_current >= (state.demand_cap + kDemandRecoveryMarginA)) {
      resetLoadSharingDemandState(state);
      return configured_max;
    }

    return std::min(configured_max, state.demand_cap);
  }

  return configured_max;
}

std::vector<LoadSharingAllocation> computeAllocations(
    const std::vector<AllocationInput>& members,
    double group_max_current,
    double safety_factor,
    double failsafe_peer_assumed_current,
    const String& failsafe_mode,
    bool& failsafe_active,
    LoadSharingRotationState& rotation,
    uint32_t now_ms,
    uint32_t rotation_interval_ms)
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

  // Build list of demanding online members (indices into result/members).
  //
  // Members that are connected but NOT charging (EVSE state B) get their
  // minimum reserved via the shaper so the EV can start on demand, but this
  // does NOT consume the shared budget: a connected EV draws no real current,
  // so charging peers keep their full share. Once the EV starts drawing
  // (state C) it becomes "charging" and normal load sharing resumes.
  // ponytail: intentional over-allocation, bounded by (connected members * min_current)
  std::vector<size_t> demanding_indices;
  for (size_t i = 0; i < members.size(); i++) {
    if (!members[i].online || !members[i].demanding) {
      continue;
    }
    if (members[i].charging) {
      demanding_indices.push_back(i);
    } else {
      double connected_min = std::min(members[i].min_current, members[i].max_current);
      result[i].setTargetCurrent(connected_min);
      result[i].setReason("connected_min");
    }
  }

  if (demanding_indices.empty()) {
    DBUGLN("LoadSharing: No charging members, only connected minimums (if any)");
    return result;
  }

  // Sort demanding members by priority (lower value = higher priority),
  // then by id for a deterministic order within equal priority.
  std::sort(demanding_indices.begin(), demanding_indices.end(),
    [&members](size_t a, size_t b) -> bool {
      if (members[a].priority != members[b].priority) {
        return members[a].priority < members[b].priority;
      }
      return members[a].id < members[b].id;
    });

  // Time-slice equal-priority members under scarcity: advance a rotation
  // offset every rotation_interval and rotate each equal-priority run of
  // the demanding list by it. With ample budget the reorder is harmless
  // (equal-share is order-insensitive); under scarcity it moves the front
  // of the line so the same peer doesn't starve indefinitely.
  if (rotation_interval_ms > 0 && demanding_indices.size() > 1) {
    if (!rotation.initialized) {
      rotation.initialized = true;
      rotation.last_rotation_ms = now_ms;
    } else if ((uint32_t)(now_ms - rotation.last_rotation_ms) >= rotation_interval_ms) {
      rotation.offset++;
      rotation.last_rotation_ms = now_ms;
    }

    size_t runStart = 0;
    while (runStart < demanding_indices.size()) {
      size_t runEnd = runStart + 1;
      while (runEnd < demanding_indices.size() &&
             members[demanding_indices[runEnd]].priority ==
             members[demanding_indices[runStart]].priority) {
        runEnd++;
      }
      size_t runLen = runEnd - runStart;
      if (runLen > 1 && (rotation.offset % runLen) != 0) {
        std::rotate(demanding_indices.begin() + runStart,
                    demanding_indices.begin() + runStart + (rotation.offset % runLen),
                    demanding_indices.begin() + runEnd);
      }
      runStart = runEnd;
    }
  }

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
