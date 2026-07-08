#include "sim_evse.h"

#include <algorithm>

SimEvse::SimEvse() = default;

double SimEvse::actualCurrent() const
{
  if (!vehicle_connected || state != OPENEVSE_STATE_CHARGING || pilot <= 0) {
    return 0.0;
  }
  if (soc >= 100.0) {
    return 0.0;
  }

  double offered_w = static_cast<double>(pilot) * voltage;
  double max_ev_w = max_charge_rate_kw * 1000.0;

  if (soc > SIM_EVSE_TAPER_START_SOC) {
    double taper = 1.0 - ((soc - SIM_EVSE_TAPER_START_SOC) / SIM_EVSE_TAPER_RANGE);
    if (taper < 0.0) taper = 0.0;
    max_ev_w *= taper;
  }

  double actual_w = std::min(offered_w, max_ev_w);
  return actual_w / voltage;
}

void SimEvse::tick(double dt_seconds)
{
  double amps = actualCurrent();
  if (amps <= 0.0 || dt_seconds <= 0.0) {
    return;
  }
  double energy_kwh = (amps * voltage / 1000.0) * (dt_seconds / 3600.0);
  double soc_delta = (energy_kwh / battery_capacity_kwh) * 100.0;
  soc = std::min(100.0, soc + soc_delta);
  if (soc >= SIM_EVSE_COMPLETE_SOC) {
    soc = 100.0;
  }
}

void SimEvse::setVehicleConnected(bool connected)
{
  vehicle_connected = connected;
  if (!connected) {
    state = OPENEVSE_STATE_NOT_CONNECTED;
  } else if (state == OPENEVSE_STATE_NOT_CONNECTED) {
    state = OPENEVSE_STATE_CONNECTED;
  }
}
