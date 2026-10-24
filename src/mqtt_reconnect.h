#ifndef MQTT_RECONNECT_H
#define MQTT_RECONNECT_H

// Pure MQTT reconnect-timing math, host-testable (no Arduino includes).
//
// All times are millis()-domain uint32_t. The check uses elapsed time
// (now - last), which unsigned arithmetic keeps correct across the
// ~49.7-day millis() wrap. An earlier version compared a signed `long`
// copy of millis() against an absolute deadline that was reset to 0 after
// each connect; past ~24.86 days of uptime millis() reads negative, so
// "now > 0" stayed false and a dropped connection was never retried
// (issue #1367).

#include <stdint.h>

// True when a new connection attempt should start now: either a retry was
// requested immediately (force), or at least interval_ms has passed since
// the last attempt.
static inline bool mqtt_reconnect_due(bool force, uint32_t last_attempt_ms,
                                      uint32_t now_ms, uint32_t interval_ms)
{
  return force || (uint32_t)(now_ms - last_attempt_ms) >= interval_ms;
}

#endif // MQTT_RECONNECT_H
