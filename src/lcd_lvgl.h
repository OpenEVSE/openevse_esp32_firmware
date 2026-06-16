#ifndef __LCD_LVGL_H
#define __LCD_LVGL_H

// LVGL renderer for the stock OpenEVSE ILI9488 TFT. Drop-in LcdTask: same public
// API as the TFT_eSPI LcdTask (lcd_tft.h) and the char-LCD LcdTask (lcd.cpp), so
// main.cpp / net / ocpp link unchanged. Selected by ENABLE_SCREEN_LVGL_TFT.

#define LCD_CHAR_STOP       1
#define LCD_CHAR_PLAY       2
#define LCD_CHAR_LIGHTNING  3
#define LCD_CHAR_LOCK       4
#define LCD_CHAR_CLOCK      5

#include "evse_man.h"
#include "scheduler.h"
#include "manual.h"

#define LCD_MAX_LEN 64
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

        Message *getNext() { return _next; }
        void setNext(Message *msg) { _next = msg; }
        const char *getMsg() { return _msg; }
        int getX() { return _x; }
        int getY() { return _y; }
        int getTime() { return _time; }
        bool getClear() { return _clear; }
    };

    Message *_head;
    Message *_tail;
    uint32_t _nextMessageTime;

    EvseManager *_evse;

    bool _initialise = true;
    bool _displayOk = false;

    // Transient message lines (set via display(); auto-cleared after their time).
    char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1];
    bool _msg_cleared = true;

    // WiFi mode, pushed in by net via setWifiMode().
    bool _wifi_client = false;
    bool _wifi_connected = false;

#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    uint32_t _backlight_timeout = 0;
    uint8_t  _prev_state = 0xff;
    bool     _prev_vehicle = false;
    void wakeBacklight();
    void updateBacklight();
#endif

    void display(Message *msg, uint32_t flags);
    unsigned long displayNextMessage();
    void clearMessageLines();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    LcdTask();

    void begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual);

    void display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags);
    void display(String &msg, int x, int y, int time, uint32_t flags);
    void display(const char *msg, int x, int y, int time, uint32_t flags);

    void setWifiMode(bool client, bool connected);
};

extern LcdTask lcd;

#endif // __LCD_LVGL_H
