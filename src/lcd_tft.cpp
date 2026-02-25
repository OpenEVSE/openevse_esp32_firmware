#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#include <WiFi.h>
#include "emonesp.h"
#include "lcd.h"
#include "lcd_common.h"
#include "screens/screen_renderer.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"
#include "app_config.h"
#include <sys/time.h>
#include "embedded_files.h"
#include "manual.h"

#include "screens/screen_manager.h"

PNG png; // Global PNG decoder instance

LcdTask::LcdTask() :
  LcdTaskBase(),
  _tft(),
#ifdef ENABLE_DOUBLE_BUFFER
  _back_buffer(&_tft),
  _screen(_back_buffer)
#else
  _screen(_tft)
#endif
{
  for(int i = 0; i < LCD_MAX_LINES; i++) {
    clear_message_line(i);
  }
}

LcdTask::~LcdTask()
{
  if (_screenManager) {
    delete _screenManager;
    _screenManager = nullptr;
  }
}

void LcdTask::backlightControl(bool on)
{
  digitalWrite(LCD_BACKLIGHT_PIN, on ? HIGH : LOW);
}

void LcdTask::onMessageDisplayed(Message *msg)
{
  wakeBacklight();
  set_message_line(msg->getX(), msg->getY(), msg->getMsg(), msg->getClear());
}

void LcdTask::setWifiMode(bool client, bool connected)
{
  if (_screenManager)
  {
    if(_screenManager->setWifiMode(client, connected))
    {
      wakeBacklight();
    }
  }
}

void LcdTask::begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual)
{
  LcdTaskBase::begin(evse, scheduler, manual);
}

void LcdTask::setup()
{
}

unsigned long LcdTask::loop(MicroTasks::WakeReason reason)
{
  DBUG("LCD UI woke: ");
  DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");

  unsigned long nextUpdate = MicroTask.Infinate;

  if(_initialise)
  {
    // We need to initialize after the Networking as that breaks the display
    DBUGVAR(ESP.getFreeHeap());
    _tft.init();
    _tft.setRotation(1);
    DBUGF("Screen initialised, size: %dx%d", _screen_width, _screen_height);

#ifdef ENABLE_DOUBLE_BUFFER
    _back_buffer_pixels = (uint16_t *)_back_buffer.createSprite(_screen_width, _screen_height);
    _back_buffer.setTextDatum(MC_DATUM);
    DBUGF("Back buffer %p", _back_buffer_pixels);
#endif

    // Create the screen manager with pointers to display and data sources
    _screenManager = new ScreenManager(_screen, *_evse, *_scheduler, *_manual);

    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    wakeBacklight();
    _initialise = false;
  }

  // If we have messages to display, do it
  if(_head) {
    nextUpdate = displayNextMessage();
  }
  DBUGVAR(_nextMessageTime);
  if(!_msg_cleared && millis() >= _nextMessageTime)
  {
    DBUGLN("Clearing message lines");
    for(int i = 0; i < LCD_MAX_LINES; i++) {
      clear_message_line(i);
    }
    _msg_cleared = true;
  }

#ifdef ENABLE_DOUBLE_BUFFER
  _tft.startWrite();
#endif

  // Update the screen manager
  if (_screenManager) {
    unsigned long screenUpdate = _screenManager->update();
    if (screenUpdate < nextUpdate) {
      nextUpdate = screenUpdate;
    }
  }

  // Update backlight state based on timeout and EVSE state
  updateBacklight();

#ifdef ENABLE_DOUBLE_BUFFER
  _tft.pushImage(0, 0, _screen_width, _screen_height, _back_buffer_pixels);
  _tft.endWrite();
#endif

  DBUGVAR(nextUpdate);
  return nextUpdate;
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_TFT
