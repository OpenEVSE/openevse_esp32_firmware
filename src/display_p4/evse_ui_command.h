#ifndef DISPLAY_P4_EVSE_UI_COMMAND_H
#define DISPLAY_P4_EVSE_UI_COMMAND_H
#if defined(ENABLE_SCREEN_LVGL)

#include <stdint.h>

// User intents from the on-device UI (design spec seam #2). Decouples the LVGL
// screens from ManualOverride / EvseManager claim mechanics.
class IEvseUiCommandSink
{
public:
  virtual ~IEvseUiCommandSink() {}
  virtual void toggleCharge() = 0;                       // start <-> stop
  virtual void setChargeCurrentLimit(uint32_t amps) = 0; // claim manual @ amps
  virtual void clearOverride() = 0;                      // drop manual claim
  virtual bool overrideActive() = 0;                     // is a manual claim held
};

class ManualOverride;

class EvseUiCommandSink : public IEvseUiCommandSink
{
public:
  explicit EvseUiCommandSink(ManualOverride &manual);
  void toggleCharge() override;
  void setChargeCurrentLimit(uint32_t amps) override;
  void clearOverride() override;
  bool overrideActive() override;

private:
  ManualOverride &_manual;
};

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_EVSE_UI_COMMAND_H
