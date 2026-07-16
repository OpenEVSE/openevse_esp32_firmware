#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cmath>
#include "loadsharing_algorithm.h"

static AllocationInput member(const char* id, bool online = true,
                              int priority = 0, double min = 6,
                              double max = 32) {
  AllocationInput input;
  input.id = id;
  input.host = id;
  input.online = online;
  input.demanding = true;
  input.charging = true;
  input.min_current = min;
  input.max_current = max;
  input.priority = priority;
  return input;
}

static std::vector<LoadSharingAllocation> allocate(
    const std::vector<AllocationInput>& members, double budget,
    const char* mode = "safe_current", unsigned long now = 0,
    unsigned long rotation_interval = 0) {
  bool failsafe = false;
  static LoadSharingRotationState rotation;
  rotation = LoadSharingRotationState();
  return computeAllocations(members, budget, 1.0, 6.0, mode, failsafe,
                            rotation, now, rotation_interval);
}

TEST_CASE("empty and unconfigured groups return no allocations") {
  CHECK(allocate({}, 32).empty());
  CHECK(allocate({member("a")}, 0).empty());
  CHECK(allocate({member("a")}, -1).empty());
}

TEST_CASE("result contains each member exactly once") {
  auto result = allocate({member("a"), member("b"), member("c")}, 32);
  REQUIRE(result.size() == 3);
  CHECK(result[0].getId() == "a");
  CHECK(result[1].getId() == "b");
  CHECK(result[2].getId() == "c");
}

TEST_CASE("priority selects a deterministic minimum subset") {
  auto result = allocate({member("b", true, 10), member("a", true, 0)}, 6);
  CHECK(result[0].getTargetCurrent() == doctest::Approx(0));
  CHECK(result[0].getReason() == "insufficient");
  CHECK(result[1].getTargetCurrent() == doctest::Approx(6));
  CHECK(result[1].getReason() == "min_subset");
}

TEST_CASE("offline reserve reduces available current") {
  auto result = allocate({member("online"), member("offline", false)}, 32);
  CHECK(result[0].getTargetCurrent() == doctest::Approx(26));
  CHECK(result[1].getTargetCurrent() == doctest::Approx(0));
  CHECK(result[1].getReason() == "offline");
}

TEST_CASE("offline reserve exhaustion never produces negative allocations") {
  auto result = allocate({member("online"), member("offline", false)}, 4);
  REQUIRE(result.size() == 2);
  CHECK(result[0].getTargetCurrent() == doctest::Approx(0));
  CHECK(result[0].getReason() == "insufficient");
}

TEST_CASE("disable mode zeros every allocation when a peer is offline") {
  auto result = allocate(
      {member("online"), member("offline", false)}, 32, "disable");
  REQUIRE(result.size() == 2);
  for (const auto& allocation : result) {
    CHECK(allocation.getTargetCurrent() == doctest::Approx(0));
    CHECK(allocation.getReason() == "failsafe_disabled");
  }
}

TEST_CASE("max below minimum is capped without negative output") {
  auto result = allocate({member("a", true, 0, 6, 4)}, 32);
  REQUIRE(result.size() == 1);
  CHECK(result[0].getTargetCurrent() == doctest::Approx(4));
}

TEST_CASE("budget equal to the minimum is allocated") {
  auto result = allocate({member("a")}, 6);
  REQUIRE(result.size() == 1);
  CHECK(result[0].getTargetCurrent() == doctest::Approx(6));
  CHECK(result[0].getReason() == "equal_share");
}

TEST_CASE("scarcity rotation boundary survives millis rollover") {
  std::vector<AllocationInput> members = {member("a"), member("b")};
  bool failsafe = false;
  LoadSharingRotationState rotation;
  auto first = computeAllocations(
      members, 6, 1, 0, "safe_current", failsafe, rotation,
      0xFFFFFF00UL, 1000);
  auto before = computeAllocations(
      members, 6, 1, 0, "safe_current", failsafe, rotation,
      0x00000200UL, 1000);
  auto after = computeAllocations(
      members, 6, 1, 0, "safe_current", failsafe, rotation,
      0x00000300UL, 1000);

  CHECK(first[0].getTargetCurrent() == doctest::Approx(6));
  CHECK(before[0].getTargetCurrent() == doctest::Approx(6));
  CHECK(after[1].getTargetCurrent() == doctest::Approx(6));
}

TEST_CASE("demand cap detects under-draw against pilot current") {
  LoadSharingDemandState state;
  double capped = applyLoadSharingDemandCap(
      state, 32, 15.6, 16, 16, 6, true, true);
  CHECK(capped == doctest::Approx(15.6));
  CHECK(state.active);
}

TEST_CASE("demand cap never falls below the minimum offerable pilot") {
  // A near-full EV drawing a sub-minimum taper trickle (1.4A) must not be
  // capped below min_current: offering < min forces the shaper to disable the
  // port, which resets the state and creates an enable/disable limit cycle.
  LoadSharingDemandState state;
  double capped = applyLoadSharingDemandCap(
      state, 32, 1.4, 16, 16, 6, true, true);
  CHECK(capped == doctest::Approx(6.0));
  CHECK(state.active);
}

TEST_CASE("demand cap stays sticky across pilot quantization cycles") {
  LoadSharingDemandState state;
  applyLoadSharingDemandCap(state, 32, 15.6, 16, 16, 6, true, true);

  for (int i = 0; i < 4; i++) {
    double capped = applyLoadSharingDemandCap(
        state, 32, 15.6, 15, 15, 6, true, true);
    CHECK(capped == doctest::Approx(15.6));
    CHECK(state.active);
  }
}

TEST_CASE("demand cap converges downward when measured draw falls") {
  LoadSharingDemandState state;
  applyLoadSharingDemandCap(state, 32, 15.6, 16, 16, 6, true, true);
  double capped = applyLoadSharingDemandCap(
      state, 32, 10.0, 15.6, 15.6, 6, true, true);
  CHECK(capped == doctest::Approx(10.0));
}

TEST_CASE("demand cap resets when member stops demanding") {
  LoadSharingDemandState state;
  applyLoadSharingDemandCap(state, 32, 15.6, 16, 16, 6, true, true);
  double released = applyLoadSharingDemandCap(
      state, 32, 15.6, 16, 16, 6, true, false);
  CHECK(released == doctest::Approx(32));
  CHECK_FALSE(state.active);
}

TEST_CASE("demand cap resets on new charging session") {
  LoadSharingDemandState state;
  applyLoadSharingDemandCap(state, 32, 15.6, 16, 16, 6, true, true);
  applyLoadSharingDemandCap(state, 32, 0, 0, 0, 6, false, true);
  double fresh = applyLoadSharingDemandCap(
      state, 32, 15.6, 16, 16, 6, true, true);
  CHECK(fresh == doctest::Approx(15.6));
}

TEST_CASE("demand cap probes for recovery without perturbing allocation") {
  LoadSharingDemandState state;
  applyLoadSharingDemandCap(state, 32, 15.6, 16, 16, 6, true, true);

  double capped = 0;
  for (int i = 0; i < 4; i++) {
    capped = applyLoadSharingDemandCap(
        state, 32, 15.6, 15.6, 15.6, 6, true, true);
    CHECK(capped == doctest::Approx(15.6));
  }

  double probed = applyLoadSharingDemandCap(
      state, 32, 15.6, 15.6, 15.6, 6, true, true);
  CHECK(probed == doctest::Approx(15.6));

  double recovered = applyLoadSharingDemandCap(
      state, 32, 16.2, 16.2, 15.6, 6, true, true);
  CHECK(recovered == doctest::Approx(32));
  CHECK_FALSE(state.active);
}

TEST_CASE("demand cap releases when measured exceeds capped ceiling") {
  LoadSharingDemandState state;
  applyLoadSharingDemandCap(state, 32, 15.6, 16, 16, 6, true, true);
  double released = applyLoadSharingDemandCap(
      state, 32, 16.2, 16.2, 16, 6, true, true);
  CHECK(released == doctest::Approx(32));
  CHECK_FALSE(state.active);
}

TEST_CASE("sticky demand cap stabilizes integer pilot redistribution") {
  LoadSharingDemandState limited;
  LoadSharingDemandState peer;

  limited.demand_cap = 15.6;
  limited.active = true;

  std::vector<double> limited_max;
  std::vector<double> peer_alloc;

  for (int cycle = 0; cycle < 8; cycle++) {
    double offered_limited = cycle == 0 ? 16.0 : limited_max.back();
    double offered_peer = cycle == 0 ? 16.0 : peer_alloc.back();

    double effective_limited = applyLoadSharingDemandCap(
        limited, 32, 15.6, 16, offered_limited, 6, true, true);
    double effective_peer = applyLoadSharingDemandCap(
        peer, 32, 16.0, 16, offered_peer, 6, true, true);

    auto result = allocate(
        {member("limited", true, 0, 6, effective_limited),
         member("peer", true, 0, 6, effective_peer)},
        32);
    limited_max.push_back(effective_limited);
    peer_alloc.push_back(result[1].getTargetCurrent());
  }

  CHECK(limited_max.back() == doctest::Approx(15.6));
  CHECK(peer_alloc.back() == doctest::Approx(16.4).epsilon(0.2));
  for (size_t i = 2; i < peer_alloc.size(); i++) {
    CHECK(std::abs(peer_alloc[i] - peer_alloc[i - 1]) <= 0.5);
  }
}
