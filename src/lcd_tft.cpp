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
#define INFO_BOX_WIDTH          190
#define INFO_BOX_HEIGHT         56

#define BOOT_PROGRESS_WIDTH     300
#define BOOT_PROGRESS_HEIGHT    16
#define BOOT_PROGRESS_X         ((TFT_SCREEN_WIDTH - BOOT_PROGRESS_WIDTH) / 2)
#define BOOT_PROGRESS_Y         235

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
  _tft(),
#ifdef ENABLE_DOUBLE_BUFFER
  _back_buffer(&_tft),
  _screen(_back_buffer)
#else
  _screen(_tft)
#endif
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

  if(_initialise)
  {
    // We need to initialise after the Networking as that brackes the display
    DBUGVAR(ESP.getFreeHeap());
    _tft.init();
    _tft.setRotation(1);
    DBUGF("Screen initialised, size: %dx%d", _screen_width, _screen_height);

#ifdef ENABLE_DOUBLE_BUFFER
    _back_buffer_pixels = (uint16_t *)_back_buffer.createSprite(_screen_width, _screen_height);
    _back_buffer.setTextDatum(MC_DATUM);
    DBUGF("Back buffer %p", _back_buffer_pixels);
#endif

    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

    _initialise = false;
  }

#ifdef ENABLE_DOUBLE_BUFFER
  _tft.startWrite();
#endif

  switch(_state)
  {
    case State::Boot:
    {
      DBUGLN("LCD UI setup");

      if(_full_update)
      {
        _screen.fillScreen(TFT_OPENEVSE_BACK);
        _screen.fillSmoothRoundRect(90, 90, 300, 110, 15, TFT_WHITE);
        render_image("/logo.png", 104, 115);
        _full_update = false;
      }

      TFT_eSprite sprite(&_screen);
      uint16_t *pixels = (uint16_t *)sprite.createSprite(BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT);
      if(nullptr == pixels)
      {
        DBUGF("Failed to create sprite for boot progress %d x %d", BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT);
        break;
      }
      sprite.fillSprite(TFT_OPENEVSE_BACK);
      sprite.fillRoundRect(0, 0, BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT, 8, TFT_WHITE);
      if(_boot_progress > 0) {
        sprite.fillRoundRect(0, 0, _boot_progress, BOOT_PROGRESS_HEIGHT, 8, TFT_OPENEVSE_GREEN);
      }
      _screen.startWrite();
      _screen.pushImage(BOOT_PROGRESS_X, BOOT_PROGRESS_Y, BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT, pixels);
      _screen.endWrite();
      sprite.deleteSprite();
      _boot_progress += 10;

      nextUpdate = 166;
      if(_boot_progress >= 300) {
        _state = State::Charge;
        _full_update = true;
      }
    } break;

    case State::Charge:
    {
      if(_full_update)
      {
        _screen.fillRect(DISPLAY_AREA_X, DISPLAY_AREA_Y, DISPLAY_AREA_WIDTH, DISPLAY_AREA_HEIGHT, TFT_OPENEVSE_BACK);
        _screen.fillSmoothRoundRect(WHITE_AREA_X, WHITE_AREA_Y, WHITE_AREA_WIDTH, WHITE_AREA_HEIGHT, 6, TFT_WHITE);
        render_image("/button_bar.png", BUTTON_BAR_X, BUTTON_BAR_Y);
      }

      String status_icon = "/car_disconnected.png";
      if(_evse->isVehicleConnected())
      {
        switch (_evse->getEvseState())
        {
          case OPENEVSE_STATE_STARTING:
            status_icon = "/start.png";
            break;
          case OPENEVSE_STATE_NOT_CONNECTED:
            status_icon = "/not_connected.png";
            break;
          case OPENEVSE_STATE_CONNECTED:
            status_icon = "/connected.png";
            break;
          case OPENEVSE_STATE_CHARGING:
            status_icon = "/charging.png";
            break;
          case OPENEVSE_STATE_VENT_REQUIRED:
          case OPENEVSE_STATE_DIODE_CHECK_FAILED:
          case OPENEVSE_STATE_GFI_FAULT:
          case OPENEVSE_STATE_NO_EARTH_GROUND:
          case OPENEVSE_STATE_STUCK_RELAY:
          case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
          case OPENEVSE_STATE_OVER_TEMPERATURE:
          case OPENEVSE_STATE_OVER_CURRENT:
            status_icon = "/error.png";
            break;
          case OPENEVSE_STATE_SLEEPING:
            status_icon = "/sleeping.png";
            break;
          case OPENEVSE_STATE_DISABLED:
            status_icon = "/disabled.png";
            break;
          default:
            break;
        }
      }

      render_image(status_icon.c_str(), 16, 52);

      char buffer[32];

      snprintf(buffer, sizeof(buffer), "%d", _evse->getChargeCurrent());
      render_right_text_box(buffer, 66, 220, 154, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, !_full_update, 3);
      if(_full_update) {
        render_left_text_box("A", 224, 200, 34, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, false, 1);
      }

      render_centered_text_box(esp_hostname.c_str(), INFO_BOX_X, 74, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT, TFT_WHITE, !_full_update);

      timeval local_time;
      gettimeofday(&local_time, NULL);
      struct tm timeinfo;
      localtime_r(&local_time.tv_sec, &timeinfo);
      strftime(buffer, sizeof(buffer), "%d/%m/%Y, %l:%M %p", &timeinfo);
      render_centered_text_box(buffer, INFO_BOX_X, 96, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT, TFT_WHITE, !_full_update);

      uint32_t elapsed = _evse->getSessionElapsed();
      uint32_t hours = elapsed / 3600;
      uint32_t minutes = (elapsed % 3600) / 60;
      uint32_t seconds = elapsed % 60;
      snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
      render_info_box("ELAPSED", buffer, INFO_BOX_X, 110, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update);

      get_scaled_number_value(_evse->getSessionEnergy(), 0, "Wh", buffer, sizeof(buffer));
      render_info_box("DELIVERED", buffer, INFO_BOX_X, 175, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update);

      nextUpdate = 1000;
      _full_update = false;
    } break;

    default:
      break;
  }

#ifdef ENABLE_DOUBLE_BUFFER
  _tft.pushImage(0, 0, _screen_width, _screen_height, _back_buffer_pixels);
  _tft.endWrite();
#endif

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

void LcdTask::render_info_box(const char *title, const char *text, int16_t x, int16_t y, int16_t width, int16_t height, bool full_update)
{
  if(full_update)
  {
    _screen.fillSmoothRoundRect(x, y, width, height, 6, TFT_OPENEVSE_INFO_BACK, TFT_WHITE);
    render_centered_text_box(title, x, y+24, width, &FreeSans9pt7b, TFT_OPENEVSE_GREEN, TFT_OPENEVSE_INFO_BACK, false);
  }
  render_centered_text_box(text, x, y+(height-4), width, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_INFO_BACK, !full_update);
}

void LcdTask::render_centered_text_box(const char *text, int16_t x, int16_t y, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t size)
{
  render_text_box(text, x, y, (width / 2), width, font, text_colour, back_colour, fill_back, BC_DATUM, size);
}

void LcdTask::render_right_text_box(const char *text, int16_t x, int16_t y, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t size)
{
  render_text_box(text, x, y, width, width, font, text_colour, back_colour, fill_back, BR_DATUM, size);
}

void LcdTask::render_left_text_box(const char *text, int16_t x, int16_t y, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t size)
{
  render_text_box(text, x, y, 0, width, font, text_colour, back_colour, fill_back, BL_DATUM, size);
}

void LcdTask::render_text_box(const char *text, int16_t x, int16_t y, int16_t text_x, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t d, uint8_t size)
{
  TFT_eSprite sprite(&_screen);

  sprite.setFreeFont(font);
  sprite.setTextSize(size);
  sprite.setTextDatum(d);
  sprite.setTextColor(text_colour, back_colour);

  int16_t height = sprite.fontHeight();
  uint16_t *pixels = (uint16_t *)sprite.createSprite(width, height);
  if(nullptr == pixels)
  {
    DBUGF("Failed to create sprite for text box %d x %d", width, height);
    return;
  }

  sprite.fillSprite(back_colour);
  sprite.drawString(text, text_x, height);

  _screen.startWrite();
  _screen.pushImage(x, y - height, width, height, pixels);
  _screen.endWrite();

  sprite.deleteSprite();
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
      _screen.startWrite();
      uint32_t dt = millis();
      image_render_state state = {&_screen, x, y};
      rc = png.decode(&state, 0);
      DBUG(millis() - dt); DBUGLN("ms");
      _screen.endWrite();
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
