#ifndef _DIVERT_SIM_SIM_SCENARIO_H
#define _DIVERT_SIM_SIM_SCENARIO_H

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "time_series.h"

namespace sim {

struct PeerEvent
{
  long t_sec;
  bool set_online = false;
  bool online = false;
  bool set_vehicle = false;
  bool vehicle = false;
  bool set_request_current = false;
  bool request_current = false;
  bool set_aux_load_kw = false;
  double aux_load_kw = 0.0;

  // {"boost": {"type": "time", "value": 900}} arms; {"boost": "cancel"} cancels.
  bool set_boost = false;
  bool boost_cancel = false;
  std::string boost_type;
  uint32_t boost_value = 0;

  // {"manual": "disabled"|"active"|"release"} drives the peer's ManualOverride
  // (priority 1000), which outranks Boost (200).
  bool set_manual = false;
  std::string manual_state;

  // {"rfid": "<tag uid>"} presents a card to the simulated reader.
  bool set_rfid = false;
  std::string rfid_tag;

  // {"temperature": 70.5} sets the EVSE temperature (deg C) reported on $GP.
  bool set_temperature = false;
  double temperature = 0.0;

  // {"soc": 80} sets the EV battery SoC (also reported to the firmware when
  // the peer's ev.report_soc is set).
  bool set_soc = false;
  double soc = 0.0;

  // {"claim": {"client": "ocpp", "priority": 1050, "state": "disabled",
  //            "charge_current": 16, "max_current": 20, "auto_release": false}}
  // registers an EvseManager claim on behalf of a client the simulator does
  // not model itself (OCPP backend, MQTT, HTTP API, evcc, ...).
  bool set_claim = false;
  uint32_t claim_client = 0;
  int claim_priority = 0;
  std::string claim_state;        // "", "active" or "disabled"
  long claim_charge_current = -1; // -1 = not set
  long claim_max_current = -1;    // -1 = not set
  bool claim_auto_release = false;

  // {"release": "ocpp"} drops that client's claim.
  bool set_release = false;
  uint32_t release_client = 0;

  // {"limit": {"type": "energy", "value": 5000, "auto_release": true}} sets a
  // session limit as /limit would; {"limit": "clear"} removes it.
  bool set_limit = false;
  bool limit_clear = false;
  std::string limit_type;
  uint32_t limit_value = 0;
  bool limit_auto_release = true;

  // {"schedule": [ ...timers... ]} replaces the peer's schedule mid-run, as a
  // Charge Manager edit would.
  bool set_schedule = false;
  std::string schedule_json;
};

// Map a client name ("ocpp", "mqtt", "manual", ...) or a numeric id to an
// EvseClient id, plus the priority that client normally claims at. Returns
// false for an unknown name.
bool lookupClient(const std::string &name, uint32_t &client, int &priority);

struct PeerScenario
{
  std::string id;
  double voltage = 240.0;
  double min_current = 6.0;
  double max_current = 32.0;
  int priority = 0;

  // EV battery model
  double battery_capacity_kwh = 75.0;
  double initial_soc = 50.0;
  double max_charge_rate_kw = 7.2;
  bool initial_request_current = true;
  double initial_aux_load_kw = 0.0;

  // Initial state
  bool initial_online = true;
  bool initial_vehicle = true;

  // Time-series inputs
  TimeSeries solar;
  TimeSeries grid_ie;
  TimeSeries live_pwr;
  TimeSeries vrms;     // optional per-tick AC RMS voltage (overrides fixed 'voltage')

  // Optional per-peer override of divert mode: "off" (default), "solar", "grid".
  // When unset, divert is enabled if the peer has any solar/grid_ie data.
  std::string divert_mode;

  // Optional per-peer override of shaper enable (defaults to true if live_pwr
  // has data, false otherwise).
  bool shaper_enabled = false;
  bool shaper_enabled_set = false;

  // EVSE temperature (deg C) over time; feeds temperature throttling.
  TimeSeries temperature;

  // Charge Manager schedule: timer events in the firmware /schedule format.
  // Empty = inherit the scenario-level schedule.
  std::string schedule_json;

  // Treat inputs.live_pwr as the rest of the house and add this EV's own draw
  // to it, as a whole-site meter would see. Without it, live_pwr is used
  // as-is (recorded site data that already includes the EV).
  bool live_pwr_add_ev = false;

  // Whether an RFID reader is present on the bus.
  bool rfid_reader = true;

  // Push the simulated SoC into the firmware's vehicle data each tick (as a
  // vehicle SoC source would), enabling SoC limits.
  bool report_soc = false;

  std::vector<PeerEvent> events;
};

struct GroupScenario
{
  bool enabled = false;
  double max_current = 0.0;
  double safety_factor = 1.0;
  std::string failsafe_mode = "safe_current";
  double failsafe_peer_assumed_current = 6.0;
  double failsafe_safe_current = 6.0;
  uint32_t rotation_interval = 1800;
};

struct Scenario
{
  // simulation
  long duration_sec = 3600;
  long tick_interval_sec = 5;
  std::time_t start_epoch = 0;
  double nominal_voltage = 240.0;

  // raw config JSON to apply to app_config (or empty)
  std::string config_json;

  // Station current (config max_current_soft). It lives in the controller,
  // so it seeds each SimEvse rather than app_config. -1 = hardware max.
  long max_current_soft = -1;

  GroupScenario group;
  double supply_max_pwr_w = 0.0;
  TimeSeries supply_live_pwr;
  std::vector<PeerScenario> peers;

  // Default Charge Manager schedule for peers that do not define their own.
  std::string schedule_json;

  // Directory containing the scenario file (used to resolve CSV refs).
  std::string scenario_dir;

  // Load a scenario from a JSON file. Returns false on parse error
  // (with details printed to stderr).
  bool loadFromFile(const std::string &path);
};

} // namespace sim

#endif // _DIVERT_SIM_SIM_SCENARIO_H
