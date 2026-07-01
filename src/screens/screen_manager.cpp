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

  return nextUpdate; 
}

void ScreenManager::handleEvent(uint8_t event)
{
  if (_screens[_current_screen]) {
    _screens[_current_screen]->handleEvent(event);
  }
}

bool ScreenManager::setWifiMode(bool client, bool connected)
{
  // Currently only the charge screen needs to know about WiFi mode
  ChargeScreen* chargeScreen = static_cast<ChargeScreen*>(_screens[SCREEN_CHARGE]);
  if (chargeScreen) {
    return chargeScreen->setWifiMode(client, connected);
  }
  return false;
}

#endif // ENABLE_SCREEN_LCD_TFT
