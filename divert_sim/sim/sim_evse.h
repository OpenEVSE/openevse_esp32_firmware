#ifndef _DIVERT_SIM_SIM_EVSE_H
#define _DIVERT_SIM_SIM_EVSE_H

#include <string>
#include <openevse.h>

// SoC at which constant-power tapering begins.
#ifndef SIM_EVSE_TAPER_START_SOC
#define SIM_EVSE_TAPER_START_SOC 80.0
#endif

// SoC range over which the taper goes from 100% to 0%.
#ifndef SIM_EVSE_TAPER_RANGE
#define SIM_EVSE_TAPER_RANGE 20.0
#endif

// Models a single simulated EVSE + EV pair.
//
// Holds the pilot/state values that the firmware backend expects to see (these
// replace the file-static `pilot`/`state` of the legacy simulator) plus a
// simple EV battery model that translates an offered current into an actual
// charge current (taking EV max rate and SoC tapering into account) and
// integrates the SoC over time.
class SimEvse
{
public:
  SimEvse();

  std::string id;
  double voltage = 240.0;
  double min_current = 6.0;
  double max_current_hw = 32.0;

  // RAPI-visible state. Updated by the RapiSender shim and read by the runner.
  long pilot = 32;
  long state = OPENEVSE_STATE_CONNECTED;

  // Whether a vehicle is currently plugged in.
  bool vehicle_connected = true;

  // Whether the connected EV is currently requesting power. A vehicle can be
  // plugged in but delay charging due to its own schedule or because charging
  // has completed.
  bool request_current = true;

  // Optional non-battery load requested by the EV while connected, e.g. cabin
  // pre-conditioning after the traction battery has finished charging.
  double aux_load_kw = 0.0;

  // EV battery model
  double battery_capacity_kwh = 75.0;
  double max_charge_rate_kw = 7.2;
  double soc = 50.0;

  // Compute the actual current the EV will draw given the present pilot,
  // state, vehicle connected flag, EV max rate and SoC taper.
  // Returns amps (0 if not charging).
  double actualCurrent() const;

  // Integrate SoC over `dt_seconds` using `actualCurrent()`. Caps at 100%.
  void tick(double dt_seconds);

  // Reflect the connected/disconnected vehicle in the RAPI state.
  void setVehicleConnected(bool connected);
};

#endif // _DIVERT_SIM_SIM_EVSE_H
