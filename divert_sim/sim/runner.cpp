#include "runner.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include <epoxy_test/ArduinoTest.h>

#include "app_config.h"
#include "loadsharing_algorithm.h"
#include "openevse.h"

#include "csv_writer.h"
#include "peer.h"
#include "scenario.h"

// Global EventLog defined in divert_sim.cpp; reused by all peers since the
// logging path is identical for each.
extern EventLog eventLog;
extern time_t simulated_time;

namespace sim {

namespace {

std::string formatTime(std::time_t epoch)
{
  std::tm tm = *std::gmtime(&epoch);
  std::ostringstream s;
  s << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return s.str();
}

const char *stateName(long s)
{
  switch (s) {
    case OPENEVSE_STATE_NOT_CONNECTED: return "ready";
    case OPENEVSE_STATE_CONNECTED:     return "connected";
    case OPENEVSE_STATE_CHARGING:      return "charging";
    case OPENEVSE_STATE_VENT_REQUIRED: return "vent_required";
    case OPENEVSE_STATE_DISABLED:      return "disabled";
    case OPENEVSE_STATE_SLEEPING:      return "sleeping";
    case OPENEVSE_STATE_GFI_FAULT:     return "gfi_fault";
    default:                           return "unknown";
  }
}

bool isStateActive(long s)
{
  return s == OPENEVSE_STATE_CHARGING;
}

const char *clientName(EvseClient client)
{
  switch (client) {
    case EvseClient_OpenEVSE_Divert:      return "divert";
    case EvseClient_OpenEVSE_Shaper:      return "shaper";
    case EvseClient_OpenEVSE_LoadSharing: return "loadsharing";
    case EvseClient_OpenEVSE_Manual:      return "manual";
    case EvseClient_OpenEVSE_Schedule:    return "schedule";
    case EvseClient_OpenEVSE_Limit:       return "limit";
    default:                              return "client";
  }
}

std::string claimState(JsonObjectConst claim)
{
  const char *state = claim["state"] | "";
  if (std::string(state) == "disabled") return "disabled";
  if (std::string(state) == "active") return "active";
  if (claim.containsKey("max_current") || claim.containsKey("charge_current")) return "other";
  return "none";
}

std::string formatClaimDetails(EvseManager &evse, std::string &aggregate_state)
{
  DynamicJsonDocument claims_doc(1024);
  evse.serializeClaims(claims_doc);
  JsonArrayConst claims = claims_doc.as<JsonArrayConst>();
  if (claims.isNull() || claims.size() == 0) {
    aggregate_state = "none";
    return "No active claims";
  }

  DynamicJsonDocument target_doc(512);
  evse.serializeTarget(target_doc);
  JsonObjectConst winners = target_doc["claims"].as<JsonObjectConst>();

  bool any_active = false;
  bool any_disabled = false;
  bool any_other = false;
  std::ostringstream details;
  bool first = true;
  for (JsonObjectConst claim : claims) {
    EvseClient client = claim["client"] | EvseClient_NULL;
    std::string state = claimState(claim);
    any_active = any_active || state == "active";
    any_disabled = any_disabled || state == "disabled";
    any_other = any_other || state == "other";

    if (!first) details << " | ";
    first = false;
    details << clientName(client) << '@' << (int)(claim["priority"] | 0) << ':' << state;
    if (claim.containsKey("charge_current")) details << " charge_current=" << (uint32_t) claim["charge_current"];
    if (claim.containsKey("max_current")) details << " max_current=" << (uint32_t) claim["max_current"];
  }

  if (!winners.isNull() && winners.size() > 0) {
    details << " ; wins";
    for (JsonPairConst winner : winners) {
      EvseClient client = winner.value().as<EvseClient>();
      details << ' ' << winner.key().c_str() << '=' << clientName(client);
    }
  }

  aggregate_state = any_disabled ? "disabled" : (any_active ? "active" : (any_other ? "other" : "none"));
  return details.str();
}

} // namespace

int run(const std::string &scenario_path,
        const std::string &output_path,
        bool config_check)
{
  Scenario scenario;
  if (!scenario.loadFromFile(scenario_path)) {
    return 2;
  }

  // Apply scenario config (single global config — applies to all peers).
  if (!scenario.config_json.empty()) {
    String cfg(scenario.config_json.c_str());
    if (!config_deserialize(cfg)) {
      std::cerr << "Runner: failed to apply scenario config" << std::endl;
      return 2;
    }
  }

  if (config_check) {
    String dump;
    config_serialize(dump, true, true, true);
    std::cout << dump.c_str() << std::endl;
    return 0;
  }

  // Build peers
  std::vector<std::unique_ptr<Peer>> peers;
  peers.reserve(scenario.peers.size());
  std::vector<std::string> peer_ids;
  peer_ids.reserve(scenario.peers.size());

  for (const auto &ps : scenario.peers) {
    auto p = std::make_unique<Peer>(ps, eventLog);
    p->begin();
    peer_ids.push_back(p->id());
    peers.push_back(std::move(p));
  }

  CsvWriter writer;
  if (!writer.open(output_path)) return 2;
  writer.writeHeader(peer_ids);

  std::time_t t_start = scenario.start_epoch != 0
                          ? scenario.start_epoch
                          : std::time(nullptr);

  // Run the simulation loop.
  long t_sec = 0;
  long tick = scenario.tick_interval_sec > 0 ? scenario.tick_interval_sec : 5;
  unsigned long fake_millis = 0;
  EpoxyTest::set_millis(fake_millis);

  bool failsafe_active = false;
  LoadSharingRotationState rotationState;
  long last_allocation_sec = -5;

  while (t_sec <= scenario.duration_sec) {
    // Keep divert's time source aligned with simulation time so minimum
    // charge duration logic can expire as expected.
    simulated_time = t_start + t_sec;

    double group_max_current = scenario.group.max_current;
    if (scenario.supply_max_pwr_w > 0.0 && !scenario.supply_live_pwr.empty()) {
      double site_live_pwr_w = scenario.supply_live_pwr.valueAt(t_sec);
      double available_w = scenario.supply_max_pwr_w - site_live_pwr_w;
      if (available_w < 0.0) available_w = 0.0;
      group_max_current = available_w / scenario.nominal_voltage;
    }

    // 1. Apply scheduled events and time-series inputs to each peer.
    for (auto &p : peers) {
      p->applyEvents(t_sec);
      p->applyInputs(t_sec);
    }

    // 2. Compute load-share allocations and apply them as a max constraint
    //    on each peer's EvseManager.
    if (scenario.group.enabled && !peers.empty() &&
        t_sec - last_allocation_sec >= 5) {
      last_allocation_sec = t_sec;
      std::vector<AllocationInput> inputs;
      inputs.reserve(peers.size());
      size_t demanding_count = 0;
      for (auto &p : peers) {
        if (p->vehicle && p->online) {
          demanding_count++;
        }
      }
      for (auto &p : peers) {
        AllocationInput in;
        in.id = p->id().c_str();
        in.host = in.id;
        in.online = p->online;
        in.demanding = p->vehicle && p->online;
        in.charging = p->vehicle && p->online &&
                      (p->simEvse().state == OPENEVSE_STATE_CHARGING ||
                       p->reason == "insufficient");
        in.min_current = p->scenario().min_current;
        in.max_current = p->scenario().max_current;
        if (demanding_count > 1) {
          in.max_current = applyLoadSharingDemandCap(
            p->demand_state,
            in.max_current,
            p->simEvse().actualCurrent(),
            p->simEvse().pilot,
            p->loadshare_allocation_amps,
            in.min_current,
            in.charging,
            in.demanding);
        }
        in.priority = p->scenario().priority;
        inputs.push_back(in);
      }
      auto allocs = computeAllocations(
          inputs,
          group_max_current,
          scenario.group.safety_factor,
          scenario.group.failsafe_peer_assumed_current,
          String(scenario.group.failsafe_mode.c_str()),
          failsafe_active,
          rotationState,
          (unsigned long) t_sec * 1000UL,
          (unsigned long) scenario.group.rotation_interval * 1000UL);

      for (const auto &a : allocs) {
        for (auto &p : peers) {
          if (a.getId() == p->id().c_str()) {
            p->loadshare_allocation_amps = a.getTargetCurrent();
            p->reason = a.getReason().c_str();
            p->shaper().setLoadSharingLimit(
              a.getTargetCurrent(), a.getReason() == "failsafe_disabled");
            break;
          }
        }
      }
    }

    // 3. Advance the firmware task scheduler. Run several iterations so
    //    callbacks chained across multiple loops settle within one tick.
    for (int i = 0; i < 5; i++) {
      MicroTask.update();
    }

    // 4. Update the EV battery model based on the resulting pilot.
    for (auto &p : peers) {
      p->updateBattery((double) tick);
    }

    // 5. Emit a CSV row.
    writer.beginRow(formatTime(t_start + t_sec));
    double group_total_actual_w = 0;
    double group_total_demand_w = 0;
    for (auto &p : peers) {
      const SimEvse &s = p->simEvse();
      double pilot_w = s.pilot * s.voltage;
      double charge_available_w = isStateActive(s.state) ? pilot_w : 0.0;
      double actual_a = s.actualCurrent();
      double actual_w = actual_a * s.voltage;
      double ev_max_w = s.max_charge_rate_kw * 1000.0;
      group_total_actual_w += actual_w;
      group_total_demand_w += charge_available_w;

      writer.addBool(p->online);
      writer.addBool(p->vehicle);
      writer.addDouble(p->last_solar_w, 1);
      writer.addDouble(p->last_grid_ie_w, 1);
      writer.addDouble(p->last_live_pwr_w, 1);
      writer.addDouble(p->divert().smoothedAvailableCurrent() * s.voltage, 1);
      writer.addDouble(p->shaper().getMaxCur() * s.voltage, 1);
      writer.addDouble((double) p->shaper().getSmoothedLivePwr(), 1);
      writer.addDouble(p->loadshare_allocation_amps * s.voltage, 1);
      writer.addDouble(pilot_w, 1);
      writer.addDouble(charge_available_w, 1);
      writer.addString(stateName(s.state));
      writer.addDouble(ev_max_w, 1);
      writer.addDouble(actual_w, 1);
      writer.addDouble(s.soc, 2);
      std::string claim_state;
      std::string claim_details = formatClaimDetails(p->evse(), claim_state);
      writer.addString(claim_state);
      writer.addString(claim_details);
      writer.addString(p->reason);
    }
    writer.addDouble(group_max_current * scenario.nominal_voltage, 1);
    writer.addDouble(group_total_actual_w, 1);
    writer.addDouble(group_total_demand_w, 1);
    writer.addBool(failsafe_active);
    writer.endRow();

    fake_millis += (unsigned long) tick * 1000UL;
    EpoxyTest::set_millis(fake_millis);
    t_sec += tick;
  }

  writer.close();
  return 0;
}

} // namespace sim
