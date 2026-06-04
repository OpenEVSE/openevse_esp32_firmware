#include "fake_evse_core.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

static std::vector<std::string> split_ws(const std::string &s) {
  std::vector<std::string> v; std::istringstream is(s); std::string t;
  while (is >> t) v.push_back(t);
  return v;
}

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

std::string fake_evse_handle(FakeEvseState &st, const std::string &cmd) {
  auto t = split_ws(cmd);
  if (t.empty()) return rapi_frame("$OK");
  const std::string &c = t[0];
  char buf[96];

  if (c == "$GV") {
    // protocol 4.0.1 (<5.0.0) => $GS uses decimal/3-token form. Do NOT bump to
    // >=5.0.0 without switching $GS to hex + 5 tokens (state pilot vflags).
    return rapi_frame("$OK 8.2.0 4.0.1");
  } else if (c == "$GS") {                          // state(dec) elapsed
    snprintf(buf, sizeof(buf), "$OK %d %u", st.state(), st.session_elapsed_s);
    return rapi_frame(buf);
  } else if (c == "$GG") {                          // milliAmps milliVolts
    snprintf(buf, sizeof(buf), "$OK %ld %ld", st.charge_ma(), st.voltage_mv);
    return rapi_frame(buf);
  } else if (c == "$GU") {                          // wattSeconds whAccumulated
    snprintf(buf, sizeof(buf), "$OK %ld %ld",
             (long)(st.session_wh * 3600.0), (long)st.total_wh);
    return rapi_frame(buf);
  } else if (c == "$GP") {                          // t1 t2 t3 (tenths)
    snprintf(buf, sizeof(buf), "$OK %ld %ld %ld",
             st.temp1_tenths, st.temp2_tenths, st.temp3_tenths);
    return rapi_frame(buf);
  } else if (c == "$GF") {                          // gfci nognd stuck (parsed base-16 by lib)
    snprintf(buf, sizeof(buf), "$OK %lX %lX %lX",
             st.gfci_count, st.nognd_count, st.stuck_count);
    return rapi_frame(buf);
  } else if (c == "$GE") {                          // pilot flags
    snprintf(buf, sizeof(buf), "$OK %ld 0", st.pilot_a);
    return rapi_frame(buf);
  } else if (c == "$GC") {                          // min maxHw pilot maxCfg
    snprintf(buf, sizeof(buf), "$OK %ld %ld %ld %ld",
             st.min_a, st.max_hw_a, st.pilot_a, st.max_cfg_a);
    return rapi_frame(buf);
  } else if (c == "$GA") {                          // scale offset
    return rapi_frame("$OK 220 0");
  } else if (c == "$GT") {                          // yr mo day hr min sec
    return rapi_frame("$OK 26 6 4 12 0 0");
  } else if (c == "$GD") {                          // delay timer (disabled)
    return rapi_frame("$OK 0 0 0 0");
  } else if (c == "$GI") {                          // serial
    return rapi_frame("$OK FAKE-0001");
  } else if (c == "$SC") {                          // set current -> echo pilot
    if (t.size() >= 2) st.pilot_a = strtol(t[1].c_str(), nullptr, 10);
    snprintf(buf, sizeof(buf), "$OK %ld", st.pilot_a);
    return rapi_frame(buf);
  } else if (c == "$SV") {                          // set voltage (mV)
    if (t.size() >= 2) st.voltage_mv = strtol(t[1].c_str(), nullptr, 10);
    return rapi_frame("$OK");
  } else if (c == "$FE") {                          // enable
    st.charging_allowed = true;  return rapi_frame("$OK");
  } else if (c == "$FS" || c == "$FD") {            // sleep / disable
    st.charging_allowed = false; return rapi_frame("$OK");
  } else if (c == "$SY") {                          // heartbeat: interval current triggered
    return rapi_frame("$OK 0 0 0");
  }
  return rapi_frame("$OK");                          // accept everything else
}

std::string fake_evse_tick(FakeEvseState &st, double seconds) {
  if (st.state() == FAKE_EVSE_CHARGING) {
    st.session_elapsed_s += (uint32_t)(seconds + 0.5);
    double watts = (st.charge_ma() / 1000.0) * (st.voltage_mv / 1000.0);
    double wh = watts * seconds / 3600.0;
    st.session_wh += wh;
    st.total_wh   += wh;
  }
  uint8_t now = st.state();
  if (now != st.last_reported_state) {
    st.last_reported_state = now;
    char buf[24];
    snprintf(buf, sizeof(buf), "$ST %02X", now);   // hex per onEvent($ST)
    return rapi_frame(buf);
  }
  return "";
}
