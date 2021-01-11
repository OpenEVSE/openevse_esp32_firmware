#ifndef __LCD_H
#define __LCD_H

#include <Arduino.h>

#define LCD_CLEAR_LINE    (1 << 0)
#define LCD_DISPLAY_NOW   (1 << 1)

#define LCD_MAX_LEN 16

#ifndef LCD_DISPLAY_CHANGE_TIME
#define LCD_DISPLAY_CHANGE_TIME (4 * 1000)
#endif

#define LCD_CHAR_STOP       1
#define LCD_CHAR_PLAY       2
#define LCD_CHAR_LIGHTNING  3
#define LCD_CHAR_LOCK       4
#define LCD_CHAR_CLOCK      5

#define LCD_COLOUR_OFF      0
#define LCD_COLOUR_RED      1
#define LCD_COLOUR_YELLOW   3
#define LCD_COLOUR_GREEN    2
#define LCD_COLOUR_TEAL     6
#define LCD_COLOUR_BLUE     4
#define LCD_COLOUR_VIOLET   5
#define LCD_COLOUR_WHITE    7

#include "evse_man.h"
#include "scheduler.h"

enum LcdInfoLine
{
  LcdInfoLine_Off,
  LcdInfoLine_EnergySession,       // Energy 1,018Wh
  LcdInfoLine_EnergyTotal,       // Lifetime 2313kWh
  LcdInfoLine_Tempurature, // EVSE Temp 30.5C
  LcdInfoLine_Time,        // Time 03:14PM
  LcdInfoLine_Date,        // Date 08/25/2020
  LcdInfoLine_ElapsedTime,
  LcdInfoLine_BatterySOC,  // Charge level 79%
  LcdInfoLine_ChargeLimit, // Charge limit 85%
  LcdInfoLine_Range,
  LcdInfoLine_RangeAdded,  // Added 127 miles
  LcdInfoLine_TimeLeft,
  LcdInfoLine_Voltage,     // Voltage 243v AC
  LcdInfoLine_TimerStart,  // Start 10:00PM
  LcdInfoLine_TimerStop,   // Stop 06:00AM
};

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

    LcdInfoLine _infoLine;

    uint8_t _evseState;
    uint8_t _pilotState;
    uint32_t _flags;

    EvseManager *_evse;
    Scheduler *_scheduler;

    uint32_t _nextMessageTime;
    uint32_t _infoLineChageTime;

    bool _updateStateDisplay;
    bool _updateInfoLine;

    MicroTasks::EventListener _evseStateEvent;

    LcdInfoLine ledStateFromEvseState(uint8_t);
    void setNewState(bool wake = true);
    int getPriority(LcdInfoLine state);

    void display(Message *msg, uint32_t flags);

    void showText(int x, int y, const char *msg, bool clear);

    void setEvseState(uint8_t lcdColour, LcdInfoLine info);
    void setInfoLine(LcdInfoLine info);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    LcdTask();

    void begin(EvseManager &evse, Scheduler &scheduler);

    void display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags);
    void display(String &msg, int x, int y, int time, uint32_t flags);
    void display(const char *msg, int x, int y, int time, uint32_t flags);
};

extern LcdTask lcd;

#endif // __LCD_H
