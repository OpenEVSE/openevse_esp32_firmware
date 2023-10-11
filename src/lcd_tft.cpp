#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#ifndef LCD_BACKLIGHT_PIN
#define LCD_BACKLIGHT_PIN TFT_BL
#endif

#include "emonesp.h"
#include "lcd.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"
#include "app_config.h"
#include <sys/time.h>

#include "web_server.h"

// Static files
struct StaticFile
{
  const char *filename;
  const char *data;
  size_t length;
  const char *type;
  const char *etag;
  bool compressed;
};

#include "lcd_static/lcd_gui_static_files.h"

LcdTask::LcdTask() :
  MicroTasks::Task(),
  _lcd()
{
}

void LcdTask::display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags)
{
}

void LcdTask::display(String &msg, int x, int y, int time, uint32_t flags)
{
}

void LcdTask::display(const char *msg, int x, int y, int time, uint32_t flags)
{
}

void LcdTask::begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual)
{
  MicroTask.startTask(this);
}

void LcdTask::setup()
{
  DBUGLN("LCD UI setup");

  _lcd.begin();
  _lcd.setRotation(1);
  _lcd.fillScreen(TFT_BLACK);

  delay(100);

  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
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

  //lv_timer_handler();

  DBUGVAR(nextUpdate);
  return nextUpdate;
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_TFT
