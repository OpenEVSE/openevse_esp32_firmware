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
#include "screens/screen_manager.h"

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

    // The TFT screen is portrait natively, so we need to rotate it
    const uint16_t _screen_width  = TFT_HEIGHT;
    const uint16_t _screen_height = TFT_WIDTH;

    bool _initialise = true;

    // Screen management
    ScreenManager* _screenManager = nullptr;

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    long _last_backlight_wakeup = 0;
    bool _previous_vehicle_state;
#endif //TFT_BACKLIGHT_TIMEOUT_MS

    char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1];
    bool _msg_cleared;

    void display(Message *msg, uint32_t flags);
    unsigned long displayNextMessage();
    void clearLine(int line);
    void showText(int x, int y, const char *msg, bool clear);

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    void timeoutBacklight();
#endif //TFT_BACKLIGHT_TIMEOUT_MS

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    LcdTask();
    ~LcdTask();

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
extern char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1]; // Message buffer shared with renderers

#endif // __LCD_TFT_H
