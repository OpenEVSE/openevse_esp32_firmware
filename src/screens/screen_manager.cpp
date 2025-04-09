#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#include "screen_manager.h"
#include "emonesp.h"
#include "openevse.h"
#include "input.h"
#include "app_config.h"
#include "lcd_common.h"

ScreenManager::ScreenManager(lv_obj_t *screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual) :
  _screen(screen),
  _evse(evse),
  _scheduler(scheduler),
  _manual(manual)
{
  // Create the base layout
  _header_panel = lv_obj_create(screen);
  lv_obj_set_size(_header_panel, lv_disp_get_hor_res(NULL), 40);
  lv_obj_align(_header_panel, LV_ALIGN_TOP_MID, 0, 0);

  _content_panel = lv_obj_create(screen);
  lv_obj_set_size(_content_panel, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL) - 80);
  lv_obj_align(_content_panel, LV_ALIGN_CENTER, 0, 0);

  _footer_panel = lv_obj_create(screen);
  lv_obj_set_size(_footer_panel, lv_disp_get_hor_res(NULL), 40);
  lv_obj_align(_footer_panel, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Add WiFi status icon to header
  _wifi_icon = lv_label_create(_header_panel);
  lv_obj_align(_wifi_icon, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_label_set_text(_wifi_icon, LV_SYMBOL_WIFI);

  // Create the initial screen
  createMainScreen();

  // Initialize backlight timer
  _lastBacklightOn = millis();
}

ScreenManager::~ScreenManager()
{
  // LVGL will handle cleanup of objects when the screen is deleted
}

void ScreenManager::createMainScreen()
{
  // Clear any existing content
  lv_obj_clean(_content_panel);

  // Create main screen widgets
  lv_obj_t *status_label = lv_label_create(_content_panel);
  lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 10);
  lv_label_set_text(status_label, "OpenEVSE");

  // Add more UI components as needed

  _currentScreen = ScreenState::Main;
}

void ScreenManager::createInfoScreen()
{
  // Implementation for info screen
  lv_obj_clean(_content_panel);

  // Add info screen components

  _currentScreen = ScreenState::Info;
}

void ScreenManager::createSettingsScreen()
{
  // Implementation for settings screen
  lv_obj_clean(_content_panel);

  // Add settings screen components

  _currentScreen = ScreenState::Settings;
}

void ScreenManager::createWifiScreen()
{
  // Implementation for WiFi screen
  lv_obj_clean(_content_panel);

  // Add WiFi screen components

  _currentScreen = ScreenState::Wifi;
}

unsigned long ScreenManager::update()
{
  unsigned long nextUpdate = 1000; // Default update every second

  // Update WiFi status
  updateWifiStatus();

  // Update screen content based on current state
  switch(_currentScreen) {
    case ScreenState::Main:
      // Update main screen data
      break;
    case ScreenState::Info:
      // Update info screen data
      break;
    case ScreenState::Settings:
      // Update settings screen data
      break;
    case ScreenState::Wifi:
      // Update WiFi screen data
      break;
  }

  // Handle backlight timeout
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  unsigned long now = millis();
  if(now - _lastBacklightOn >= TFT_BACKLIGHT_TIMEOUT_MS) {
    digitalWrite(LCD_BACKLIGHT_PIN, LOW);
  }

  // Set next update based on backlight timeout
  unsigned long backlight_timeout = TFT_BACKLIGHT_TIMEOUT_MS - (now - _lastBacklightOn);
  if(backlight_timeout < nextUpdate) {
    nextUpdate = backlight_timeout;
  }
#endif

  return nextUpdate;
}

void ScreenManager::setWifiMode(bool client, bool connected)
{
  _wifi_client = client;
  _wifi_connected = connected;
  updateWifiStatus();
}

void ScreenManager::updateWifiStatus()
{
  if(_wifi_connected) {
    lv_obj_clear_flag(_wifi_icon, LV_OBJ_FLAG_HIDDEN);
    if(_wifi_client) {
      // Client mode - full WiFi symbol
      lv_label_set_text(_wifi_icon, LV_SYMBOL_WIFI);
    } else {
      // AP mode - different WiFi symbol or text
      lv_label_set_text(_wifi_icon, "AP");
    }
  } else {
    // No WiFi - show disconnected icon or hide
    lv_obj_add_flag(_wifi_icon, LV_OBJ_FLAG_HIDDEN);
  }
}

void ScreenManager::wakeBacklight()
{
  _lastBacklightOn = millis();
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
}

void ScreenManager::switchScreen(ScreenState newScreen)
{
  if(newScreen == _currentScreen) return;

  switch(newScreen) {
    case ScreenState::Main:
      createMainScreen();
      break;
    case ScreenState::Info:
      createInfoScreen();
      break;
    case ScreenState::Settings:
      createSettingsScreen();
      break;
    case ScreenState::Wifi:
      createWifiScreen();
      break;
  }
}
