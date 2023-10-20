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

#define TFT_OPENEVSE_BACK       0x2413
#define TFT_OPENEVSE_GREEN      0x3E92
#define TFT_OPENEVSE_TEXT       0x1BD1
#define TFT_OPENEVSE_INFO_BACK  0x23d1

// The TFT is natively portrait but we are rendering as landscape
#define TFT_SCREEN_WIDTH        TFT_HEIGHT
#define TFT_SCREEN_HEIGHT       TFT_WIDTH

#define BUTTON_BAR_X            0
#define BUTTON_BAR_Y            (TFT_SCREEN_HEIGHT - BUTTON_BAR_HEIGHT)
#define BUTTON_BAR_HEIGHT       55
#define BUTTON_BAR_WIDTH        TFT_SCREEN_WIDTH

#define DISPLAY_AREA_X          0
#define DISPLAY_AREA_Y          0
#define DISPLAY_AREA_WIDTH      TFT_SCREEN_WIDTH
#define DISPLAY_AREA_HEIGHT     (TFT_SCREEN_HEIGHT - BUTTON_BAR_HEIGHT)

#define WHITE_AREA_BOARDER      8
#define WHITE_AREA_X            WHITE_AREA_BOARDER
#define WHITE_AREA_Y            45
#define WHITE_AREA_WIDTH        (DISPLAY_AREA_WIDTH - (2 * 8))
#define WHITE_AREA_HEIGHT       (DISPLAY_AREA_HEIGHT - (WHITE_AREA_Y + 20))


#define INFO_BOX_BOARDER        8
#define INFO_BOX_X              ((WHITE_AREA_X + WHITE_AREA_WIDTH) - (INFO_BOX_WIDTH + INFO_BOX_BOARDER))
#define INFO_BOX_WIDTH          170
#define INFO_BOX_HEIGHT         56

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

      _lcd.fillRect(DISPLAY_AREA_X, DISPLAY_AREA_Y, DISPLAY_AREA_WIDTH, DISPLAY_AREA_HEIGHT, TFT_OPENEVSE_BACK);
      _lcd.fillSmoothRoundRect(WHITE_AREA_X, WHITE_AREA_Y, WHITE_AREA_WIDTH, WHITE_AREA_HEIGHT, 6, TFT_WHITE);
      render_image("/button_bar.png", BUTTON_BAR_X, BUTTON_BAR_Y);
      render_image("/not_connected.png", 16, 52);

      char buffer[32];

      snprintf(buffer, sizeof(buffer), "%d", _evse->getChargeCurrent());
      render_right_text(buffer, 220, 200, &FreeSans24pt7b, TFT_BLACK, 3);
      _lcd.setTextSize(1);
      _lcd.print("A");

      render_centered_text(esp_hostname.c_str(), INFO_BOX_X, 72, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT);
      render_centered_text("11/08/2023, 3:45 AM", INFO_BOX_X, 94, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT);

      render_info_box("ELAPSED", "00:00:00", INFO_BOX_X, 110, INFO_BOX_WIDTH, INFO_BOX_HEIGHT);

      get_scaled_number_value(_evse->getSessionEnergy(), 0, "Wh", buffer, sizeof(buffer));
      render_info_box("DELIVERED", buffer, INFO_BOX_X, 175, INFO_BOX_WIDTH, INFO_BOX_HEIGHT);

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

void LcdTask::render_info_box(const char *title, const char *text, int16_t x, int16_t y, int16_t width, int16_t height)
{
  _lcd.fillSmoothRoundRect(x, y, width, height, 6, TFT_OPENEVSE_INFO_BACK, TFT_WHITE);
  render_centered_text(title, x, y+22, width, &FreeSans9pt7b, TFT_OPENEVSE_GREEN);
  render_centered_text(text, x, y+(height-10), width, &FreeSans9pt7b, TFT_WHITE);
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
