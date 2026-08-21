#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "claim_timer.h"

TEST_CASE("claim_timer_expired: no deadline never expires") {
  CHECK_FALSE(claim_timer_expired(0, 0));
  CHECK_FALSE(claim_timer_expired(0, 1000000));
}

TEST_CASE("claim_timer_expired: before/at/after deadline") {
  CHECK_FALSE(claim_timer_expired(1000, 999));
  CHECK(claim_timer_expired(1000, 1000));
  CHECK(claim_timer_expired(1000, 1001));
}

TEST_CASE("claim_timer_expired: millis() wrap is rollover-safe") {
  // release_at_ms just after the wrap, now_ms just before it: not yet expired.
  CHECK_FALSE(claim_timer_expired(100, 0xFFFFFF00));
  // now_ms has wrapped past the deadline: expired.
  CHECK(claim_timer_expired(100, 100));
  CHECK(claim_timer_expired(100, 200));
}

TEST_CASE("claim_timer_remaining_s: no deadline is 0") {
  CHECK(claim_timer_remaining_s(0, 0) == 0);
  CHECK(claim_timer_remaining_s(0, 5000) == 0);
}

TEST_CASE("claim_timer_remaining_s: 0 once past the deadline") {
  CHECK(claim_timer_remaining_s(1000, 1000) == 0);
  CHECK(claim_timer_remaining_s(1000, 1001) == 0);
}

TEST_CASE("claim_timer_remaining_s: ceil rounding") {
  CHECK(claim_timer_remaining_s(2000, 0) == 2);       // exact 2000ms -> 2s
  CHECK(claim_timer_remaining_s(2000, 1) == 2);        // 1999ms -> ceil to 2s
  CHECK(claim_timer_remaining_s(2000, 1001) == 1);     // 999ms -> ceil to 1s
  CHECK(claim_timer_remaining_s(2000, 1000) == 1);     // 1000ms -> ceil to 1s
}

TEST_CASE("claim_timer_remaining_s: millis() wrap") {
  // deadline 100ms after the wrap, now 0x100ms before it -> 200ms left -> ceils to 1s.
  CHECK(claim_timer_remaining_s(100, 0xFFFFFF9C) == 1);
}
