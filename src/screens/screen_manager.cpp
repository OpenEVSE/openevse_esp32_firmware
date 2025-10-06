#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_SCREEN_MANAGER)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#include "emonesp.h"
#include "screens/screen_manager.h"
#include "screens/screen_boot.h"
#include "screens/screen_charge.h"
#include "screens/screen_lock.h"
#include "lcd_common.h"

ScreenManager::ScreenManager(TFT_eSPI &screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual) :
  _screen(screen),
  _evse(evse),
  _scheduler(scheduler),
  _manual(manual),
  _current_screen(SCREEN_BOOT)
{
  // Initialize all screens to nullptr
  for (int i = 0; i < SCREEN_COUNT; i++) {
    _screens[i] = nullptr;
  }

  // Create screen objects
  initializeScreens();

  // Initialize the first active screen
  if (_screens[_current_screen]) {
    _screens[_current_screen]->init();
  }
}

ScreenManager::~ScreenManager()
{
  // Clean up screen objects
  for (int i = 0; i < SCREEN_COUNT; i++) {
    if (_screens[i]) {
      delete _screens[i];
      _screens[i] = nullptr;
    }
  }
}

void ScreenManager::initializeScreens()
{
  _screens[SCREEN_BOOT] = new BootScreen(_screen, _evse, _scheduler, _manual);
  _screens[SCREEN_CHARGE] = new ChargeScreen(_screen, _evse, _scheduler, _manual);
  _screens[SCREEN_LOCK] = new LockScreen(_screen, _evse, _scheduler, _manual);
  // Initialize additional screens as needed
}

void ScreenManager::setScreen(ScreenType screen)
{
  if (screen >= SCREEN_COUNT || !_screens[screen]) {
    return;
  }

  if (_current_screen != screen) {
    DBUGF("Changing screen from %d to %d", _current_screen, screen);
    _current_screen = screen;
    _screens[_current_screen]->init();
  }
}

unsigned long ScreenManager::update()
{
  unsigned long nextUpdate = 1000; // Default to 1 second

  // Handle special case: automatic transition from boot to charge screen
  if (_current_screen == SCREEN_BOOT) {
    BootScreen* bootScreen = static_cast<BootScreen*>(_screens[SCREEN_BOOT]);
    if (bootScreen->isBootComplete()) {
      setScreen(SCREEN_CHARGE);
    }
  }


#ifdef ENABLE_LOCK_SCREEN
  // Check if EVSE has entered or exited the active state

  // If EVSE is not active and we're not on the lock screen, switch to it
  if (!_evse.isActive() && _current_screen != SCREEN_LOCK && _current_screen != SCREEN_BOOT) {
    DBUGF("EVSE not active, switching to lock screen");
    setScreen(SCREEN_LOCK);
  }
  // If EVSE is active again and we're on the lock screen, switch back to charge screen
  else if (_evse.isActive() && _current_screen == SCREEN_LOCK) {
    DBUGF("EVSE active, switching back to charge screen");
    setScreen(SCREEN_CHARGE);
  }
#endif

  // Update the current screen
  if (_screens[_current_screen]) {
    nextUpdate = _screens[_current_screen]->update();
  }

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  bool vehicle_state = _evse.isVehicleConnected();
  uint8_t evse_state = _evse.getEvseState();

  if (_previous_evse_state != evse_state || _previous_vehicle_state != vehicle_state) {
    wakeBacklight();
    _previous_vehicle_state = vehicle_state;  
    _previous_evse_state = evse_state;
  } else {
    updateBacklight();
  }
#endif //TFT_BACKLIGHT_TIMEOUT_MS

  return nextUpdate; 
}

void ScreenManager::handleEvent(uint8_t event)
{
  if (_screens[_current_screen]) {
    _screens[_current_screen]->handleEvent(event);
  }
}

void ScreenManager::setWifiMode(bool client, bool connected)
{
  // Currently only the charge screen needs to know about WiFi mode
  ChargeScreen* chargeScreen = static_cast<ChargeScreen*>(_screens[SCREEN_CHARGE]);
  if (chargeScreen) {
    if(chargeScreen->setWifiMode(client, connected))
    {
      #ifdef TFT_BACKLIGHT_TIMEOUT_MS
      wakeBacklight();
      #endif //TFT_BACKLIGHT_TIMEOUT_MS
    }
  }
}

// Add backlight management implementations
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
void ScreenManager::wakeBacklight()
{
  DBUGLN("🔦 Waking backlight");
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
  _backlight_timeout = millis() + TFT_BACKLIGHT_TIMEOUT_MS;
}

void ScreenManager::timeoutBacklight()
{
  if (millis() >= _backlight_timeout) 
  {
    DBUGLN("Timing out backlight");
    digitalWrite(LCD_BACKLIGHT_PIN, LOW);
  }
}

void ScreenManager::updateBacklight()
{
  DBUGF("Backlight timeout in %lu ms", _backlight_timeout - millis());

  bool timeout = true;
  if (_evse.isVehicleConnected()) {
    switch (_evse.getEvseState()) {
      case OPENEVSE_STATE_STARTING:
      case OPENEVSE_STATE_VENT_REQUIRED:
      case OPENEVSE_STATE_DIODE_CHECK_FAILED:
      case OPENEVSE_STATE_GFI_FAULT:
      case OPENEVSE_STATE_NO_EARTH_GROUND:
      case OPENEVSE_STATE_STUCK_RELAY:
      case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
      case OPENEVSE_STATE_OVER_TEMPERATURE:
      case OPENEVSE_STATE_OVER_CURRENT:
        timeout = false;
        break;
      case OPENEVSE_STATE_NOT_CONNECTED:
      case OPENEVSE_STATE_CONNECTED:
      case OPENEVSE_STATE_SLEEPING:
      case OPENEVSE_STATE_DISABLED:
        timeout = true;
        break;
      case OPENEVSE_STATE_CHARGING:
#ifdef TFT_BACKLIGHT_CHARGING_THRESHOLD
        if (_evse.getAmps() >= TFT_BACKLIGHT_CHARGING_THRESHOLD) {
          wakeBacklight();
          timeout = false;
        }
#else
        timeout = false;
#endif //TFT_BACKLIGHT_CHARGING_THRESHOLD
        break;
      default:
        timeout = true;
        break;
    }
  }
  DBUGVAR(timeout);
  if (timeout) {
    timeoutBacklight();
  }
}
#endif //TFT_BACKLIGHT_TIMEOUT_MS

#endif // ENABLE_SCREEN_LCD_TFT
