// Pure backlight policy. Compiled for both device and native_test; no guards.
#include "backlight.h"

uint8_t bl_pct_to_duty(uint8_t pct)
{
  if (pct >= 100) {
    return 255;
  }
  return (uint8_t)(((uint16_t)pct * 255u + 50u) / 100u);
}

bool bl_should_standby(bool keep_awake, uint32_t timeout_s, uint32_t idle_ms)
{
  if (keep_awake) {
    return false;
  }
  if (timeout_s == 0) {
    return false;
  }
  // uint64_t keeps the multiply identical on the 32-bit device and 64-bit host.
  return idle_ms >= (uint64_t)timeout_s * 1000u;
}
