#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#ifndef LCD_BACKLIGHT_PIN
#define LCD_BACKLIGHT_PIN TFT_BL
#endif

#include <WiFi.h>
#include "emonesp.h"
#include "lcd.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"
#include "app_config.h"
#include <sys/time.h>
#include "embedded_files.h"
//#include "fonts/DejaVu_Sans_72.h"

//#define TFT_BACKLIGHT_TIMEOUT_MS 600000  //timeout backlight after 10 minutes
//#define TFT_BACKLIGHT_CHARGING_THRESHOLD 0.1  //stay awake if car is drawing more than this many amps

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
#define BOOT_PROGRESS_Y         195

#include "web_server.h"

PNG png;

#include "lcd_static/lcd_gui_static_files.h"

#define MAX_IMAGE_WIDTH TFT_HEIGHT // Adjust for your images

struct image_render_state {
  TFT_eSPI *tft;
  int16_t xpos;
  int16_t ypos;
};

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
    clearLine(i);
  }
  _msg_cleared = true;
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
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  if (client != wifi_client || connected != wifi_connected) {
    wakeBacklight();
  }
#endif //TFT_BACKLIGHT_TIMEOUT_MS
  wifi_client = client;
  wifi_connected = connected;
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
#ifdef TFT_BACKLIGHT_TIMEOUT_MS    
    wakeBacklight();
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
      clearLine(i);
    }
    _msg_cleared = true;
  }

#ifdef ENABLE_DOUBLE_BUFFER
  _tft.startWrite();
#endif

  uint8_t evse_state = _evse->getEvseState();

  switch(_state)
  {
    case State::Boot:
    {
      DBUGLN("LCD UI setup");

      if(_full_update)
      {
        _screen.fillScreen(TFT_OPENEVSE_BACK);
        _screen.fillSmoothRoundRect(90, 60, 300, 110, 15, TFT_WHITE);
        render_image("/logo.png", 104, 85);
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

      String line = getLine(0);
      if(line.length() > 0) {
        render_centered_text_box(line.c_str(), 0, 250, TFT_SCREEN_WIDTH, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, !_full_update);
      }
      line = getLine(1);
      if(line.length() > 0) {
        render_centered_text_box(line.c_str(), 0, 270, TFT_SCREEN_WIDTH, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, !_full_update);
      }

      nextUpdate = 166;
      if(_boot_progress >= 300) {
        _state = State::Charge;
        _full_update = true;
      }
    } break;

    case State::Charge:
    { 
      //redraw when going in or out of charging state to switch between pilot and power display
      if ((evse_state == OPENEVSE_STATE_CHARGING || _previous_evse_state == OPENEVSE_STATE_CHARGING) && evse_state != _previous_evse_state) {  
        _full_update = true;
      }

      if(_full_update)
      {
        _screen.fillRect(DISPLAY_AREA_X, DISPLAY_AREA_Y, DISPLAY_AREA_WIDTH, DISPLAY_AREA_HEIGHT, TFT_OPENEVSE_BACK);
        _screen.fillSmoothRoundRect(WHITE_AREA_X, WHITE_AREA_Y, WHITE_AREA_WIDTH, WHITE_AREA_HEIGHT, 6, TFT_WHITE);
        render_image("/button_bar.png", BUTTON_BAR_X, BUTTON_BAR_Y);
      }

      String status_icon = "/disabled.png";
      String car_icon = "/car_disconnected.png";
      String wifi_icon = "/no_wifi.png";

      if(_evse->isVehicleConnected()) {
        car_icon = "/car_connected.png";
      }

      switch (evse_state)
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

      char buffer[32] = "";
      char buffer2[12];

      if (wifi_client) {
        if (wifi_connected) {
          wifi_icon = "/wifi.png";
          snprintf(buffer, sizeof(buffer), "%ddB", WiFi.RSSI());
        }
      } else {
        if (wifi_connected) {
          wifi_icon = "/access_point_connected.png";
          snprintf(buffer, sizeof(buffer), "%d", WiFi.softAPgetStationNum());
        } else {
          wifi_icon = "/access_point.png";
        }
      }
      render_right_text_box(buffer, 350, 30, 50, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, false, 1);
    
      render_image(status_icon.c_str(), 16, 52);
      render_image(car_icon.c_str(), 16, 92);
      render_image(wifi_icon.c_str(), 16, 132);


      if (evse_state == OPENEVSE_STATE_CHARGING) {
        float power = _evse->getPower() / 1000.0;  //kW
        if (power < 10) { 
          snprintf(buffer, sizeof(buffer), "%.2f", power);
        } else if (power < 100) {
          snprintf(buffer, sizeof(buffer), "%.1f", power);
        } else {
          snprintf(buffer, sizeof(buffer), "%.0f", power);
        }
        render_left_text_box(buffer, 66, 157, 188, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, !_full_update, 2);
        render_left_text_box("kW", 224, 165, 34, &FreeSans9pt7b, TFT_BLACK, TFT_WHITE, false, 1);
      } else {
        snprintf(buffer, sizeof(buffer), "%d", _evse->getChargeCurrent());
        render_right_text_box(buffer, 66, 175, 154, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, !_full_update, 2);
        if (_full_update) {
          render_left_text_box("A", 224, 165, 34, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, false, 1);
        }
      }
      if (_evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)) {
        snprintf(buffer, sizeof(buffer), "%.1fC", _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR));
        render_right_text_box(buffer, 415, 30, 50, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, false, 1);
      }

      snprintf(buffer, sizeof(buffer), "%.1f V  %.2f A", _evse->getVoltage(), _evse->getAmps());
      if (evse_state == OPENEVSE_STATE_CHARGING) {
        snprintf(buffer2, sizeof(buffer2), "Pilot: %dA", _evse->getChargeCurrent());
      } else {
        get_scaled_number_value(_evse->getPower(), 2, "W", buffer2, sizeof(buffer2));
      }
      render_data_box(buffer2, buffer, 66, 175, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update);

      String line = getLine(0);
      if(line.length() == 0) {
        line = esp_hostname;
      }
      render_centered_text_box(line.c_str(), INFO_BOX_X, 74, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT, TFT_WHITE, !_full_update);

      line = getLine(1);
      render_centered_text_box(line.c_str(), INFO_BOX_X, 96, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT, TFT_WHITE, !_full_update);

      uint32_t elapsed = _evse->getSessionElapsed();
      uint32_t hours = elapsed / 3600;
      uint32_t minutes = (elapsed % 3600) / 60;
      uint32_t seconds = elapsed % 60;
      snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
      render_info_box("ELAPSED", buffer, INFO_BOX_X, 110, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update);

      get_scaled_number_value(_evse->getSessionEnergy(), 0, "Wh", buffer, sizeof(buffer));
      render_info_box("DELIVERED", buffer, INFO_BOX_X, 175, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update);

      timeval local_time;
      gettimeofday(&local_time, NULL);
      struct tm timeinfo;
      localtime_r(&local_time.tv_sec, &timeinfo);
      strftime(buffer, sizeof(buffer), "%Y-%m-%d  %H:%M:%S", &timeinfo);
      render_left_text_box(buffer, 12, 30, 175, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, false, 1);

      _previous_evse_state = evse_state;

      //sleep until next whole second so clock doesn't skip
      gettimeofday(&local_time, NULL);
      nextUpdate = 1000 - local_time.tv_usec/1000;
      _full_update = false;
    } break;

    default:
      break;
  }

#ifdef ENABLE_DOUBLE_BUFFER
  _tft.pushImage(0, 0, _screen_width, _screen_height, _back_buffer_pixels);
  _tft.endWrite();
#endif

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  bool vehicle_state = _evse->isVehicleConnected();
  if (evse_state != _previous_evse_state || vehicle_state != _previous_vehicle_state) {  //wake backlight on state change
    wakeBacklight();
    _previous_vehicle_state = vehicle_state;
  } else {  //otherwise timeout backlight in appropriate states
    bool timeout = true;
    if (_evse->isVehicleConnected()) {
          switch (_evse->getEvseState()) {
            case OPENEVSE_STATE_STARTING:
            case OPENEVSE_STATE_VENT_REQUIRED:
            case OPENEVSE_STATE_DIODE_CHECK_FAILED:
            case OPENEVSE_STATE_GFI_FAULT:
            case OPENEVSE_STATE_NO_EARTH_GROUND:
            case OPENEVSE_STATE_STUCK_RELAY:
            case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
            case OPENEVSE_STATE_OVER_TEMPERATURE:
            case OPENEVSE_STATE_OVER_CURRENT:
              timeout = false;
              break;
            case OPENEVSE_STATE_NOT_CONNECTED:
            case OPENEVSE_STATE_CONNECTED:
            case OPENEVSE_STATE_SLEEPING:
            case OPENEVSE_STATE_DISABLED:
              timeout = true;
              break;
            case OPENEVSE_STATE_CHARGING:
#ifdef TFT_BACKLIGHT_CHARGING_THRESHOLD
              if (_evse->getAmps() >= TFT_BACKLIGHT_CHARGING_THRESHOLD) {
                //Reset the timer here, so we don't timeout as soon as charging stops.
                //This will also wake the backlight if the vehicle starts drawing current again.
                wakeBacklight();  
                timeout = false;
              }
#else
              timeout = false;
#endif //TFT_BACKLIGHT_CHARGING_THRESHOLD
              break;
            default:
              timeout = true;
              break;
          }
        }
    if (timeout) {
      timeoutBacklight();
    }
  }
#endif //TFT_BACKLIGHT_TIMEOUT_MS
  _previous_evse_state = evse_state;
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

void LcdTask::render_data_box(const char *title, const char *text, int16_t x, int16_t y, int16_t width, int16_t height, bool full_update)
{
  if(full_update)
  {
    _screen.fillSmoothRoundRect(x, y, width, height, 6, TFT_OPENEVSE_INFO_BACK, TFT_WHITE);
  }
  render_centered_text_box(title, x, y+24, width, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_INFO_BACK, !full_update);
  render_centered_text_box(text, x, y+(height-4), width, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_INFO_BACK, !full_update);
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
    // DBUGF("Found %s (%d bytes)", filename, file->length);
    int16_t rc = png.openFLASH((uint8_t *)file->data, file->length, png_draw);
    if (rc == PNG_SUCCESS)
    {
      // DBUGLN("Successfully opened png file");
      // DBUGF("image specs: (%d x %d), %d bpp, pixel type: %d", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
      _screen.startWrite();
      uint32_t dt = millis();
      image_render_state state = {&_screen, x, y};
      rc = png.decode(&state, 0);
      // DBUG(millis() - dt); DBUGLN("ms");
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
    wakeBacklight();
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
}

void LcdTask::clearLine(int line)
{
  if(line < 0 || line >= LCD_MAX_LINES) {
    return;
  }

  memset(_msg[line], ' ', LCD_MAX_LEN);
  _msg[line][LCD_MAX_LEN] = '\0';
}

String LcdTask::getLine(int line)
{
  if(line < 0 || line >= LCD_MAX_LINES) {
    return "";
  }

  // trim leading and trailing spaces
  int len = LCD_MAX_LEN;
  while(len > 0 && _msg[line][len - 1] == ' ') {
    len--;
  }
  char *start = _msg[line];
  while(len > 0 && *start == ' ') {
    start++;
    len--;
  }

  return String(start, len);
}

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
void LcdTask::wakeBacklight() {
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
  _last_backlight_wakeup = millis();
}

void LcdTask::timeoutBacklight() {
  if (millis() - _last_backlight_wakeup >= TFT_BACKLIGHT_TIMEOUT_MS) {
    digitalWrite(LCD_BACKLIGHT_PIN, LOW);
  }
}
#endif //TFT_BACKLIGHT_TIMEOUT_MS

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_lcd
