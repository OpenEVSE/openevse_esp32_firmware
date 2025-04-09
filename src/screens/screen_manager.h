#ifndef _SCREEN_MANAGER_H
#define _SCREEN_MANAGER_H

#include <Arduino.h>
#include <lvgl.h>

class EvseManager;
class Scheduler;
class ManualOverride;

// Screen state definitions
enum class ScreenState {
  Main,
  Info,
  Settings,
  Wifi,
  // Add more screens as needed
};

class ScreenManager
{
  private:
    lv_obj_t *_screen;              // Main screen object
    EvseManager &_evse;             // Reference to EVSE manager
    Scheduler &_scheduler;          // Reference to scheduler
    ManualOverride &_manual;        // Reference to manual controls

    bool _wifi_client = false;
    bool _wifi_connected = false;
    ScreenState _currentScreen = ScreenState::Main;

    unsigned long _lastBacklightOn = 0;

    // LVGL UI components
    lv_obj_t *_header_panel;
    lv_obj_t *_content_panel;
    lv_obj_t *_footer_panel;
    lv_obj_t *_wifi_icon;

    void createMainScreen();
    void createInfoScreen();
    void createSettingsScreen();
    void createWifiScreen();

    void updateWifiStatus();

  public:
    ScreenManager(lv_obj_t *screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual);
    ~ScreenManager();

    unsigned long update();
    void setWifiMode(bool client, bool connected);
    void wakeBacklight();
    void switchScreen(ScreenState newScreen);
};

#endif // _SCREEN_MANAGER_H
