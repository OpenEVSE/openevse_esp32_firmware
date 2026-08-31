#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "deadline_timer.h"

TEST_CASE("deadline_timer_expired: no deadline never expires") {
  CHECK_FALSE(deadline_timer_expired(0, 0));
  CHECK_FALSE(deadline_timer_expired(0, 1000000));
}

TEST_CASE("deadline_timer_expired: before/at/after deadline") {
  CHECK_FALSE(deadline_timer_expired(1000, 999));
  CHECK(deadline_timer_expired(1000, 1000));
  CHECK(deadline_timer_expired(1000, 1001));
}

TEST_CASE("deadline_timer_expired: millis() wrap is rollover-safe") {
  // deadline just after the wrap, now just before it: not yet expired.
  CHECK_FALSE(deadline_timer_expired(100, 0xFFFFFF00));
  // now == deadline: expired.
  CHECK(deadline_timer_expired(100, 100));
  // now has wrapped past the deadline: expired.
  CHECK(deadline_timer_expired(100, 200));
}

TEST_CASE("deadline_timer_remaining_s: no deadline is 0") {
  CHECK(deadline_timer_remaining_s(0, 0) == 0);
  CHECK(deadline_timer_remaining_s(0, 5000) == 0);
}

TEST_CASE("deadline_timer_remaining_s: 0 once past the deadline") {
  CHECK(deadline_timer_remaining_s(1000, 1000) == 0);
  CHECK(deadline_timer_remaining_s(1000, 1001) == 0);
}

TEST_CASE("deadline_timer_remaining_s: ceil rounding") {
  CHECK(deadline_timer_remaining_s(2000, 0) == 2);     // exact 2000ms -> 2s
  CHECK(deadline_timer_remaining_s(2000, 1) == 2);     // 1999ms -> ceil 2s
  CHECK(deadline_timer_remaining_s(2000, 1001) == 1);  // 999ms -> ceil 1s
  CHECK(deadline_timer_remaining_s(2000, 1000) == 1);  // 1000ms -> 1s
}

TEST_CASE("deadline_timer_remaining_s: millis() wrap") {
  // deadline 100ms after the wrap, now 0x100ms before it -> 200ms -> 1s.
  CHECK(deadline_timer_remaining_s(100, 0xFFFFFF9C) == 1);
}

TEST_CASE("DEADLINE_TIMER_MAX_DURATION_S survives the deadline math") {
  uint32_t now_ms = 0;
  uint32_t release_at_ms = now_ms + (uint32_t)DEADLINE_TIMER_MAX_DURATION_S * 1000UL;

  CHECK_FALSE(deadline_timer_expired(release_at_ms, now_ms));
  CHECK(deadline_timer_remaining_s(release_at_ms, now_ms) == (uint32_t)DEADLINE_TIMER_MAX_DURATION_S);
  CHECK_FALSE(deadline_timer_expired(release_at_ms, now_ms + 1000));
  CHECK(deadline_timer_remaining_s(release_at_ms, now_ms + 1000) == (uint32_t)DEADLINE_TIMER_MAX_DURATION_S - 1);
}
