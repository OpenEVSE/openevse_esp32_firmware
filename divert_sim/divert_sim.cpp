#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/time.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>

#include "StdioSerial.h"
#include "RapiSender.h"
#include "openevse.h"
#include "divert.h"
#include "event.h"
#include "event_log.h"
#include "manual.h"
#include "current_shaper.h"
#include "loadsharing_algorithm.h"

#include "parser.hpp"
#include "cxxopts.hpp"

#include <ArduinoJson.h>
#include <MicroTasks.h>
#include <EpoxyFS.h>

#include <epoxy_test/ArduinoTest.h>

using namespace aria::csv;

EventLog eventLog;
EvseManager evse(RAPI_PORT, eventLog);
DivertTask divert(evse);
ManualOverride manual(evse);

long pilot = 32;                      // OpenEVSE Pilot Setting
long state = OPENEVSE_STATE_CONNECTED; // OpenEVSE State
double voltage = 240; // Voltage from OpenEVSE or MQTT

extern double smoothed_available_current;

int date_col = 0;
int grid_ie_col = -1;
int solar_col = -1;
int voltage_col = -1;
int live_power_col = -1;

time_t simulated_time = 0;
time_t last_time = 0;

bool kw = false;

time_t parse_date(const char *dateStr)
{
  int y = 2020, M = 1, d = 1, h = 0, m = 0, s = 0;
  char ampm[5];
  if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%dZ", &y, &M, &d, &h, &m, &s)) {
    if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%d+00:00", &y, &M, &d, &h, &m, &s)) {
      if(6 != sscanf(dateStr, "%d-%d-%d %d:%d:%d", &y, &M, &d, &h, &m, &s)) {
        if(6 != sscanf(dateStr, "%d/%d/%d %d:%d:%d", &d, &M, &y, &h, &m, &s)) {
          if(3 != sscanf(dateStr, "%d:%d %s", &h, &m, ampm)) {
            if(1 == sscanf(dateStr, "%d", &s)) {
              return s;
            }
          } else {
            y = 2020; M = 1; d = 1; s = 0;
            if(12 == h) {
              h -= 12;
            }
            if('P' == ampm[0]) {
              h += 12;
            }
          }
        }
      }
    }
  }

  tm time = {0};
  time.tm_year = y - 1900; // Year since 1900
  time.tm_mon = M - 1;     // 0-11
  time.tm_mday = d;        // 1-31
  time.tm_hour = h;        // 0-23
  time.tm_min = m;         // 0-59
  time.tm_sec = s;         // 0-61 (0-60 in C++11)

  return timegm(&time);
}

int get_watt(const char *val)
{
  float number = 0.0;
  if(1 != sscanf(val, "%f", &number)) {
    throw std::invalid_argument("Not a number");
  }

  if(kw) {
    number *= 1000;
  }

  return (int)round(number);
}

time_t divertmode_get_time()
{
  return simulated_time;
}

// ============================================================================
// Load sharing simulation
// ============================================================================

static const double TAPER_START_SOC = 80.0;
static const double TAPER_RANGE = 20.0;

struct PeerEvent {
  long time_sec;
  bool set_online;
  bool online;
  bool set_vehicle;
  bool vehicle;
};

struct SupplyEvent {
  long time_sec;
  double value;
};

struct PeerConfig {
  std::string id;
  double min_current;
  double max_current;
  double voltage;
  double max_charge_rate_kw;
  double battery_capacity_kwh;
  double initial_soc;
  int priority;
  bool initial_online;
  bool initial_vehicle;
  std::vector<PeerEvent> events;
};

struct PeerSimState {
  std::string id;
  bool online;
  bool vehicle_connected;
  double min_current;
  double max_current;
  double voltage;
  double max_charge_rate_kw;
  double battery_capacity_kwh;
  double soc;
  int priority;
  double allocated_current;
  double available_power_w;
  double actual_current;
  double actual_power_w;
  std::string reason;
  size_t next_event_index;
};

static std::vector<SupplyEvent> parse_supply_csv_events(const std::string &filename)
{
  std::vector<SupplyEvent> events;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: cannot open supply CSV " << filename << std::endl;
    return events;
  }

  CsvParser parser(file);
  parser.delimiter(',');
  int row_num = 0;
  for (auto& row : parser) {
    row_num++;
    if (row_num == 1) continue;

    SupplyEvent evt;
    evt.time_sec = 0;
    evt.value = 0;

    try {
      int col = 0;
      for (auto& field : row) {
        if (col == 0) {
          evt.time_sec = atol(field.c_str());
        } else if (col == 1) {
          float v = 0;
          sscanf(field.c_str(), "%f", &v);
          evt.value = v;
        }
        col++;
      }
      events.push_back(evt);
    } catch (...) {
      // skip unparseable rows
    }
  }
  std::sort(events.begin(), events.end(), [](const SupplyEvent &a, const SupplyEvent &b) {
    return a.time_sec < b.time_sec;
  });

  return events;
}

static double get_series_value(const std::vector<SupplyEvent> &events, long time_sec, double fallback)
{
  if (events.empty()) {
    return fallback;
  }

  double value = events.front().value;
  for (const auto &event : events) {
    if (event.time_sec <= time_sec) {
      value = event.value;
    } else {
      break;
    }
  }

  return value;
}

static int run_loadsharing_sim(const std::string &scenario_path)
{
  std::ifstream scenario_file(scenario_path);
  if (!scenario_file.is_open()) {
    std::cerr << "Error: cannot open scenario file " << scenario_path << std::endl;
    return EXIT_FAILURE;
  }
  std::stringstream scenario_buf;
  scenario_buf << scenario_file.rdbuf();
  std::string scenario_json = scenario_buf.str();

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, scenario_json);
  if (err) {
    std::cerr << "Error parsing scenario JSON: " << err.c_str() << std::endl;
    return EXIT_FAILURE;
  }

  // Parse group config
  JsonObject group = doc["group"];
  double safety_factor = group["safety_factor"] | 1.0;
  const char *failsafe_mode_str = group["failsafe_mode"] | "safe_current";
  String failsafe_mode(failsafe_mode_str);
  double failsafe_peer_assumed_current = group["failsafe_peer_assumed_current"] | 6.0;

  JsonObject simulation = doc["simulation"];
  long duration = simulation["duration"] | 3600;
  long tick_interval = simulation["tick_interval"] | 5;
  double nominal_voltage = simulation["nominal_voltage"] | 240.0;

  if (tick_interval <= 0) {
    tick_interval = 5;
  }

  JsonObject supply = doc["supply"];
  double max_pwr_w = supply["max_pwr"] | 0.0;
  if (max_pwr_w <= 0) {
    std::cerr << "Error: supply.max_pwr must be > 0" << std::endl;
    return EXIT_FAILURE;
  }

  double live_pwr_fixed_w = 0.0;
  std::vector<SupplyEvent> live_pwr_series;
  JsonVariant live_pwr_var = supply["live_pwr"];
  if (live_pwr_var.is<double>() || live_pwr_var.is<long>() || live_pwr_var.is<int>()) {
    live_pwr_fixed_w = live_pwr_var.as<double>();
  } else if (live_pwr_var.is<const char*>()) {
    live_pwr_series = parse_supply_csv_events(live_pwr_var.as<const char*>());
  } else if (live_pwr_var.is<JsonArray>()) {
    for (JsonObject e : live_pwr_var.as<JsonArray>()) {
      SupplyEvent event;
      event.time_sec = e["time"] | 0;
      event.value = e["value"] | 0.0;
      live_pwr_series.push_back(event);
    }
    std::sort(live_pwr_series.begin(), live_pwr_series.end(), [](const SupplyEvent &a, const SupplyEvent &b) {
      return a.time_sec < b.time_sec;
    });
  }

  std::vector<PeerConfig> peer_configs;
  JsonArray peers_json = doc["peers"];
  for (JsonObject p : peers_json) {
    PeerConfig pc;
    pc.id = p["id"] | "unknown";
    pc.min_current = p["min_current"] | 6.0;
    pc.max_current = p["max_current"] | 32.0;
    pc.voltage = p["voltage"] | 240.0;
    pc.priority = p["priority"] | 0;

    JsonObject ev = p["ev"];
    pc.battery_capacity_kwh = ev["battery_capacity_kwh"] | 75.0;
    pc.initial_soc = ev["initial_soc"] | 50.0;
    pc.max_charge_rate_kw = ev["max_charge_rate_kw"] | 7.2;

    JsonObject initial = p["initial"];
    pc.initial_online = initial["online"] | true;
    pc.initial_vehicle = initial["vehicle"] | false;

    JsonArray events = p["events"];
    for (JsonObject e : events) {
      PeerEvent event;
      event.time_sec = e["time"] | 0;
      event.set_online = e.containsKey("online");
      event.online = e["online"] | false;
      event.set_vehicle = e.containsKey("vehicle");
      event.vehicle = e["vehicle"] | false;
      pc.events.push_back(event);
    }

    std::sort(pc.events.begin(), pc.events.end(), [](const PeerEvent &a, const PeerEvent &b) {
      return a.time_sec < b.time_sec;
    });

    peer_configs.push_back(pc);
  }

  if (peer_configs.empty()) {
    std::cerr << "Error: no peers defined in scenario" << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<PeerSimState> peer_states(peer_configs.size());
  for (size_t i = 0; i < peer_configs.size(); i++) {
    peer_states[i].id = peer_configs[i].id;
    peer_states[i].online = peer_configs[i].initial_online;
    peer_states[i].vehicle_connected = peer_configs[i].initial_vehicle;
    peer_states[i].min_current = peer_configs[i].min_current;
    peer_states[i].max_current = peer_configs[i].max_current;
    peer_states[i].voltage = peer_configs[i].voltage;
    peer_states[i].max_charge_rate_kw = peer_configs[i].max_charge_rate_kw;
    peer_states[i].battery_capacity_kwh = peer_configs[i].battery_capacity_kwh;
    peer_states[i].soc = peer_configs[i].initial_soc;
    peer_states[i].priority = peer_configs[i].priority;
    peer_states[i].allocated_current = 0;
    peer_states[i].available_power_w = 0;
    peer_states[i].actual_current = 0;
    peer_states[i].actual_power_w = 0;
    peer_states[i].reason = "idle";
    peer_states[i].next_event_index = 0;
  }

  std::cout << "Time,Max_Pwr_W,Live_Pwr_W,Available_Pwr_W,Available_A";
  for (const auto &ps : peer_states) {
    std::cout << "," << ps.id << "_online"
              << "," << ps.id << "_vehicle"
              << "," << ps.id << "_soc"
              << "," << ps.id << "_allocated"
              << "," << ps.id << "_available_power_w"
              << "," << ps.id << "_actual"
              << "," << ps.id << "_actual_power_w"
              << "," << ps.id << "_reason";
  }
  std::cout << ",Total_Allocated,Total_Actual,Total_EV_Power_W,Total_Demand_W" << std::endl;

  for (long t = 0; t <= duration; t += tick_interval) {
    for (size_t i = 0; i < peer_states.size(); i++) {
      while (peer_states[i].next_event_index < peer_configs[i].events.size() &&
             peer_configs[i].events[peer_states[i].next_event_index].time_sec <= t) {
        const PeerEvent &event = peer_configs[i].events[peer_states[i].next_event_index];
        if (event.set_online) {
          peer_states[i].online = event.online;
        }
        if (event.set_vehicle) {
          peer_states[i].vehicle_connected = event.vehicle;
        }
        peer_states[i].next_event_index++;
      }
    }

    double live_pwr_w = get_series_value(live_pwr_series, t, live_pwr_fixed_w);
    if (live_pwr_w < 0) {
      live_pwr_w = 0;
    }
    double available_pwr_w = max_pwr_w - live_pwr_w;
    if (available_pwr_w < 0) {
      available_pwr_w = 0;
    }
    double available_current = available_pwr_w / nominal_voltage;

    std::vector<AllocationInput> inputs;
    for (const auto &ps : peer_states) {
      AllocationInput ai;
      ai.id = String(ps.id.c_str());
      ai.host = ai.id;
      ai.online = ps.online;
      ai.demanding = ps.online && ps.vehicle_connected && ps.soc < 100.0;
      ai.min_current = ps.min_current;
      ai.max_current = ps.max_current;
      ai.priority = ps.priority;
      inputs.push_back(ai);
    }

    bool failsafe_active = false;
    auto allocations = computeAllocations(inputs, available_current, safety_factor,
                                          failsafe_peer_assumed_current, failsafe_mode,
                                          failsafe_active);

    double total_allocated = 0;
    double total_actual = 0;
    double total_ev_power_w = 0;
    for (size_t i = 0; i < peer_states.size(); i++) {
      if (i < allocations.size()) {
        peer_states[i].allocated_current = allocations[i].getTargetCurrent();
        String reason_str = allocations[i].getReason();
        if (reason_str.length() > 0) {
          peer_states[i].reason = std::string(reason_str.c_str(), reason_str.length());
        } else {
          peer_states[i].reason = "idle";
        }
      } else {
        peer_states[i].allocated_current = 0;
        peer_states[i].reason = "error";
      }

      if (peer_states[i].online && peer_states[i].vehicle_connected &&
          peer_states[i].allocated_current > 0 && peer_states[i].soc < 100.0) {
        double offered_power_w = peer_states[i].allocated_current * peer_states[i].voltage;
        double actual_power_w = std::min(offered_power_w, peer_states[i].max_charge_rate_kw * 1000.0);

        if (peer_states[i].soc > TAPER_START_SOC) {
          double taper_factor = 1.0 -
                                ((peer_states[i].soc - TAPER_START_SOC) / TAPER_RANGE);
          taper_factor = std::max(0.0, taper_factor);
          actual_power_w *= taper_factor;
        }

        peer_states[i].available_power_w = offered_power_w;
        peer_states[i].actual_power_w = actual_power_w;
        peer_states[i].actual_current = actual_power_w / peer_states[i].voltage;

        double energy_added_kwh = ((actual_power_w / 1000.0) * tick_interval) / 3600.0;
        double soc_increase = (energy_added_kwh / peer_states[i].battery_capacity_kwh) * 100.0;
        peer_states[i].soc = std::min(100.0, peer_states[i].soc + soc_increase);

        if (peer_states[i].soc >= 100.0) {
          peer_states[i].actual_power_w = 0;
          peer_states[i].actual_current = 0;
          peer_states[i].reason = "idle";
        }
      } else {
        peer_states[i].available_power_w = 0;
        peer_states[i].actual_power_w = 0;
        peer_states[i].actual_current = 0;
      }

      total_allocated += peer_states[i].allocated_current;
      total_actual += peer_states[i].actual_current;
      total_ev_power_w += peer_states[i].actual_power_w;
    }
    double total_demand_w = live_pwr_w + total_ev_power_w;

    std::cout << t
              << "," << max_pwr_w
              << "," << live_pwr_w
              << "," << available_pwr_w
              << "," << available_current;
    for (const auto &ps : peer_states) {
      std::cout << "," << (ps.online ? 1 : 0)
                << "," << (ps.vehicle_connected ? 1 : 0)
                << "," << ps.soc
                << "," << ps.allocated_current
                << "," << ps.available_power_w
                << "," << ps.actual_current
                << "," << ps.actual_power_w
                << "," << ps.reason;
    }
    std::cout << "," << total_allocated
              << "," << total_actual
              << "," << total_ev_power_w
              << "," << total_demand_w
              << std::endl;
  }

  return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
  int voltage_arg = -1;
  std::string sep = ",";
  std::string config;
  std::string scenario;

  cxxopts::Options options(argv[0], " - example command line options");
  options
    .positional_help("[optional args]")
    .show_positional_help();

  options
    .add_options()
    ("help", "Print help")
    ("d,date", "The date column", cxxopts::value<int>(date_col), "N")
    ("s,solar", "The solar column", cxxopts::value<int>(solar_col), "N")
    ("g,gridie", "The Grid IE column", cxxopts::value<int>(grid_ie_col), "N")
    ("l,livepwr", "The live power column", cxxopts::value<int>(live_power_col), "N")
    ("c,config", "Config options, either a file name or JSON", cxxopts::value<std::string>(config))
    ("v,voltage", "The Voltage column if < 50, else the fixed voltage", cxxopts::value<int>(voltage_arg), "N")
    ("kw", "values are KW")
    ("sep", "Field separator", cxxopts::value<std::string>(sep))
    ("config-check", "Output the config and exit")
    ("config-load", "Simulate loading config from EEPROM")
    ("config-commit", "Simulate saving the config to EEPROM")
    ("loadsharing", "Run load sharing simulation mode")
    ("scenario", "Load sharing scenario JSON file", cxxopts::value<std::string>(scenario));

  auto result = options.parse(argc, argv);

  if (result.count("help"))
  {
    std::cout << options.help({"", "Group"}) << std::endl;
    exit(0);
  }

  // Load sharing mode: runs a standalone simulation and exits
  if (result.count("loadsharing"))
  {
    if (scenario.empty()) {
      std::cerr << "Error: --loadsharing requires --scenario <file>" << std::endl;
      return EXIT_FAILURE;
    }
    int rc = run_loadsharing_sim(scenario);
    // Use _exit() to skip global destructors — the global EvseManager
    // was never initialized in loadsharing mode and its destructor
    // (EnergyMeter::save) crashes trying to write to uninitialised FS.
    std::cout.flush();
    _exit(rc);
  }

  EpoxyTest::set_millis(millis());

  fs::EpoxyFS.begin();
  if(result.count("config-load") > 0) {
    config_load_settings();
  } else {
    config_reset();
  }

  // If config is set and not a JSON string, assume it is a file name
  if(config.length() > 0 && config[0] != '{')
  {
    std::ifstream t(config);
    std::stringstream buffer;
    buffer << t.rdbuf();
    config = buffer.str();
  }
  // If we have some JSON load it
  if(config.length() > 0 && config[0] == '{') {
    config_deserialize(config.c_str());
  }

  if(result.count("config-commit") > 0) {
    config_commit();
  }

  if(result.count("config-check"))
  {
    String config_out;
    config_serialize(config_out, true, false, false);
    std::cout << config_out.c_str() << std::endl;
    return EXIT_SUCCESS;
  }

  kw = result.count("kw") > 0;

  divert_type = grid_ie_col >= 0 ? 1 : 0;

  if(voltage_arg >= 0) {
    if(voltage_arg < 50) {
      voltage_col = voltage_arg;
    } else {
      voltage = voltage_arg;
    }
  }

  solar = 0;
  grid_ie = 0;

  evse.begin();
  divert.begin();
  shaper.begin(evse);

  // Initialise the EVSE Manager
  while (!evse.isConnected()) {
    MicroTask.update();
  }

  if(solar_col >= 0 || grid_ie_col >= 0) {
    divert.setMode(DivertMode::Eco);
  }
  if(live_power_col >= 0) {
    shaper.setState(true);
  }

  CsvParser parser(std::cin);
  parser.delimiter(sep.c_str()[0]);
  int row_number = 0;

  std::cout << "Date,Solar,Grid IE,Pilot,Charge Power,Min Charge Power,State,Smoothed Available,Live Power,Smoother Live Power,Shaper Max Power" << std::endl;
  for (auto& row : parser)
  {
    try
    {
      int col = 0;
      std::string val;

      for (auto& field : row)
      {
        val = field;
        if(date_col == col) {
          simulated_time = parse_date(val.c_str());
        } else if (grid_ie_col == col) {
          grid_ie = get_watt(val.c_str());
        } else if (solar_col == col) {
          solar = get_watt(val.c_str());
        } else if (live_power_col == col) {
          shaper.setLivePwr(get_watt(val.c_str()));
        } else if (voltage_col == col) {
          voltage = stoi(field);
        }

        col++;
      }

      if(last_time != 0)
      {
        int delta = simulated_time - last_time;
        if(delta > 0) {
          EpoxyTest::add_millis(delta * 1000);
          DBUGVAR(millis());
        }
      }
      last_time = simulated_time;

      divert.update_state();
      MicroTask.update();

      tm tm;
      gmtime_r(&simulated_time, &tm);

      char buffer[32];
      std::strftime(buffer, 32, "%d/%m/%Y %H:%M:%S", &tm);

      int ev_pilot = (OPENEVSE_STATE_CHARGING == state ? pilot : 0);
      int ev_watt = ev_pilot * voltage;
      int min_ev_watt = 6 * voltage;

      double smoothed = divert.smoothedAvailableCurrent() * voltage;

      int live_power = shaper.getLivePwr();
      int smoothed_live_pwr = shaper.getSmoothedLivePwr();
      int shaper_max_power = shaper.getMaxPwr();

      std::cout << buffer << "," << solar << "," << grid_ie << "," << ev_pilot << "," << ev_watt << "," << min_ev_watt << "," << state << "," << smoothed << "," << live_power << "," << smoothed_live_pwr << "," << shaper_max_power << std::endl;
    }
    catch(const std::invalid_argument& e)
    {
    }
  }
}

void event_send(String event)
{
}

void event_send(JsonDocument &event)
{
}

void emoncms_publish(JsonDocument &data)
{
}
