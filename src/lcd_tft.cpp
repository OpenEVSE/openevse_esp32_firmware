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
#include "embedded_files.h"

#include "web_server.h"

PNG png;

#include "lcd_static/lcd_gui_static_files.h"

#define MAX_IMAGE_WIDTH TFT_HEIGHT // Adjust for your images

struct image_render_state {
  TFT_eSPI *tft;
  int16_t xpos;
  int16_t ypos;
};

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

  switch(_state)
  {
    case State::Boot:
      DBUGLN("LCD UI setup");

      _lcd.begin();
      _lcd.setRotation(1);

      render_image("/BootScreen.png", 0, 0);

      pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
      digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
      nextUpdate = 5000;
      _state = State::Charge;
      break;

    case State::Charge:
      render_image("/ChargeScreen.png", 0, 0);

      _lcd.setCursor(0, 0, 2);
      _lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      _lcd.setTextSize(2);
      _lcd.print("OpenEVSE");
      _lcd.setCursor(0, 16);
      _lcd.print("WiFi Connected");

      break;

    default:
      break;
  }

  DBUGVAR(nextUpdate);
  return nextUpdate;
}

void LcdTask::render_image(const char *filename, int16_t x, int16_t y)
{
  StaticFile *file = NULL;
  if(embedded_get_file(filename, lcd_gui_static_files, ARRAY_LENGTH(lcd_gui_static_files), &file))
  {
    DBUGF("Found %s (%d bytes)", filename, file->length);
    int16_t rc = png.openFLASH((uint8_t *)file->data, file->length, png_draw);
    if (rc == PNG_SUCCESS)
    {
      DBUGLN("Successfully opened png file");
      DBUGF("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
      _lcd.startWrite();
      uint32_t dt = millis();
      image_render_state state = {&_lcd, x, y};
      rc = png.decode(&state, 0);
      DBUG(millis() - dt); DBUGLN("ms");
      _lcd.endWrite();
      // png.close(); // not needed for memory->memory decode
    }
  }
}

void LcdTask::png_draw(PNGDRAW *pDraw)
{
  image_render_state *state = (image_render_state *)pDraw->pUser;
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  state->tft->pushImage(state->xpos, state->ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_lcd
