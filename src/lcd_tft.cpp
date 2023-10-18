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
//#include "fonts/DejaVu_Sans_72.h"

#define TFT_OPENEVSE_BACK   0x2413
#define TFT_OPENEVSE_GREEN  0x3E92
#define TFT_OPENEVSE_TEXT   0x1BD1

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
  _evse = &evse;
  _scheduler = &scheduler;
  _manual = &manual;
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

      _lcd.fillScreen(TFT_OPENEVSE_BACK);
      _lcd.fillSmoothRoundRect(90, 90, 300, 110, 15, TFT_WHITE);
      _lcd.fillSmoothRoundRect(90, 235, 300, 16, 8, TFT_OPENEVSE_GREEN);
      render_image("/logo.png", 104, 115);

      pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
      digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
      nextUpdate = 5000;
      _state = State::Charge;
      break;

    case State::Charge:
      render_image("/ChargeScreen.png", 0, 0);

      render_right_text("16", 266, 180, &FreeSans24pt7b, TFT_BLACK, 3);
      _lcd.setTextSize(1);
      _lcd.print("A");

      char buffer[32];

      render_centered_text(esp_hostname.c_str(), 324, 72, 140, &FreeSans9pt7b, TFT_OPENEVSE_TEXT);
      render_centered_text("11/08/2023, 3:45 AM", 324, 92, 140, &FreeSans9pt7b, TFT_OPENEVSE_TEXT);
      render_centered_text("00:00:00", 324, 144, 140, &FreeSans9pt7b, TFT_WHITE);
      get_scaled_number_value(_evse->getSessionEnergy(), 0, "Wh", buffer, sizeof(buffer));
      render_centered_text(buffer, 324, 196, 140, &FreeSans9pt7b, TFT_WHITE);
      //nextUpdate = 1000;
      break;

    default:
      break;
  }

  DBUGVAR(nextUpdate);
  return nextUpdate;
}

void LcdTask::get_scaled_number_value(double value, int precision, const char *unit, char *buffer, size_t size)
{
  static const char *mod[] = {
    "",
    "k",
    "M",
    "G",
    "T",
    "P"
  };

  int index = 0;
  while (value > 1000 && index < ARRAY_ITEMS(mod))
  {
    value /= 1000;
    index++;
  }

  snprintf(buffer, size, "%.*f %s%s", precision, value, mod[index], unit);
}


void LcdTask::render_centered_text(const char *text, int16_t x, int16_t y, int16_t width, const GFXfont *font, uint16_t color, uint8_t size)
{
  _lcd.setFreeFont(font);
  _lcd.setTextSize(size);
  int16_t text_width = _lcd.textWidth(text);
  int16_t text_x = x + ((width - text_width) / 2);
  _lcd.setTextColor(color);
  _lcd.setCursor(text_x, y);
  _lcd.print(text);
}

void LcdTask::render_right_text(const char *text, int16_t x, int16_t y, const GFXfont *font, uint16_t color, uint8_t size)
{
  _lcd.setFreeFont(font);
  _lcd.setTextSize(size);
  int16_t text_width = _lcd.textWidth(text);
  int16_t text_x = x - text_width;
  _lcd.setTextColor(color);
  _lcd.setCursor(text_x, y);
  _lcd.print(text);
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

#ifdef SMOOTH_FONT
void LcdTask::load_font(const char *filename)
{
  StaticFile *file = NULL;
  if(embedded_get_file(filename, lcd_gui_static_files, ARRAY_LENGTH(lcd_gui_static_files), &file))
  {
    DBUGF("Found %s (%d bytes)", filename, file->length);
    _lcd.loadFont((uint8_t *)file->data);
  }
}
#endif

void LcdTask::png_draw(PNGDRAW *pDraw)
{
  image_render_state *state = (image_render_state *)pDraw->pUser;
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  state->tft->pushImage(state->xpos, state->ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_lcd
