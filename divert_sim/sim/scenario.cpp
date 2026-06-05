#include "scenario.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

#include <ArduinoJson.h>

namespace sim {

namespace {

std::string dirname_of(const std::string &path)
{
  auto pos = path.find_last_of('/');
  return (pos == std::string::npos) ? std::string(".") : path.substr(0, pos);
}

std::time_t parseEpoch(const std::string &s)
{
  if (s.empty()) return 0;
  // Try plain epoch
  bool digits = true;
  for (char c : s) if (c != '-' && (c < '0' || c > '9')) { digits = false; break; }
  if (digits) return (std::time_t) std::atol(s.c_str());

  int y, M, d, h, mi, sec;
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &M, &d, &h, &mi, &sec) == 6) {
    struct tm t = {};
    t.tm_year = y - 1900; t.tm_mon = M - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = sec;
    return timegm(&t);
  }
  return 0;
}

} // namespace

bool Scenario::loadFromFile(const std::string &path)
{
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Scenario: cannot open " << path << std::endl;
    return false;
  }

  scenario_dir = dirname_of(path);

  std::stringstream buf;
  buf << file.rdbuf();
  std::string text = buf.str();

  DynamicJsonDocument doc(64 * 1024);
  DeserializationError err = deserializeJson(doc, text);
  if (err) {
    std::cerr << "Scenario: JSON parse error: " << err.c_str() << std::endl;
    return false;
  }

  JsonObjectConst root = doc.as<JsonObjectConst>();
  JsonObjectConst sim = root["simulation"].as<JsonObjectConst>();
  if (!sim.isNull()) {
    duration_sec = sim["duration"] | duration_sec;
    tick_interval_sec = sim["tick_interval"] | tick_interval_sec;
    nominal_voltage = sim["nominal_voltage"] | nominal_voltage;
    if (sim.containsKey("start_time")) {
      start_epoch = parseEpoch(sim["start_time"].as<const char *>());
    }
  }

  if (root.containsKey("config")) {
    // Re-serialise so we can hand it to config_deserialize.
    std::stringstream cfg;
    serializeJson(root["config"], cfg);
    config_json = cfg.str();
  }

  JsonObjectConst grp = root["group"].as<JsonObjectConst>();
  if (!grp.isNull()) {
    group.enabled = grp["enabled"] | false;
    group.max_current = grp["max_current"] | 0.0;
    group.safety_factor = grp["safety_factor"] | 1.0;
    if (grp.containsKey("failsafe_mode")) {
      group.failsafe_mode = grp["failsafe_mode"].as<const char *>();
    }
    group.failsafe_peer_assumed_current =
        grp["failsafe_peer_assumed_current"] | 6.0;
  }

  // Backward-compat: old scenario files use a top-level "supply" object with
  // "max_pwr" in watts and no "group.enabled" or "group.max_current".
  JsonObjectConst supply = root["supply"].as<JsonObjectConst>();
  if (!supply.isNull()) {
    supply_max_pwr_w = supply["max_pwr"] | 0.0;
    if (supply_max_pwr_w > 0 && group.max_current == 0.0) {
      group.max_current = supply_max_pwr_w / nominal_voltage;
      group.enabled = true;
    }
    if (supply.containsKey("live_pwr")) {
      if (!supply_live_pwr.loadFromJson(supply["live_pwr"],
                                        scenario_dir,
                                        (long) start_epoch,
                                        duration_sec)) {
        std::cerr << "Scenario: invalid supply.live_pwr" << std::endl;
        return false;
      }
    }
  }
  // If group section exists with max_current > 0, treat as enabled unless
  // explicitly set to false.
  if (!grp.isNull() && group.max_current > 0.0 && !grp.containsKey("enabled")) {
    group.enabled = true;
  }

  JsonArrayConst peerArr = root["peers"].as<JsonArrayConst>();
  if (peerArr.isNull() || peerArr.size() == 0) {
    std::cerr << "Scenario: no peers defined" << std::endl;
    return false;
  }

  int idx = 0;
  for (JsonObjectConst pj : peerArr) {
    PeerScenario p;
    p.id = pj["id"] | (std::string("evse-") + std::to_string(idx)).c_str();
    p.voltage = pj["voltage"] | 240.0;
    p.min_current = pj["min_current"] | 6.0;
    p.max_current = pj["max_current"] | 32.0;
    p.priority = pj["priority"] | 0;

    JsonObjectConst ev = pj["ev"].as<JsonObjectConst>();
    if (!ev.isNull()) {
      p.battery_capacity_kwh = ev["battery_capacity_kwh"] | 75.0;
      p.initial_soc = ev["initial_soc"] | 50.0;
      p.max_charge_rate_kw = ev["max_charge_rate_kw"] | 7.2;
    }

    JsonObjectConst init = pj["initial"].as<JsonObjectConst>();
    if (!init.isNull()) {
      p.initial_online = init["online"] | true;
      p.initial_vehicle = init["vehicle"] | true;
    }

    JsonObjectConst inputs = pj["inputs"].as<JsonObjectConst>();
    if (!inputs.isNull()) {
      if (inputs.containsKey("solar") && inputs.containsKey("grid_ie")) {
        std::cerr << "Scenario: inputs.solar and inputs.grid_ie are mutually exclusive for peer "
                  << p.id << std::endl;
        return false;
      }

      if (inputs.containsKey("solar")) {
        if (!p.solar.loadFromJson(inputs["solar"],
                                 scenario_dir,
                                 (long) start_epoch,
                                 duration_sec)) {
          std::cerr << "Scenario: invalid inputs.solar for peer " << p.id << std::endl;
          return false;
        }
      }
      if (inputs.containsKey("grid_ie")) {
        if (!p.grid_ie.loadFromJson(inputs["grid_ie"],
                                   scenario_dir,
                                   (long) start_epoch,
                                   duration_sec)) {
          std::cerr << "Scenario: invalid inputs.grid_ie for peer " << p.id << std::endl;
          return false;
        }
      }
      if (inputs.containsKey("live_pwr")) {
        if (!p.live_pwr.loadFromJson(inputs["live_pwr"],
                                    scenario_dir,
                                    (long) start_epoch,
                                    duration_sec)) {
          std::cerr << "Scenario: invalid inputs.live_pwr for peer " << p.id << std::endl;
          return false;
        }
      }
    }

    if (pj.containsKey("divert_mode")) {
      p.divert_mode = pj["divert_mode"].as<const char *>();
    }
    if (pj.containsKey("shaper_enabled")) {
      p.shaper_enabled = pj["shaper_enabled"].as<bool>();
      p.shaper_enabled_set = true;
    }

    JsonArrayConst events = pj["events"].as<JsonArrayConst>();
    if (!events.isNull()) {
      for (JsonObjectConst ej : events) {
        PeerEvent e;
        e.t_sec = ej["time"] | 0;
        if (ej.containsKey("online")) {
          e.set_online = true; e.online = ej["online"].as<bool>();
        }
        if (ej.containsKey("vehicle")) {
          e.set_vehicle = true; e.vehicle = ej["vehicle"].as<bool>();
        }
        p.events.push_back(e);
      }
    }

    peers.push_back(std::move(p));
    idx++;
  }

  return true;
}

} // namespace sim
