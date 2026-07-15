#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
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

TEST_CASE("underused pilot cap preserves measured demand floor") {
  CHECK(capLoadSharingMaxCurrent(32, 8, 16) == doctest::Approx(8));
  CHECK(capLoadSharingMaxCurrent(32, 20, 16) == doctest::Approx(32));
  CHECK(capLoadSharingMaxCurrent(32, 0, 16) == doctest::Approx(32));
}
