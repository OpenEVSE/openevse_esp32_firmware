#ifndef DISPLAY_P4_EVSE_UI_MODEL_H
#define DISPLAY_P4_EVSE_UI_MODEL_H
#if defined(ENABLE_SCREEN_LVGL)

#include <stdint.h>

// Read-only view of EVSE + network state for the on-device UI (design spec
// seam #1). Decouples the LVGL screens from EvseManager/EvseMonitor.
class IEvseUiModel
{
public:
  virtual ~IEvseUiModel() {}

  // Charge / connection state
  virtual bool evseConnected() = 0;       // OpenEVSE controller reachable (RAPI)
  virtual uint8_t evseState() = 0;        // OPENEVSE_STATE_*
  virtual const char *stateText() = 0;    // human-readable state
  virtual bool vehicleConnected() = 0;
  virtual bool charging() = 0;
  virtual bool active() = 0;               // EVSE enabled (not disabled)
  virtual bool error() = 0;

  // Live electrical values
  virtual double voltage() = 0;            // V
  virtual double amps() = 0;               // A
  virtual double power() = 0;              // W
  virtual uint32_t pilotCurrent() = 0;     // A (pilot / charge-current limit)

  // Session
  virtual uint32_t sessionElapsed() = 0;   // seconds
  virtual double sessionEnergy() = 0;      // Wh

  // Environment
  virtual bool tempValid() = 0;
  virtual double temperatureC() = 0;

  // Network
  virtual bool wifiConnected() = 0;
  virtual bool wifiApMode() = 0;
  virtual int wifiRssi() = 0;              // dBm (STA)
};

class EvseManager;

class EvseUiModel : public IEvseUiModel
{
public:
  explicit EvseUiModel(EvseManager &evse);

  bool evseConnected() override;
  uint8_t evseState() override;
  const char *stateText() override;
  bool vehicleConnected() override;
  bool charging() override;
  bool active() override;
  bool error() override;
  double voltage() override;
  double amps() override;
  double power() override;
  uint32_t pilotCurrent() override;
  uint32_t sessionElapsed() override;
  double sessionEnergy() override;
  bool tempValid() override;
  double temperatureC() override;
  bool wifiConnected() override;
  bool wifiApMode() override;
  int wifiRssi() override;

private:
  EvseManager &_evse;
};

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_EVSE_UI_MODEL_H
