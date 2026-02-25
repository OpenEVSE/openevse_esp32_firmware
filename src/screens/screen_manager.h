#ifndef __SCREEN_MANAGER_H
#define __SCREEN_MANAGER_H

#include "screen_base.h"
#include <vector>

// Screen types
enum ScreenType {
  SCREEN_BOOT,
  SCREEN_CHARGE,
  SCREEN_LOCK,   // Lock screen when EVSE is disabled
  // Add other screen types here as needed
  SCREEN_COUNT
};

class ScreenManager {
public:
  ScreenManager(TFT_eSPI &screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual);
  ~ScreenManager();

  // Set the active screen
  void setScreen(ScreenType screen);

  // Get the current active screen
  ScreenType getCurrentScreen() const { return _current_screen; }

  // Update the current screen, returns time until next update
  unsigned long update();

  // Handle events (button presses, etc)
  void handleEvent(uint8_t event);

  // Set WiFi mode on the charge screen, returns true if changed
  bool setWifiMode(bool client, bool connected);

private:
  TFT_eSPI &_screen;
  EvseManager &_evse;
  Scheduler &_scheduler;
  ManualOverride &_manual;

  ScreenType _current_screen;
  ScreenBase* _screens[SCREEN_COUNT];

  // Initialize all screen objects
  void initializeScreens();
};

#endif // __SCREEN_MANAGER_H
