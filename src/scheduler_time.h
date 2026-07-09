#ifndef SCHEDULER_TIME_H
#define SCHEDULER_TIME_H

#include <stdint.h>

// Pure weekly-wheel time math for the scheduler, kept dependency-free so it
// can be unit tested natively (test/test_scheduler_time).  Both the event
// duration and the next-event delay previously computed these spans with
// unsigned arithmetic, underflowing to ~49.5 days whenever the target sat
// earlier in the week than the reference point.

namespace SchedulerTime
{
  constexpr int32_t SECONDS_IN_A_DAY  = 24 * 60 * 60;
  constexpr int32_t SECONDS_IN_A_WEEK = 7 * SECONDS_IN_A_DAY;

  // Span in seconds from (fromDay, fromOffset) to (toDay, toOffset) on a
  // weekly wheel.  Days are 0-6, offsets are seconds into the day.
  //
  // wrapZero: treat a zero-length span as a full week.  Event durations need
  // this — a lone event's "next" is itself, meaning next week's occurrence —
  // while a delay of zero legitimately means "now".
  inline uint32_t weeklySpan(int fromDay, int32_t fromOffset,
                             int toDay, int32_t toOffset,
                             bool wrapZero)
  {
    int days = toDay - fromDay;
    if(days < 0) {
      days += 7;
    }

    int32_t span = (days * SECONDS_IN_A_DAY) + (toOffset - fromOffset);
    if(span < 0 || (wrapZero && 0 == span)) {
      span += SECONDS_IN_A_WEEK;
    }

    return (uint32_t)span;
  }
} // namespace SchedulerTime

#endif // SCHEDULER_TIME_H
