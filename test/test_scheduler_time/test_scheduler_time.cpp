#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "scheduler_time.h"

using SchedulerTime::weeklySpan;
using SchedulerTime::SECONDS_IN_A_DAY;
using SchedulerTime::SECONDS_IN_A_WEEK;

static int32_t hms(int h, int m, int s = 0) { return (h * 60 + m) * 60 + s; }

// Durations (wrapZero = true): span from an event to its "next", where a
// non-positive span means next week's occurrence.

TEST_CASE("duration: simple same-day window") {
  // Monday 01:00 -> Monday 06:00 = 5 hours
  CHECK(weeklySpan(1, hms(1, 0), 1, hms(6, 0), true) == 5 * 60 * 60);
}

TEST_CASE("duration: window crossing midnight") {
  // Monday 23:00 -> Tuesday 01:00 = 2 hours
  CHECK(weeklySpan(1, hms(23, 0), 2, hms(1, 0), true) == 2 * 60 * 60);
}

TEST_CASE("duration: Saturday to Sunday week boundary") {
  // Saturday 23:30 -> Sunday 00:30 = 1 hour (day 6 -> day 0 wraps)
  CHECK(weeklySpan(6, hms(23, 30), 0, hms(0, 30), true) == 60 * 60);
}

TEST_CASE("duration: Sunday-only 1-minute window gap wraps to next week") {
  // The 17:50 stop's next is the 17:49 start, same weekday, a week later.
  // This was the unsigned underflow: 17:49 - 17:50 = -60 -> ~49 days.
  CHECK(weeklySpan(0, hms(17, 50), 0, hms(17, 49), true)
        == SECONDS_IN_A_WEEK - 60);
}

TEST_CASE("duration: lone event is a full week") {
  // A lone event's next is itself: zero span must mean next week.
  CHECK(weeklySpan(3, hms(12, 0), 3, hms(12, 0), true)
        == (uint32_t)SECONDS_IN_A_WEEK);
}

TEST_CASE("duration: 1-minute window is 60 seconds, not a week") {
  CHECK(weeklySpan(0, hms(17, 49), 0, hms(17, 50), true) == 60);
}

// Delays (wrapZero = false): time from "now" to an event's next occurrence.

TEST_CASE("delay: event later today") {
  // Now Monday 08:00, event Monday 20:00 = 12 hours
  CHECK(weeklySpan(1, hms(8, 0), 1, hms(20, 0), false) == 12 * 60 * 60);
}

TEST_CASE("delay: event already passed today wraps to next week") {
  // Now Sunday 18:00, Sunday-only event at 17:49.  This was the second
  // unsigned underflow (getDelay): -11 minutes -> ~49.5 days, so week-2+
  // occurrences of single-weekday rules fired late or never.
  CHECK(weeklySpan(0, hms(18, 0), 0, hms(17, 49), false)
        == SECONDS_IN_A_WEEK - 11 * 60);
}

TEST_CASE("delay: event earlier in the week wraps forward") {
  // Now Friday 12:00, event Monday 06:00 = 3 days minus 6 hours short of...
  // day delta 1-5 = -4 -> +7 = 3 days; offset 06:00-12:00 = -6h
  CHECK(weeklySpan(5, hms(12, 0), 1, hms(6, 0), false)
        == 3 * SECONDS_IN_A_DAY - 6 * 60 * 60);
}

TEST_CASE("delay: zero means now, not next week") {
  CHECK(weeklySpan(2, hms(9, 30), 2, hms(9, 30), false) == 0);
}

TEST_CASE("delay: one second before the event") {
  CHECK(weeklySpan(2, hms(9, 29, 59), 2, hms(9, 30), false) == 1);
}

TEST_CASE("delay: one second after the event wraps to a week minus one") {
  CHECK(weeklySpan(2, hms(9, 30, 1), 2, hms(9, 30), false)
        == SECONDS_IN_A_WEEK - 1);
}
