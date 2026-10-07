#include "scenario.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

#include <ArduinoJson.h>

#include "evse_man.h"

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

std::string toJson(JsonVariantConst v)
{
  std::stringstream out;
  serializeJson(v, out);
  return out.str();
}

} // namespace

bool lookupClient(const std::string &name, uint32_t &client, int &priority)
{
  struct Entry { const char *name; uint32_t client; int priority; };
  static const Entry entries[] = {
    { "manual",       EvseClient_OpenEVSE_Manual,       EvseManager_Priority_Manual },
    { "ocpp",         EvseClient_OpenEVSE_OCPP,         EvseManager_Priority_OCPP },
    { "mqtt",         EvseClient_OpenEVSE_MQTT,         EvseManager_Priority_MQTT },
    { "evcc",         EvseClient_evcc,                  EvseManager_Priority_API },
    { "demandshaper", EvseClient_OpenEnergyMonitor_DemandShaper, EvseManager_Priority_API },
    { "divert",       EvseClient_OpenEVSE_Divert,       EvseManager_Priority_Divert },
    { "boost",        EvseClient_OpenEVSE_Boost,        EvseManager_Priority_Boost },
    { "schedule",     EvseClient_OpenEVSE_Schedule,     EvseManager_Priority_Timer },
    { "limit",        EvseClient_OpenEVSE_Limit,        EvseManager_Priority_Limit },
    { "rfid",         EvseClient_OpenEVSE_RFID,         EvseManager_Priority_RFID },
    { "shaper",       EvseClient_OpenEVSE_Shaper,       EvseManager_Priority_Safety },
    { "temp_throttle", EvseClient_OpenEVSE_TempThrottle, EvseManager_Priority_Safety },
    { "loadsharing",  EvseClient_OpenEVSE_LoadSharing,  EvseManager_Priority_Safety },
  };
  for (const auto &e : entries) {
    if (name == e.name) {
      client = e.client;
      priority = e.priority;
      return true;
    }
  }
  // Numeric client id (as reported by /claims) — default to API priority.
  if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
    client = (uint32_t) std::strtoul(name.c_str(), nullptr, 10);
    priority = EvseManager_Priority_API;
    return true;
  }
  return false;
}

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

  // Replay scenarios inline thousands of samples; size the document to the
  // file rather than a fixed budget.
  DynamicJsonDocument doc(64 * 1024 + text.size() * 4);
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
    max_current_soft = root["config"]["max_current_soft"] | -1L;
  }

  if (root.containsKey("schedule")) {
    schedule_json = toJson(root["schedule"]);
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
    group.failsafe_safe_current = grp["failsafe_safe_current"] | 6.0;
    group.rotation_interval = grp["rotation_interval"] | 1800;
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
      p.initial_request_current = ev["request_current"] | true;
      p.initial_aux_load_kw = ev["aux_load_kw"] | 0.0;
      p.report_soc = ev["report_soc"] | false;
    }
    p.rfid_reader = pj["rfid_reader"] | true;
    p.live_pwr_add_ev = pj["live_pwr_add_ev"] | false;
    if (pj.containsKey("schedule")) {
      p.schedule_json = toJson(pj["schedule"]);
    }

    JsonObjectConst init = pj["initial"].as<JsonObjectConst>();
    if (!init.isNull()) {
      p.initial_online = init["online"] | true;
      p.initial_vehicle = init["vehicle"] | true;
    }

    JsonObjectConst inputs = pj["inputs"].as<JsonObjectConst>();
    if (!inputs.isNull()) {
      // solar and grid_ie may both be present when the same CSV provides both
      // columns (e.g. day1/2/3_grid_ie.csv col1=solar, col2=grid_ie).
      // DivertTask reads them independently, so both inputs are applied each tick.

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
      if (inputs.containsKey("temperature")) {
        if (!p.temperature.loadFromJson(inputs["temperature"],
                                        scenario_dir,
                                        (long) start_epoch,
                                        duration_sec)) {
          std::cerr << "Scenario: invalid inputs.temperature for peer " << p.id << std::endl;
          return false;
        }
      }
      if (inputs.containsKey("vrms")) {
        if (!p.vrms.loadFromJson(inputs["vrms"],
                                scenario_dir,
                                (long) start_epoch,
                                duration_sec)) {
          std::cerr << "Scenario: invalid inputs.vrms for peer " << p.id << std::endl;
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
        if (ej.containsKey("request_current")) {
          e.set_request_current = true; e.request_current = ej["request_current"].as<bool>();
        }
        if (ej.containsKey("aux_load_kw")) {
          e.set_aux_load_kw = true; e.aux_load_kw = ej["aux_load_kw"].as<double>();
        }
        if (ej.containsKey("boost")) {
          e.set_boost = true;
          if (ej["boost"].is<const char *>()) {
            e.boost_cancel = (std::string(ej["boost"].as<const char *>()) == "cancel");
          } else {
            JsonObjectConst bj = ej["boost"].as<JsonObjectConst>();
            e.boost_type = bj["type"] | "";
            e.boost_value = bj["value"] | 0;
          }
        }
        // A non-string "manual" (null, number, object) still satisfies
        // containsKey() but yields NULL from as<const char *>(), which is UB
        // to feed to std::string. Ignore the event instead.
        if (ej.containsKey("manual") && ej["manual"].is<const char *>()) {
          const char *ms = ej["manual"].as<const char *>();
          if (ms) {
            e.set_manual = true;
            e.manual_state = ms;
          }
        }
        if (ej.containsKey("rfid") && ej["rfid"].is<const char *>()) {
          e.set_rfid = true;
          e.rfid_tag = ej["rfid"].as<const char *>();
        }
        if (ej.containsKey("temperature")) {
          e.set_temperature = true;
          e.temperature = ej["temperature"].as<double>();
        }
        if (ej.containsKey("soc")) {
          e.set_soc = true;
          e.soc = ej["soc"].as<double>();
        }
        if (ej.containsKey("claim")) {
          JsonObjectConst cj = ej["claim"].as<JsonObjectConst>();
          std::string name = cj["client"] | "";
          if (cj["client"].is<long>()) name = std::to_string(cj["client"].as<long>());
          if (!lookupClient(name, e.claim_client, e.claim_priority)) {
            std::cerr << "Scenario: unknown claim client '" << name << "'" << std::endl;
            return false;
          }
          e.set_claim = true;
          e.claim_priority = cj["priority"] | e.claim_priority;
          e.claim_state = cj["state"] | "";
          e.claim_charge_current = cj["charge_current"] | -1L;
          e.claim_max_current = cj["max_current"] | -1L;
          e.claim_auto_release = cj["auto_release"] | false;
        }
        if (ej.containsKey("release")) {
          std::string name = ej["release"] | "";
          if (ej["release"].is<long>()) name = std::to_string(ej["release"].as<long>());
          int unused_priority;
          if (!lookupClient(name, e.release_client, unused_priority)) {
            std::cerr << "Scenario: unknown release client '" << name << "'" << std::endl;
            return false;
          }
          e.set_release = true;
        }
        if (ej.containsKey("limit")) {
          e.set_limit = true;
          if (ej["limit"].is<const char *>()) {
            e.limit_clear = true;
          } else {
            JsonObjectConst lj = ej["limit"].as<JsonObjectConst>();
            e.limit_type = lj["type"] | "none";
            e.limit_value = lj["value"] | 0;
            e.limit_auto_release = lj["auto_release"] | true;
          }
        }
        if (ej.containsKey("schedule")) {
          e.set_schedule = true;
          e.schedule_json = toJson(ej["schedule"]);
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
