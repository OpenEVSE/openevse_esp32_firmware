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

LcdTask::Message::Message(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags) :
  _next(NULL),
  _msg(""),
  _x(x),
  _y(y),
  _time(time),
  _clear(flags & LCD_CLEAR_LINE ? 1 : 0)
{
  strncpy_P(_msg, reinterpret_cast<PGM_P>(msg), LCD_MAX_LEN);
  _msg[LCD_MAX_LEN] = '\0';
}

LcdTask::Message::Message(String &msg, int x, int y, int time, uint32_t flags) :
  Message(msg.c_str(), x, y, time, flags)
{
}

LcdTask::Message::Message(const char *msg, int x, int y, int time, uint32_t flags) :
  _next(NULL),
  _msg(""),
  _x(x),
  _y(y),
  _time(time),
  _clear(flags & LCD_CLEAR_LINE ? 1 : 0)
{
  strncpy(_msg, msg, LCD_MAX_LEN);
  _msg[LCD_MAX_LEN] = '\0';
}

LcdTask::LcdTask() :
  MicroTasks::Task(),
  _head(NULL),
  _tail(NULL),
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

  // Clean up any remaining messages
  for(Message *next, *node = _head; node; node = next) {
    next = node->getNext();
    delete node;
  }
}

void LcdTask::display(Message *msg, uint32_t flags)
{
  if(flags & LCD_DISPLAY_NOW)
  {
    for(Message *next, *node = _head; node; node = next) {
      next = node->getNext();
      delete node;
    }
    _head = NULL;
    _tail = NULL;
  }

  if(_tail) {
    _tail->setNext(msg);
  } else {
    _head = msg;
    _nextMessageTime = millis();
  }
  _tail = msg;

  if(flags & LCD_DISPLAY_NOW) {
    displayNextMessage();
  }
}

void LcdTask::display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags)
{
  DBUGVAR(msg);
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::display(String &msg, int x, int y, int time, uint32_t flags)
{
  DBUGVAR(msg);
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::display(const char *msg, int x, int y, int time, uint32_t flags)
{
  DBUGVAR(msg);
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::setWifiMode(bool client, bool connected)
{
  if (_screenManager)
  {
    _screenManager->setWifiMode(client, connected);
  }
}

void LcdTask::begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual)
{
  MicroTask.startTask(this);
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
    _screenManager = new ScreenManager(_screen, evse, scheduler, manual);

    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    _screenManager->wakeBacklight();
#else
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
#endif //TFT_BACKLIGHT_TIMEOUT_MS
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

#ifdef ENABLE_DOUBLE_BUFFER
  _tft.pushImage(0, 0, _screen_width, _screen_height, _back_buffer_pixels);
  _tft.endWrite();
#endif

  DBUGVAR(nextUpdate);
  return nextUpdate;
}

unsigned long LcdTask::displayNextMessage()
{
  while(_head && millis() >= _nextMessageTime)
  {
    // Pop a message from the queue
    Message *msg = _head;
    DBUGF("msg = %p", msg);
    _head = _head->getNext();
    if(NULL == _head) {
      _tail = NULL;
    }

    // Display the message
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    if (_screenManager) {
      _screenManager->wakeBacklight();
    }
#endif //TFT_BACKLIGHT_TIMEOUT_MS
    set_message_line(msg->getX(), msg->getY(), msg->getMsg(), msg->getClear());

    _nextMessageTime = millis() + msg->getTime();

    // delete the message
    delete msg;
  }

  unsigned long nextUpdate = _nextMessageTime - millis();
  DBUGVAR(nextUpdate);
  return nextUpdate;
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_TFT
