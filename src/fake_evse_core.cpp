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

uint32_t FakeEvseState::vflags() const {
  uint32_t f = 0;
  if (vehicle_present)               f |= FAKE_VFLAG_EV_CONNECTED;  // physical plug
  if (state() == FAKE_EVSE_CHARGING) f |= FAKE_VFLAG_CHARGING_ON;   // relay closed
  return f;
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
    // protocol >=5.0.0 selects the hex 5-token $GS form below (state pilot
    // vflags), which is the only path where the lib reads vflags -> required for
    // EV_CONNECTED so isVehicleConnected() + session-complete reset work.
    return rapi_frame("$OK 8.2.0 5.1.0");
  } else if (c == "$GS") {                          // state(hex) elapsed(dec) pilot(hex) vflags(hex)
    snprintf(buf, sizeof(buf), "$OK %X %u %X %X",
             st.state(), st.session_elapsed_s, st.state(), st.vflags());
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
  } else if (c == "$SC") {                          // set current -> clamp to [min,maxCfg], echo pilot
    if (t.size() >= 2) {
      long req = strtol(t[1].c_str(), nullptr, 10);
      if (req < st.min_a)      req = st.min_a;
      if (req > st.max_cfg_a)  req = st.max_cfg_a;
      st.pilot_a = req;
    }
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
    // Emit $AT (not bare $ST): the lib's $ST async handler calls back with
    // vflags=0, which would clobber EV_CONNECTED that $GS set. $AT carries
    // state/pilot/capacity/vflags, so the async path preserves EV_CONNECTED ->
    // isVehicleConnected() + session-complete reset stay correct on transitions.
    char buf[32];
    snprintf(buf, sizeof(buf), "$AT %X %X %ld %X",
             now, now, st.pilot_a, st.vflags());
    return rapi_frame(buf);
  }
  return "";
}
