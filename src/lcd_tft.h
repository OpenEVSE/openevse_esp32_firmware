#ifndef __LCD_TFT_H
#define __LCD_TFT_H

#define LCD_CHAR_STOP       1
#define LCD_CHAR_PLAY       2
#define LCD_CHAR_LIGHTNING  3
#define LCD_CHAR_LOCK       4
#define LCD_CHAR_CLOCK      5

#include "evse_man.h"
#include "scheduler.h"
#include "manual.h"

//#include <lvgl.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>

#define LCD_MAX_LEN 16
#define LCD_MAX_LINES 2

class LcdTask : public MicroTasks::Task
{
  private:
    class Message;

    class Message
    {
      private:
        Message *_next;
        char _msg[LCD_MAX_LEN + 1];
        int _x;
        int _y;
        int _time;
        uint32_t _clear:1;

      public:
        Message(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags);
        Message(String &msg, int x, int y, int time, uint32_t flags);
        Message(const char *msg, int x, int y, int time, uint32_t flags);

        Message *getNext() {
          return _next;
        }
        void setNext(Message *msg) {
          _next = msg;
        }

        const char *getMsg() {
          return _msg;
        }

        int getX() {
          return _x;
        }

        int getY() {
          return _y;
        }

        int getTime() {
          return _time;
        }

        bool getClear() {
          return _clear;
        }
    };

    Message *_head;
    Message *_tail;
    uint32_t _nextMessageTime;

    TFT_eSPI _tft;                  // The TFT display

#ifdef ENABLE_DOUBLE_BUFFER
    TFT_eSprite _back_buffer;       // The back buffer
    uint16_t *_back_buffer_pixels;
#endif

    TFT_eSPI &_screen;                 // What we are going to write to

    // The TFT screen is portrate natively, so we need to rotate it
    const uint16_t _screen_width  = TFT_HEIGHT;
    const uint16_t _screen_height = TFT_WIDTH;

    enum class State {
      Boot,
      Charge
    };

    State _state = State::Boot;
    bool _full_update = true;
    bool _initialise = true;
    uint16_t _boot_progress = 0;
    EvseManager *_evse;
    Scheduler *_scheduler;
    ManualOverride *_manual;
    uint8_t _previous_evse_state;
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    long _last_backlight_wakeup = 0;
    bool _previous_vehicle_state;
#endif //TFT_BACKLIGHT_TIMEOUT_MS
    bool wifi_client;
    bool wifi_connected;

    char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1];
    bool _msg_cleared;

    static void png_draw(PNGDRAW *pDraw);

    void display(Message *msg, uint32_t flags);
    unsigned long displayNextMessage();
    void clearLine(int line);
    void showText(int x, int y, const char *msg, bool clear);

    String getLine(int line);

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    void timeoutBacklight();
#endif //TFT_BACKLIGHT_TIMEOUT_MS

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

    void render_image(const char *filename, int16_t x, int16_t y);
    void render_text_box(const char *text, int16_t x, int16_t y, int16_t text_x, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t d, uint8_t size);
    void render_centered_text_box(const char *text, int16_t x, int16_t y, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t size = 1);
    void render_right_text_box(const char *text, int16_t x, int16_t y, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t size = 1);
    void render_left_text_box(const char *text, int16_t x, int16_t y, int16_t width, const GFXfont *font, uint16_t text_colour, uint16_t back_colour, bool fill_back, uint8_t size = 1);
    void render_data_box(const char *title, const char *text, int16_t x, int16_t y, int16_t width, int16_t height, bool full_update = true);
    void render_info_box(const char *title, const char *text, int16_t x, int16_t y, int16_t width, int16_t height, bool full_update = true);
    void load_font(const char *filename);

    void get_scaled_number_value(double value, int precision, const char *unit, char *buffer, size_t size);

  public:
    LcdTask();

    void begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual);

    void display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags);
    void display(String &msg, int x, int y, int time, uint32_t flags);
    void display(const char *msg, int x, int y, int time, uint32_t flags);
    void setWifiMode(bool client, bool connected);

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    void wakeBacklight();
#endif //TFT_BACKLIGHT_TIMEOUT_MS
    
    void fill_screen(uint16_t color) {
      _screen.fillScreen(color);
    }
};

extern LcdTask lcd;

#endif // __LCD_TFT_H
