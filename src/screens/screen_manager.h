#ifndef __SCREEN_MANAGER_H
#define __SCREEN_MANAGER_H

#include "screen_base.h"
#include <vector>

// Screen types
enum ScreenType {
  SCREEN_BOOT,
  SCREEN_CHARGE,
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

  // Set WiFi mode on the charge screen
  void setWifiMode(bool client, bool connected);

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  void wakeBacklight();
  void timeoutBacklight();
  void updateBacklight();
#endif //TFT_BACKLIGHT_TIMEOUT_MS

private:
  TFT_eSPI &_screen;
  EvseManager &_evse;
  Scheduler &_scheduler;
  ManualOverride &_manual;

  ScreenType _current_screen;
  ScreenBase* _screens[SCREEN_COUNT];

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  unsigned long _last_backlight_wakeup = 0;
  bool _previous_vehicle_state = false;
  uint8_t _previous_evse_state = 0;
#endif //TFT_BACKLIGHT_TIMEOUT_MS

  // Initialize all screen objects
  void initializeScreens();
};

#endif // __SCREEN_MANAGER_H
