#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "charge_threshold.h"

// Mirror of LimitType::Value (limit.h) — kept in sync by the static_asserts
// in charge_threshold.cpp's includes being compiled firmware-side.
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
  // 64-bit internal sum: a huge basis must not wrap into "already reached".
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 10, 0xFFFFFFF0u, 0xFFFFFFF5u));
  CHECK(ChargeThreshold::remaining(T_ENERGY, 10, 0xFFFFFFF0u, 0xFFFFFFF5u) == 5);
}
