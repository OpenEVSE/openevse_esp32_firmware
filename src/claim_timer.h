#ifndef CLAIM_TIMER_H
#define CLAIM_TIMER_H

// Pure deadline math for timed claims, host-testable (no Arduino includes).
// All times are millis()-domain uint32_t; comparisons use rollover-safe
// signed subtraction so this remains correct across the ~49.7-day millis()
// wrap.

#include <stdint.h>

static inline bool claim_timer_expired(uint32_t release_at_ms, uint32_t now_ms)
{
  return release_at_ms != 0 && (int32_t)(now_ms - release_at_ms) >= 0;
}

static inline uint32_t claim_timer_remaining_s(uint32_t release_at_ms, uint32_t now_ms)
{
  if(release_at_ms == 0 || (int32_t)(now_ms - release_at_ms) >= 0) {
    return 0;
  }
  return (release_at_ms - now_ms + 999) / 1000;  // ceil
}

#endif // CLAIM_TIMER_H
