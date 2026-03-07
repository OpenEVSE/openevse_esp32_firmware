#ifndef __LCD_H
#define __LCD_H

#include <Arduino.h>

#define LCD_CLEAR_LINE    (1 << 0)
#define LCD_DISPLAY_NOW   (1 << 1)

#ifndef LCD_DISPLAY_CHANGE_TIME
#define LCD_DISPLAY_CHANGE_TIME (4 * 1000)
#endif

#if ENABLE_SCREEN_LCD_TFT
// HACK: This should be done in a much more C++ way
#include "lcd_tft.h"
#else

#define LCD_MAX_LEN 16

#define LCD_CHAR_STOP       1
#define LCD_CHAR_PLAY       2
#define LCD_CHAR_LIGHTNING  3
#define LCD_CHAR_LOCK       4
#define LCD_CHAR_CLOCK      5

#include "evse_man.h"
#include "scheduler.h"
#include "manual.h"

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

    enum class LcdInfoLine
    {
      Off,
      EnergySession,  // Energy 1,018Wh
      EnergyTotal,    // Lifetime 2313kWh
      Temperature,    // EVSE Temp 30.5C
      Time,           // Time 03:14PM
      Date,           // Date 08/25/2020
      ElapsedTime,
      BatterySOC,     // Charge level 79%
      ChargeLimit,    // Charge limit 85%
      Range,
      RangeAdded,     // Added 127 miles
      TimeLeft,
      Voltage,        // Voltage 243v AC
      TimerStart,     // Start 10:00PM
      TimerStop,      // Stop 06:00AM
      TimerRemaining, // Remaining 6:23
      ManualOverride
    };

    Message *_head;
    Message *_tail;

    LcdInfoLine _infoLine;

    uint8_t _evseState;
    uint8_t _pilotState;
    uint32_t _flags;

    EvseManager *_evse;
    Scheduler *_scheduler;
    ManualOverride *_manual;

    uint32_t _nextMessageTime;
    uint32_t _infoLineChageTime;

    bool _updateStateDisplay;
    bool _updateInfoLine;

    MicroTasks::EventListener _evseStateEvent;
    MicroTasks::EventListener _evseSettingsEvent;

    LcdInfoLine ledStateFromEvseState(uint8_t);
    void setNewState(bool wake = true);
    int getPriority(LcdInfoLine state);

    void showText(int x, int y, const char *msg, bool clear);

    void setEvseState(uint8_t lcdColour);
    void setInfoLine(LcdInfoLine info);

    void onButton(int event);
    

    LcdInfoLine getNextInfoLine(LcdInfoLine info);

    void display(Message *msg, uint32_t flags);
    unsigned long displayNextMessage();

    void displayStateLine(uint8_t EvseState, unsigned long &nextUpdate);
    void displayInfoLine(LcdInfoLine info, unsigned long &nextUpdate);
    void displayNumberValue(int line, const char *name, double value, int precision, const char *unit);
    void displayScaledNumberValue(int line, const char *name, double value, int precision, const char *unit);
    void displayInfoEventTime(const char *name, Scheduler::EventInstance &event);
    void displayNameValue(int line, const char *name, const char *value);
    void displayStopWatchTime(const char *name, uint32_t time);
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

#endif // ENABLE_SCREEN_LCD_TFT

extern LcdTask lcd;

#endif // __LCD_H

