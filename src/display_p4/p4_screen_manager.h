#ifndef DISPLAY_P4_SCREEN_MANAGER_H
#define DISPLAY_P4_SCREEN_MANAGER_H
#if defined(ENABLE_SCREEN_LVGL)

#include <stdint.h>

class IEvseUiModel;

enum class P4Screen : uint8_t { Boot, Charge, Sleeping, Fault };

// Decides which logical screen should be active from the EVSE UI model.
// UI-backend-agnostic: it only computes the target screen; the display task (or,
// later, the EEZ UI layer) acts on current()/update().
class P4ScreenManager
{
public:
  explicit P4ScreenManager(IEvseUiModel &model);

  // Recompute the target screen. Returns true if it changed since last call.
  bool update();
  P4Screen current() const { return _current; }

  // Leave the Boot screen once startup is complete (called by the owner).
  void markBooted() { _booted = true; }

  static const char *name(P4Screen s);

private:
  P4Screen select() const;
  IEvseUiModel &_model;
  P4Screen _current;
  bool _booted;
};

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_SCREEN_MANAGER_H
