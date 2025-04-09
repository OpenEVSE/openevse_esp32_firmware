#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#include <WiFi.h>
#include "emonesp.h"
#include "lcd.h"
#include "lcd_common.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"
#include "app_config.h"
#include <sys/time.h>
#include "embedded_files.h"
#include "manual.h"

// LVGL includes
#include <lvgl.h>
#include <TFT_eSPI.h>

#include "screens/screen_manager.h"

// Global message buffer shared with renderers
char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1];

// Define LCD buffer size based on screen dimensions (if not already defined)
#ifndef LCD_BUFFER_SIZE
#define LCD_BUFFER_SIZE (TFT_WIDTH * 10) // Buffer for 10 rows of the display
#endif

// LVGL timing definitions
#ifndef LVGL_TICK_PERIOD
#define LVGL_TICK_PERIOD 5  // 5 milliseconds tick period for LVGL timer
#endif
#ifndef LVGL_UPDATE_PERIOD
#define LVGL_UPDATE_PERIOD 10  // 10 milliseconds minimum update period
#endif

// LVGL display buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[LCD_BUFFER_SIZE];
#ifdef ENABLE_DOUBLE_BUFFER
static lv_color_t buf2[LCD_BUFFER_SIZE];
#endif

// Display driver
static lv_disp_drv_t disp_drv;
static TFT_eSPI tft = TFT_eSPI();

// Input device driver (if needed)
// static lv_indev_drv_t indev_drv;

// LVGL flush callback
static void lvgl_flush_cb(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)color_p, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

// LVGL timer callback
static void lvgl_timer_cb(lv_timer_t * timer)
{
  lv_tick_inc(LVGL_TICK_PERIOD);
}

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
  _lvgl_initialized(false),
  _msg_label(NULL),
  _screenManager(nullptr),
  _initialise(true),
  _msg_cleared(false),
  _nextMessageTime(0),
  _screen(tft)
{
  for(int i = 0; i < LCD_MAX_LINES; i++) {
    clearLine(i);
  }
  _msg_cleared = true;
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

    // Initialize TFT
    tft.init();
    tft.setRotation(1);

    // Initialize LVGL
    lv_init();

    // Initialize the display buffer
#ifdef ENABLE_DOUBLE_BUFFER
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_BUFFER_SIZE);
#else
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, LCD_BUFFER_SIZE);
#endif

    // Initialize the display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.hor_res = _screen_width;
    disp_drv.ver_res = _screen_height;
    lv_disp_drv_register(&disp_drv);

    // Create an LVGL timer
    lv_timer_create(lvgl_timer_cb, LVGL_TICK_PERIOD, NULL);

    // Create a label for messages
    _msg_label = lv_label_create(lv_scr_act());
    lv_obj_align(_msg_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(_msg_label, "");

    DBUGF("Screen initialised, size: %dx%d", _screen_width, _screen_height);

    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    if(_screenManager) {
      _screenManager->wakeBacklight();
    } else {
      digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
    }
#else
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
#endif //TFT_BACKLIGHT_TIMEOUT_MS
    _initialise = false;
    _lvgl_initialized = true;

    // Create the screen manager with pointers to display and data sources
    _screenManager = new ScreenManager(lv_scr_act(), evse, scheduler, manual);
  }

  // Process LVGL tasks
  if (_lvgl_initialized) {
    lv_timer_handler();
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
      clearLine(i);
    }
    if (_msg_label) {
      lv_label_set_text(_msg_label, "");
    }
    _msg_cleared = true;
  }

  // Update the screen manager
  if (_screenManager) {
    unsigned long screenUpdate = _screenManager->update();
    if (screenUpdate < nextUpdate) {
      nextUpdate = screenUpdate;
    }
  }

  // LVGL recommends calling lv_timer_handler every few ms
  if (nextUpdate > LVGL_UPDATE_PERIOD) {
    nextUpdate = LVGL_UPDATE_PERIOD;
  }

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
    showText(msg->getX(), msg->getY(), msg->getMsg(), msg->getClear());

    _nextMessageTime = millis() + msg->getTime();

    // delete the message
    delete msg;
  }

  unsigned long nextUpdate = _nextMessageTime - millis();
  DBUGVAR(nextUpdate);
  return nextUpdate;
}

void LcdTask::showText(int x, int y, const char *msg, bool clear)
{
  DBUGF("LCD: %d %d %s, clear=%s", x, y, msg, clear ? "true" : "false");

  if(clear) {
    clearLine(y);
  }

  strncpy(_msg[y], msg + x, LCD_MAX_LEN - x);
  _msg[y][LCD_MAX_LEN] = '\0';
  _msg_cleared = false;

  // Update the LVGL label with all message content
  if (_msg_label) {
    String text = "";
    for (int i = 0; i < LCD_MAX_LINES; i++) {
      if (_msg[i][0] != '\0') {
        if (i > 0) text += "\n";
        text += _msg[i];
      }
    }
    lv_label_set_text(_msg_label, text.c_str());
  }
}

void LcdTask::clearLine(int line)
{
  if(line < 0 || line >= LCD_MAX_LINES) {
    return;
  }

  memset(_msg[line], ' ', LCD_MAX_LEN);
  _msg[line][LCD_MAX_LEN] = '\0';
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_TFT
