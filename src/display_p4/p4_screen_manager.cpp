#if defined(ENABLE_SCREEN_LVGL)

#include "evse_ui_model.h"
#include "p4_screen_manager.h"

P4ScreenManager::P4ScreenManager(IEvseUiModel &model)
  : _model(model), _current(P4Screen::Boot), _booted(false)
{
}

P4Screen P4ScreenManager::select() const
{
  if (!_booted) {
    return P4Screen::Boot;
  }
  if (_model.error()) {
    return P4Screen::Fault;
  }
  if (!_model.active()) {
    return P4Screen::Sleeping;   // EVSE disabled / sleeping
  }
  return P4Screen::Charge;
}

bool P4ScreenManager::update()
{
  P4Screen next = select();
  if (next != _current) {
    _current = next;
    return true;
  }
  return false;
}

const char *P4ScreenManager::name(P4Screen s)
{
  switch (s) {
    case P4Screen::Boot:     return "boot";
    case P4Screen::Charge:   return "charge";
    case P4Screen::Sleeping: return "sleeping";
    case P4Screen::Fault:    return "fault";
  }
  return "?";
}

#endif // ENABLE_SCREEN_LVGL
