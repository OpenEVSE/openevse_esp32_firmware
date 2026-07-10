#include "peer.h"

#include "app_config.h"
#include "openevse.h"

namespace sim {

Peer::Peer(const PeerScenario &scenario, EventLog &eventLog) :
    _scenario(scenario),
    _sim(),
    _stream(),
    _evse(_stream, eventLog),
    _divert(_evse),
    _shaper(),
    _manual(_evse)
{
  _sim.id = _scenario.id;
  _sim.voltage = _scenario.voltage;
  _sim.min_current = _scenario.min_current;
  _sim.max_current_hw = _scenario.max_current;
  _sim.battery_capacity_kwh = _scenario.battery_capacity_kwh;
  _sim.max_charge_rate_kw = _scenario.max_charge_rate_kw;
  _sim.soc = _scenario.initial_soc;
  _sim.request_current = _scenario.initial_request_current;
  _sim.aux_load_kw = _scenario.initial_aux_load_kw;
  _sim.pilot = (long) _scenario.max_current;
  _sim.vehicle_connected = _scenario.initial_vehicle;
  _sim.state = _scenario.initial_vehicle
                   ? OPENEVSE_STATE_CONNECTED
                   : OPENEVSE_STATE_NOT_CONNECTED;

  online = _scenario.initial_online;
  vehicle = _scenario.initial_vehicle;
  _stream.sim = &_sim;
}

Peer::~Peer() = default;

void Peer::begin()
{
  _evse.begin();
  _divert.begin();

  bool eco = config_divert_enabled() && config_charge_mode() == 1;
  if (!_scenario.divert_mode.empty()) {
    if (_scenario.divert_mode == "eco" || _scenario.divert_mode == "solar" ||
        _scenario.divert_mode == "grid") {
      eco = true;
    } else if (_scenario.divert_mode == "off" || _scenario.divert_mode == "normal") {
      eco = false;
    }
  }
  _divert.setMode(eco ? DivertMode::Eco : DivertMode::Normal);

  _shaper.begin(_evse);
}

void Peer::applyInputs(long t_sec)
{
  if (!_scenario.solar.empty()) {
    last_solar_w = _scenario.solar.valueAt(t_sec);
    _divert.setSolar((int) last_solar_w);
  }
  if (!_scenario.grid_ie.empty()) {
    last_grid_ie_w = _scenario.grid_ie.valueAt(t_sec);
    _divert.setGridIe((int) last_grid_ie_w);
  }
  _divert.update_state();

  if (!_scenario.live_pwr.empty()) {
    last_live_pwr_w = _scenario.live_pwr.valueAt(t_sec);
    _shaper.setLivePwr((int) last_live_pwr_w);
  }
  if (!_scenario.vrms.empty()) {
    double v = _scenario.vrms.valueAt(t_sec);
    if (v >= 100 && v <= 300) _sim.voltage = (int) v;
  }
}

void Peer::applyEvents(long t_sec)
{
  while (_next_event_idx < _scenario.events.size() &&
         _scenario.events[_next_event_idx].t_sec <= t_sec) {
    const PeerEvent &e = _scenario.events[_next_event_idx];
    if (e.set_online) online = e.online;
    if (e.set_vehicle) {
      vehicle = e.vehicle;
      _sim.setVehicleConnected(vehicle);
    }
    if (e.set_request_current) _sim.request_current = e.request_current;
    if (e.set_aux_load_kw) _sim.aux_load_kw = e.aux_load_kw;
    _next_event_idx++;
  }
}

void Peer::updateBattery(double dt_seconds)
{
  // The runner has already executed firmware tasks for this tick — read the
  // current pilot and decide if we are charging.
  bool is_charging = vehicle && _sim.request_current && _sim.pilot > 0
      && _sim.state != OPENEVSE_STATE_DISABLED
      && _sim.state != OPENEVSE_STATE_SLEEPING
      && _sim.state != OPENEVSE_STATE_NOT_CONNECTED;

  if (is_charging) {
    _sim.state = OPENEVSE_STATE_CHARGING;
    if (_sim.actualCurrent() < 1.0) {
      _sim.request_current = false;
      _sim.state = OPENEVSE_STATE_CONNECTED;
    }
  } else if (vehicle) {
    if (_sim.state == OPENEVSE_STATE_CHARGING) {
      _sim.state = OPENEVSE_STATE_CONNECTED;
    }
  } else {
    _sim.state = OPENEVSE_STATE_NOT_CONNECTED;
  }

  _sim.tick(dt_seconds);
}

} // namespace sim
