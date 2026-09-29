#include "charge_threshold.h"

// LimitType::Value::None == 0; every other type compares the same way, so the
// only type-dependent behavior is "None never fires".
static const uint8_t TYPE_NONE = 0;

bool ChargeThreshold::reached(uint8_t type, uint32_t target, uint32_t basis, uint32_t current)
{
  if(type == TYPE_NONE || target == 0) {
    return false;
  }
  return (uint64_t)current >= (uint64_t)basis + (uint64_t)target;
}

uint32_t ChargeThreshold::remaining(uint8_t type, uint32_t target, uint32_t basis, uint32_t current)
{
  if(type == TYPE_NONE || target == 0) {
    return 0;
  }
  uint64_t goal = (uint64_t)basis + (uint64_t)target;
  return (uint64_t)current >= goal ? 0 : (uint32_t)(goal - current);
}
