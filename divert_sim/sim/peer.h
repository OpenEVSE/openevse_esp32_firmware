#ifndef _DIVERT_SIM_SIM_PEER_H
#define _DIVERT_SIM_SIM_PEER_H

#include <memory>
#include <string>

#include <Arduino.h>

#include "evse_man.h"
#include "divert.h"
#include "current_shaper.h"
#include "manual.h"
#include "event_log.h"

#include "scenario.h"
#include "sim_evse.h"
#include "sim_stream.h"

namespace sim {

// A simulated charge point: per-peer SimEvse + SimStream + EvseManager +
// DivertTask + CurrentShaperTask + scenario reference.
//
// All firmware modules are owned (not pointers to globals) so per-peer state
// is independent. The global `evse`/`divert`/`shaper` symbols defined in
// `divert_sim.cpp` are linkage stubs used only by the firmware's logging
// path inside `evse_man.cpp` and are not driven by the runner.
class Peer
{
public:
  Peer(const PeerScenario &scenario, EventLog &eventLog);
  ~Peer();

  void begin();

  // Read the per-peer time-series at scenario time `t_sec` and push the
  // values into divert/shaper.
  void applyInputs(long t_sec);

  // Apply any events scheduled at `t_sec` (online / vehicle changes).
  void applyEvents(long t_sec);

  // Advance the per-peer EV battery model by `dt_seconds` and update the
  // SimEvse state (charging vs connected) based on the current pilot.
  void updateBattery(double dt_seconds);

  const PeerScenario &scenario() const { return _scenario; }
  const std::string &id() const { return _scenario.id; }

  SimEvse &simEvse() { return _sim; }
  EvseManager &evse() { return _evse; }
  DivertTask &divert() { return _divert; }
  CurrentShaperTask &shaper() { return _shaper; }

  // Cached values used both for output and for load-share allocation.
  double last_solar_w = 0.0;
  double last_grid_ie_w = 0.0;
  double last_live_pwr_w = 0.0;

  // Online/vehicle state — separate from SimEvse so we can model offline
  // peers (peer doesn't even respond to commands).
  bool online = true;
  bool vehicle = true;

private:
  PeerScenario _scenario;
  SimEvse _sim;
  SimStream _stream;
  EvseManager _evse;
  DivertTask _divert;
  CurrentShaperTask _shaper;
  ManualOverride _manual;

  // Track which event-indices have already fired.
  size_t _next_event_idx = 0;
};

} // namespace sim

#endif // _DIVERT_SIM_SIM_PEER_H
