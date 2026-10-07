#include "peer.h"

#include "app_config.h"
#include "openevse.h"

#include <iostream>

namespace sim {

Peer::Peer(const PeerScenario &scenario, EventLog &eventLog, long max_current_soft) :
    _scenario(scenario),
    _sim(),
    _stream(),
    _evse(_stream, eventLog),
    _divert(_evse),
    _shaper(),
    _manual(_evse),
    _boost(),
    _limit(),
    _temp_throttle(),
    _rfid_reader(),
    _rfid(),
    _scheduler(_evse, _divert, _shaper, _rfid, _limit)
{
  _sim.id = _scenario.id;
  _sim.voltage = _scenario.voltage;
  _sim.min_current = _scenario.min_current;
  _sim.max_current_hw = _scenario.max_current;
  _sim.max_configured = (max_current_soft > 0 && max_current_soft < _scenario.max_current)
                          ? max_current_soft
                          : (long) _scenario.max_current;
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

  _rfid_reader.present = _scenario.rfid_reader;

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
  _boost.begin(_evse);
  _limit.begin(_evse);
  _temp_throttle.begin(_evse);
  _rfid.begin(_evse, _rfid_reader);
  _scheduler.begin();
}

void Peer::loadSchedule(const std::string &scenario_default)
{
  const std::string &json = _scenario.schedule_json.empty()
                              ? scenario_default
                              : _scenario.schedule_json;
  if (!json.empty()) {
    replaceSchedule(json);
  }
}

void Peer::replaceSchedule(const std::string &json)
{
  // /schedule POSTs add or update events; a Charge Manager edit that drops a
  // rule DELETEs its timers. Mirror that by removing every existing event
  // before adding the new set.
  DynamicJsonDocument current(_scheduler.scheduleJsonCapacity());
  _scheduler.serialize(current);
  for (JsonObjectConst e : current.as<JsonArrayConst>()) {
    _scheduler.removeEvent(e["id"] | 0U);
  }
  if (!_scheduler.deserialize(json.c_str())) {
    std::cerr << "Peer " << id() << ": failed to load schedule" << std::endl;
  }
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
    if (_scenario.live_pwr_add_ev) {
      // Use the firmware's own ammeter reading for the EV share: the shaper
      // adds that reading back, so an ideal site meter in step with the
      // ammeter cancels exactly instead of chasing a one-poll lag.
      last_live_pwr_w += _evse.getAmps() * _evse.getVoltage();
    }
    _shaper.setLivePwr((int) last_live_pwr_w);
  }
  if (!_scenario.temperature.empty()) {
    _sim.temperature = _scenario.temperature.valueAt(t_sec);
  }
  if (_scenario.report_soc && vehicle) {
    _evse.setVehicleStateOfCharge((int) _sim.soc);
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
    if (e.set_boost) {
      if (e.boost_cancel) {
        _boost.cancel();
      } else {
        LimitType type = LimitType::None;  // fromString() may not assign
        type.fromString(e.boost_type.c_str());
        _boost.arm(type, e.boost_value);
      }
    }
    if (e.set_manual) {
      if (e.manual_state == "release") {
        _manual.release();
      } else {
        EvseProperties props(e.manual_state == "disabled" ? EvseState::Disabled
                                                          : EvseState::Active);
        _manual.claim(props);
      }
    }
    if (e.set_rfid) _rfid_reader.presentCard(e.rfid_tag);
    if (e.set_temperature) _sim.temperature = e.temperature;
    if (e.set_soc) _sim.soc = e.soc;
    if (e.set_claim) {
      EvseProperties props;
      if (e.claim_state == "active") props.setState(EvseState::Active);
      else if (e.claim_state == "disabled") props.setState(EvseState::Disabled);
      if (e.claim_charge_current >= 0) props.setChargeCurrent((uint32_t) e.claim_charge_current);
      if (e.claim_max_current >= 0) props.setMaxCurrent((uint32_t) e.claim_max_current);
      props.setAutoRelease(e.claim_auto_release);
      _evse.claim((EvseClient) e.claim_client, e.claim_priority, props);
    }
    if (e.set_release) _evse.release((EvseClient) e.release_client);
    if (e.set_limit) {
      if (e.limit_clear) {
        _limit.clear();
      } else {
        LimitProperties props;
        LimitType type = LimitType::None;
        type.fromString(e.limit_type.c_str());
        props.setType(type);
        props.setValue(e.limit_value);
        props.setAutoRelease(e.limit_auto_release);
        _limit.set(props);
      }
    }
    if (e.set_schedule) replaceSchedule(e.schedule_json);
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
