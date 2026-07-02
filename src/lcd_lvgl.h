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
    bool _booting = false;       // showing the boot splash before the main screen
    uint32_t _bootStart = 0;
    uint8_t _activeScreen = 0;   // 0 = boot, 1 = setup (AP), 2 = charge
    bool _wifiModeKnown = false; // has setWifiMode() been called yet?

    // Transient message lines (set via display(); auto-cleared after their time).
    char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1];
    bool _msg_cleared = true;

    // WiFi mode, pushed in by net via setWifiMode().
    bool _wifi_client = false;
    bool _wifi_connected = false;

    // Active display theme last applied from the tft_theme config (-1 = none yet,
    // 0 = dark/nightshift, 1 = light). Polled in loop(); a change repaints.
    int8_t _themeLight = -1;
    bool applyThemeFromConfig();  // sets the palette; true if it changed

    // Backlight + standby (PWM). Brightness 0..100; idle measured from _lastWake.
    uint32_t _lastWake = 0;
    uint8_t  _prev_state = 0xff;
    bool     _prev_vehicle = false;
    bool     _standby = false;          // currently dimmed to the standby screen/level
    // int32_t with a -1 sentinel = "not read yet"; only -1 until the first
    // applyDisplayConfig() in init() (a uint32 config cast can't go negative after).
    int32_t  _activeBrightness = -1;
    int32_t  _standbyBrightness = -1;
    int32_t  _timeoutS = -1;
    void wakeBacklight();               // active brightness, exit standby, re-arm idle
    void enterStandby();                // standby brightness (+ standby screen if >0)
    bool stateKeepsAwake(uint8_t state, bool vehicle, double amps);  // charging/fault force-bright
    void applyDisplayConfig();          // refresh cached brightness/timeout + apply live

    void display(Message *msg, uint32_t flags);
    unsigned long displayNextMessage();
    void clearMessageLines();
    void buildSetupScreen();     // gather AP creds + build the QR setup screen

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
