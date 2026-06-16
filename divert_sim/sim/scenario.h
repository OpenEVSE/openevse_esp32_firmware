#ifndef _DIVERT_SIM_SIM_SCENARIO_H
#define _DIVERT_SIM_SIM_SCENARIO_H

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
};

struct PeerScenario
{
  std::string id;
  double voltage = 240.0;
  double min_current = 6.0;
  double max_current = 32.0;

  // EV battery model
  double battery_capacity_kwh = 75.0;
  double initial_soc = 50.0;
  double max_charge_rate_kw = 7.2;

  // Initial state
  bool initial_online = true;
  bool initial_vehicle = true;

  // Time-series inputs
  TimeSeries solar;
  TimeSeries grid_ie;
  TimeSeries live_pwr;

  // Optional per-peer override of divert mode: "off" (default), "solar", "grid".
  // When unset, divert is enabled if the peer has any solar/grid_ie data.
  std::string divert_mode;

  // Optional per-peer override of shaper enable (defaults to true if live_pwr
  // has data, false otherwise).
  bool shaper_enabled = false;
  bool shaper_enabled_set = false;

  std::vector<PeerEvent> events;
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

  std::vector<PeerScenario> peers;

  // Directory containing the scenario file (used to resolve CSV refs).
  std::string scenario_dir;

  // Load a scenario from a JSON file. Returns false on parse error
  // (with details printed to stderr).
  bool loadFromFile(const std::string &path);
};

} // namespace sim

#endif // _DIVERT_SIM_SIM_SCENARIO_H
