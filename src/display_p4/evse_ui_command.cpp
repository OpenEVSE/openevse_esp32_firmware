#if defined(ENABLE_SCREEN_LVGL)

#include "manual.h"
#include "evse_man.h"
#include "evse_ui_command.h"

EvseUiCommandSink::EvseUiCommandSink(ManualOverride &manual) : _manual(manual) {}

void EvseUiCommandSink::toggleCharge()
{
  _manual.toggle();
}

void EvseUiCommandSink::setChargeCurrentLimit(uint32_t amps)
{
  EvseProperties props(EvseState::Active);
  props.setChargeCurrent(amps);
  _manual.claim(props);
}

void EvseUiCommandSink::clearOverride()
{
  _manual.release();
}

bool EvseUiCommandSink::overrideActive()
{
  return _manual.isActive();
}

#endif // ENABLE_SCREEN_LVGL
