#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "charge_threshold.h"

// Mirror of LimitType::Value (limit.h) — kept in sync by convention; a
// firmware-side caller is expected to static_assert TYPE_NONE == LimitType::None.
enum { T_NONE = 0, T_TIME = 1, T_ENERGY = 2, T_SOC = 3, T_RANGE = 4 };

TEST_CASE("absolute target (basis 0): reached at/above target") {
  CHECK_FALSE(ChargeThreshold::reached(T_SOC, 80, 0, 79));
  CHECK(ChargeThreshold::reached(T_SOC, 80, 0, 80));
  CHECK(ChargeThreshold::reached(T_SOC, 80, 0, 81));
}

TEST_CASE("zero target is never reached (matches Limit's val > 0 guard)") {
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 0, 0, 100000));
  CHECK_FALSE(ChargeThreshold::reached(T_SOC, 0, 0, 100));
  CHECK(ChargeThreshold::remaining(T_ENERGY, 0, 0, 100000) == 0);
}

TEST_CASE("None type is never reached") {
  CHECK_FALSE(ChargeThreshold::reached(T_NONE, 100, 0, 200));
  CHECK(ChargeThreshold::remaining(T_NONE, 100, 0, 200) == 0);
}

TEST_CASE("delta basis: reached measures growth since activation") {
  // 5000 Wh already in the session at arm; boost target = 2000 Wh more.
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 5000));
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 6999));
  CHECK(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 7000));
  CHECK(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 9000));
}

TEST_CASE("remaining counts down to 0 and never underflows") {
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 5000) == 2000);
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 6500) == 500);
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 7000) == 0);
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 9999) == 0);
  CHECK(ChargeThreshold::remaining(T_SOC, 80, 0, 50) == 30);
}

TEST_CASE("basis + target near uint32 max does not overflow") {
  // basis (0xFFFFFFF0) + target (0x20) = 0x100000010, which wraps to 0x10
  // under 32-bit arithmetic. current (0xFFFFFFF5) is above that wrapped
  // value but below the true 64-bit goal, so 32-bit math would wrongly
  // report "reached" with 0 remaining; the 64-bit internal sum must not.
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 0x20u, 0xFFFFFFF0u, 0xFFFFFFF5u));
  CHECK(ChargeThreshold::remaining(T_ENERGY, 0x20u, 0xFFFFFFF0u, 0xFFFFFFF5u) == 0x1Bu);
}
