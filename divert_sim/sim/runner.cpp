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

  while (t_sec <= scenario.duration_sec) {
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
    if (scenario.group.enabled && !peers.empty()) {
      std::vector<AllocationInput> inputs;
      inputs.reserve(peers.size());
      for (auto &p : peers) {
        AllocationInput in;
        in.id = p->id().c_str();
        in.host = in.id;
        in.online = p->online;
        in.demanding = p->vehicle && p->online;
        in.min_current = p->scenario().min_current;
        in.max_current = p->scenario().max_current;
        in.priority = p->scenario().priority;
        inputs.push_back(in);
      }
      auto allocs = computeAllocations(
          inputs,
          group_max_current,
          scenario.group.safety_factor,
          scenario.group.failsafe_peer_assumed_current,
          String(scenario.group.failsafe_mode.c_str()),
          failsafe_active);

      for (const auto &a : allocs) {
        for (auto &p : peers) {
          if (a.getId() == p->id().c_str()) {
            p->loadshare_allocation_amps = a.getTargetCurrent();
            p->reason = a.getReason().c_str();
            // Apply allocation as a claim on the EvseManager.
            EvseProperties props;
            props.setState(a.getTargetCurrent() > 0 ? EvseState::Active
                                                   : EvseState::Disabled);
            props.setMaxCurrent((uint32_t) a.getTargetCurrent());
            p->evse().claim(EvseClient_OpenEVSE_LoadSharing,
                            EvseManager_Priority_Default, props);
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
