#ifndef _OPENEVSE_CHARGE_THRESHOLD_H
#define _OPENEVSE_CHARGE_THRESHOLD_H

// Shared threshold evaluation for Limit and Boost.
//
// Pure integer math over values the caller reads, so it is host-testable with
// no firmware dependencies. `type` is LimitType::Value's underlying uint8_t
// (limit.h is deliberately not included here — it drags in evse_man.h);
// callers pass a LimitType and the enum converts implicitly.
//
//   basis = the dimension's value at activation. 0 for absolute targets
//           (Limit's session totals, Boost's SoC/Range); the activation
//           snapshot for delta targets (Boost's energy).
//   reached: target > 0 && current >= basis + target
//   remaining: (basis + target) - current, floored at 0.
//
// The Time dimension for Boost is wall-clock and handled by deadline_timer.h,
// not here; Limit's session-minutes Time check does flow through here.

#include <stdint.h>

class ChargeThreshold
{
  public:
    static bool reached(uint8_t type, uint32_t target, uint32_t basis, uint32_t current);
    static uint32_t remaining(uint8_t type, uint32_t target, uint32_t basis, uint32_t current);
};

#endif // _OPENEVSE_CHARGE_THRESHOLD_H
