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

void LcdTask::displayFlush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
{
  uint32_t w = ( area->x2 - area->x1 + 1 );
  uint32_t h = ( area->y2 - area->y1 + 1 );

  lcd._lcd.startWrite();
  lcd._lcd.setAddrWindow( area->x1, area->y1, w, h );
  lcd._lcd.pushColors( ( uint16_t * )&color_p->full, w * h, true );
  lcd._lcd.endWrite();

  lv_disp_flush_ready( disp );
}

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

  //lv_init();

  _lcd.begin();
  _lcd.setRotation(1);
  _lcd.fillScreen(TFT_BLACK);

  delay(100);

  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

  //size_t bufferSize = _screenWidth * _screenHeight / 13;
  //_buf1 = ( lv_color_t * )malloc( bufferSize * sizeof( lv_color_t ) );
  //lv_disp_draw_buf_init( &_draw_buf, _buf1, NULL, bufferSize );
//
  //static lv_disp_drv_t disp_drv;
  //lv_disp_drv_init( &disp_drv );
//
  //disp_drv.hor_res = _screenWidth;
  //disp_drv.ver_res = _screenHeight;
  //disp_drv.flush_cb = displayFlush;
  //disp_drv.draw_buf = &_draw_buf;
  //lv_disp_drv_register( &disp_drv );

//  static lv_indev_drv_t indev_drv;
//  lv_indev_drv_init( &indev_drv );
//  indev_drv.type = LV_INDEV_TYPE_POINTER;
//  indev_drv.read_cb = my_touchpad_read;
//  lv_indev_drv_register( &indev_drv );

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
