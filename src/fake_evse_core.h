#pragma once
#include <string>
#include <cstdint>

// OpenEVSE state codes (mirror openevse.h)
enum {
  FAKE_EVSE_NOT_CONNECTED   = 1,
  FAKE_EVSE_CONNECTED       = 2,  // real-hw transient; not emitted by state()
  FAKE_EVSE_CHARGING        = 3,
  FAKE_EVSE_GFI_FAULT       = 6,
  FAKE_EVSE_NO_GROUND       = 7,
  FAKE_EVSE_STUCK_RELAY     = 8,
  FAKE_EVSE_OVER_TEMP       = 10,
  FAKE_EVSE_SLEEPING        = 254,
};

struct FakeEvseState {
  // physical-world inputs (set via /fakeevse)
  bool        vehicle_present = false;
  std::string fault           = "none";   // none|gfci|noground|stuck|overtemp
  long        voltage_mv      = 240000;

  // controller state (mutated by RAPI)
  bool charging_allowed = true;           // $FE -> true, $FS/$FD -> false
  long pilot_a          = 32;             // advertised/charge current (A); set via $SC

  // static capability
  long min_a    = 6;
  long max_hw_a = 48;
  long max_cfg_a = 32;

  // session accrual
  uint32_t session_elapsed_s = 0;
  double   session_wh        = 0.0;
  double   total_wh          = 0.0;
  uint8_t  last_reported_state = FAKE_EVSE_NOT_CONNECTED;  // transition tracking (mutated by tick)

  // temps (tenths degC; -2560 == sensor absent)
  long temp1_tenths = 250;
  long temp2_tenths = -2560;
  long temp3_tenths = -2560;

  // fault counters
  long gfci_count = 0, nognd_count = 0, stuck_count = 0;

  uint8_t state() const;                  // derived OpenEVSE state code
  long    charge_ma() const;              // delivered current (mA); 0 unless charging
  void    set_vehicle(bool present);      // resets session on rising edge
};

// Uppercase 2-hex XOR checksum over every byte of `body`.
std::string rapi_xor_checksum(const std::string &body);
// Full reply frame: "<body>^<XX>\r"
std::string rapi_frame(const std::string &body);

// Handle one inbound RAPI command body (e.g. "$GS", "$SC 24"; no "^CK"/CR),
// mutate state, and return the framed reply ("$OK ...^XX\r"). Unknown -> "$OK".
std::string fake_evse_handle(FakeEvseState &st, const std::string &cmd);

// Advance `seconds` of simulated time. While charging, accrue session/total
// energy from charge_ma()*voltage. Returns a framed async "$ST <hex>" if the
// derived state changed since the previous call, else "".
std::string fake_evse_tick(FakeEvseState &st, double seconds);
