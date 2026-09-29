#ifndef DEADLINE_TIMER_H
#define DEADLINE_TIMER_H

// Pure wall-clock deadline math, host-testable (no Arduino includes).
// All times are millis()-domain uint32_t; comparisons use rollover-safe
// signed subtraction so this remains correct across the ~49.7-day millis()
// wrap.

#include <stdint.h>

// Maximum allowed duration, in seconds. Durations must stay far below the
// ~24.8-day (2^31 ms) signed-compare horizon used by deadline_timer_expired():
// a deadline computed past that horizon reads as already expired the instant
// it is set, and a duration approaching 2^32/1000 s (~49.7 days) wraps the
// `* 1000UL` millis multiply used to compute it. 7 days is far above any real
// boost duration and comfortably inside that horizon.
#define DEADLINE_TIMER_MAX_DURATION_S (7 * 24 * 3600)

static inline bool deadline_timer_expired(uint32_t release_at_ms, uint32_t now_ms)
{
  return release_at_ms != 0 && (int32_t)(now_ms - release_at_ms) >= 0;
}

static inline uint32_t deadline_timer_remaining_s(uint32_t release_at_ms, uint32_t now_ms)
{
  if(release_at_ms == 0 || (int32_t)(now_ms - release_at_ms) >= 0) {
    return 0;
  }
  return (release_at_ms - now_ms + 999) / 1000;  // ceil
}

#endif // DEADLINE_TIMER_H
