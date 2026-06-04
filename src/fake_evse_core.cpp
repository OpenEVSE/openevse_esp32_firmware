#include "fake_evse_core.h"
#include <cstdio>

uint8_t FakeEvseState::state() const {
  if (fault == "gfci")     return FAKE_EVSE_GFI_FAULT;
  if (fault == "noground") return FAKE_EVSE_NO_GROUND;
  if (fault == "stuck")    return FAKE_EVSE_STUCK_RELAY;
  if (fault == "overtemp") return FAKE_EVSE_OVER_TEMP;
  if (!vehicle_present)    return FAKE_EVSE_NOT_CONNECTED;
  if (!charging_allowed)   return FAKE_EVSE_SLEEPING;
  return FAKE_EVSE_CHARGING;
}

long FakeEvseState::charge_ma() const {
  return state() == FAKE_EVSE_CHARGING ? pilot_a * 1000 : 0;
}

void FakeEvseState::set_vehicle(bool present) {
  if (present && !vehicle_present) {       // rising edge: new session
    session_elapsed_s = 0;
    session_wh = 0.0;
  }
  vehicle_present = present;
}

std::string rapi_xor_checksum(const std::string &body) {
  uint8_t chk = 0;
  for (char c : body) chk ^= (uint8_t)c;
  char buf[3];
  snprintf(buf, sizeof(buf), "%02X", (unsigned)chk);
  return std::string(buf);
}

std::string rapi_frame(const std::string &body) {
  return body + "^" + rapi_xor_checksum(body) + "\r";
}
