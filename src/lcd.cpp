#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "lcd.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"
#include <time.h>

static void IGNORE(int ret) {
}

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
  _infoLine(LcdInfoLine_Off),
  _evseState(OPENEVSE_STATE_STARTING),
  _evse(NULL),
  _scheduler(NULL),
  _nextMessageTime(0),
  _evseStateEvent(this)
{
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
    MicroTask.wakeTask(this);
  }
}

void LcdTask::display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags)
{
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::display(String &msg, int x, int y, int time, uint32_t flags)
{
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::display(const char *msg, int x, int y, int time, uint32_t flags)
{
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::setEvseState(uint8_t lcdColour, LcdInfoLine info)
{
  _evse->getOpenEVSE().lcdSetColour(lcdColour, IGNORE);
  setInfoLine(info);
}

void LcdTask::setInfoLine(LcdInfoLine info)
{
  _infoLine = info;
  if(LcdInfoLine_Off != info)
  {
    _updateInfoLine = true;
    _infoLineChageTime = millis() + LCD_DISPLAY_CHANGE_TIME;
  }
}

void LcdTask::begin(EvseManager &evse, Scheduler &scheduler)
{
  _evse = &evse;
  _scheduler = &scheduler;
  MicroTask.startTask(this);
}

void LcdTask::setup()
{
  _evse->onStateChange(&_evseStateEvent);
}

unsigned long LcdTask::loop(MicroTasks::WakeReason reason)
{
  unsigned long nextUpdate = MicroTask.Infinate;

  DBUG("LCD UI woke: ");
  DBUG(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");
  DBUG(" ");
  DBUGLN(LcdInfoLine_Off == _infoLine ? "LcdInfoLine_Off" :
         LcdInfoLine_EnergySession == _infoLine ? "LcdInfoLine_EnergySession" :
         LcdInfoLine_EnergyTotal == _infoLine ? "LcdInfoLine_EnergyTotal" :
         LcdInfoLine_Tempurature == _infoLine ? "LcdInfoLine_Tempurature" :
         LcdInfoLine_Time == _infoLine ? "LcdInfoLine_Time" :
         LcdInfoLine_Date == _infoLine ? "LcdInfoLine_Date" :
         LcdInfoLine_ElapsedTime == _infoLine ? "LcdInfoLine_ElapsedTime" :
         LcdInfoLine_BatterySOC == _infoLine ? "LcdInfoLine_BatterySOC" :
         LcdInfoLine_ChargeLimit == _infoLine ? "LcdInfoLine_ChargeLimit" :
         LcdInfoLine_Range == _infoLine ? "LcdInfoLine_Range" :
         LcdInfoLine_RangeAdded == _infoLine ? "LcdInfoLine_RangeAdded" :
         LcdInfoLine_TimeLeft == _infoLine ? "LcdInfoLine_TimeLeft" :
         LcdInfoLine_Voltage == _infoLine ? "LcdInfoLine_Voltage" :
         LcdInfoLine_TimerStart == _infoLine ? "LcdInfoLine_TimerStart" :
         LcdInfoLine_TimerStop == _infoLine ? "LcdInfoLine_TimerStop" :
         LcdInfoLine_TimerRemaining == _infoLine ? "LcdInfoLine_TimerRemaining" :
         "UNKNOWN");

  bool evseStateChanged = false;
  uint8_t newEvseState = _evse->getEvseState();
  if(newEvseState != _evseState)
  {
    if(OPENEVSE_STATE_STARTING == _evseState)
    {
      // We have just started, disable the LCD and button, we are joing to handle them
      _evse->getOpenEVSE().feature(OPENEVSE_FEATURE_BUTTON, false, IGNORE);
      _evse->getOpenEVSE().lcdEnable(false, IGNORE);
    }

    _evseState = newEvseState;
    DBUGVAR(_evseState);

    evseStateChanged = true;
  }

  bool pilotStateChanged = false;
  uint8_t newPilotState = _evse->getPilotState();
  if(newPilotState != _pilotState)
  {
    _pilotState = newPilotState;
    DBUGVAR(_pilotState);

    pilotStateChanged = true;
  }

  bool flagsChanged = false;
  uint32_t newFlags = _evse->getFlags();
  if(newFlags != _flags)
  {
    _flags = newFlags;
    DBUGVAR(_flags, HEX);

    flagsChanged = true;
  }

  DBUG("EVSE state: ");
  DBUG(OPENEVSE_STATE_STARTING == _evseState ? "OPENEVSE_STATE_STARTING" :
       OPENEVSE_STATE_NOT_CONNECTED == _evseState ? "OPENEVSE_STATE_NOT_CONNECTED" :
       OPENEVSE_STATE_CONNECTED == _evseState ? "OPENEVSE_STATE_CONNECTED" :
       OPENEVSE_STATE_CHARGING == _evseState ? "OPENEVSE_STATE_CHARGING" :
       OPENEVSE_STATE_VENT_REQUIRED == _evseState ? "OPENEVSE_STATE_VENT_REQUIRED" :
       OPENEVSE_STATE_DIODE_CHECK_FAILED == _evseState ? "OPENEVSE_STATE_DIODE_CHECK_FAILED" :
       OPENEVSE_STATE_GFI_FAULT == _evseState ? "OPENEVSE_STATE_GFI_FAULT" :
       OPENEVSE_STATE_NO_EARTH_GROUND == _evseState ? "OPENEVSE_STATE_NO_EARTH_GROUND" :
       OPENEVSE_STATE_STUCK_RELAY == _evseState ? "OPENEVSE_STATE_STUCK_RELAY" :
       OPENEVSE_STATE_GFI_SELF_TEST_FAILED == _evseState ? "OPENEVSE_STATE_GFI_SELF_TEST_FAILED" :
       OPENEVSE_STATE_OVER_TEMPERATURE == _evseState ? "OPENEVSE_STATE_OVER_TEMPERATURE" :
       OPENEVSE_STATE_OVER_CURRENT == _evseState ? "OPENEVSE_STATE_OVER_CURRENT" :
       OPENEVSE_STATE_SLEEPING == _evseState ? "OPENEVSE_STATE_SLEEPING" :
       OPENEVSE_STATE_DISABLED == _evseState ? "OPENEVSE_STATE_DISABLED" :
       "UNKNOWN");
  DBUGF(", flags: %08x", _flags);

  // If the OpenEVSE has not started don't do anything
  if(OPENEVSE_STATE_STARTING == _evseState) {
    return MicroTask.Infinate;
  }

  if(evseStateChanged)
  {
    // Set the LCD background colour based on the EVSE state and deal with any
    // resulting state changes
    switch(_evseState)
    {
      case OPENEVSE_STATE_STARTING:
        // Do nothing
        break;
      case OPENEVSE_STATE_NOT_CONNECTED:
        setEvseState(LCD_COLOUR_GREEN, LcdInfoLine_EnergyTotal);
        break;
      case OPENEVSE_STATE_CONNECTED:
        setEvseState(LCD_COLOUR_YELLOW, LcdInfoLine_EnergyTotal);
        break;
      case OPENEVSE_STATE_CHARGING:
        // TODO: Colour should also take into account the tempurature, >60 YELLOW
        setEvseState(LCD_COLOUR_TEAL, LcdInfoLine_EnergyTotal);
        break;
      case OPENEVSE_STATE_VENT_REQUIRED:
      case OPENEVSE_STATE_DIODE_CHECK_FAILED:
      case OPENEVSE_STATE_GFI_FAULT:
      case OPENEVSE_STATE_NO_EARTH_GROUND:
      case OPENEVSE_STATE_STUCK_RELAY:
      case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
      case OPENEVSE_STATE_OVER_TEMPERATURE:
      case OPENEVSE_STATE_OVER_CURRENT:
        setEvseState(LCD_COLOUR_RED, LcdInfoLine_Off);
        break;
      case OPENEVSE_STATE_SLEEPING:
      case OPENEVSE_STATE_DISABLED:
        setEvseState(_evse->isVehicleConnected() ? LCD_COLOUR_WHITE : LCD_COLOUR_VIOLET, LcdInfoLine_Time);
        break;
      default:
        break;
    }

    _updateStateDisplay = true;
  }
  else if(flagsChanged)
  {
    switch(_evseState)
    {
      case OPENEVSE_STATE_SLEEPING:
      case OPENEVSE_STATE_DISABLED:
        _evse->getOpenEVSE().lcdSetColour(_evse->isVehicleConnected() ? LCD_COLOUR_WHITE : LCD_COLOUR_VIOLET, IGNORE);
        break;
      default:
        break;
    }
  }

  // If we have messages to display, do it
  if(_head)
  {
    DBUGVAR(millis());
    DBUGVAR(_nextMessageTime);
    while(millis() >= _nextMessageTime)
    {
      // Pop a message from the queue
      Message *msg = _head;
      _head = _head->getNext();
      if(NULL == _head) {
        _tail = NULL;
      }

      // Display the message
      showText(msg->getX(), msg->getY(), msg->getMsg(), msg->getClear());

      _nextMessageTime = millis() + msg->getTime();

      // delete the message
      delete msg;

      _updateStateDisplay = true;
      _updateInfoLine = true;
    }

    return _nextMessageTime - millis();
  }

  // Else display the status
  char temp[20];

  if(millis() >= _infoLineChageTime)
  {
    switch(_evseState)
    {
      case OPENEVSE_STATE_NOT_CONNECTED:
      case OPENEVSE_STATE_CONNECTED:
        // Line 1 "Energy 1,018Wh"
        //        "Lifetime 2313kWh"
        //        "EVSE Temp 30.5C"
        //        "Time 03:14PM"
        //        "Date 08/25/2020"

        switch(_infoLine)
        {
          case LcdInfoLine_EnergySession:
            setInfoLine(LcdInfoLine_EnergyTotal);
            break;
          case LcdInfoLine_EnergyTotal:
            setInfoLine(LcdInfoLine_Tempurature);
            break;
          case LcdInfoLine_Tempurature:
            setInfoLine(LcdInfoLine_Time);
            break;
          case LcdInfoLine_Time:
            setInfoLine(LcdInfoLine_Date);
            break;
          case LcdInfoLine_Date:
            setInfoLine(_scheduler->getNextEvent().isValid() ?
                        LcdInfoLine_TimerStart :
                        LcdInfoLine_EnergySession);
            break;
          case LcdInfoLine_TimerStart:
            setInfoLine(_scheduler->getNextEvent().isValid() ?
                        LcdInfoLine_TimerStop :
                        LcdInfoLine_EnergySession);
          default:
            setInfoLine(LcdInfoLine_EnergySession);
            break;
        }

        break;
      case OPENEVSE_STATE_CHARGING:
        // Line 1 "Energy 1,018Wh"
        //        "Lifetime 2313kWh"
        //        "EVSE Temp 45.5C"
        //        if Tesla "Range 259 miles"
        //                 "Charge level 79%"
        //                 "Charge limit 85%"
        //                 "Added 127 miles"
        //        "Voltage 243v AC"
        //        if timer "Stop 06:00AM"

        switch(_infoLine)
        {
          case LcdInfoLine_EnergySession:
            setInfoLine(LcdInfoLine_EnergyTotal);
            break;
          case LcdInfoLine_EnergyTotal:
            setInfoLine(LcdInfoLine_Tempurature);
            break;
          case LcdInfoLine_Tempurature:
            setInfoLine(_scheduler->getNextEvent().isValid() ?
                        LcdInfoLine_TimerStop :
                        LcdInfoLine_EnergySession);
            break;
          case LcdInfoLine_TimerStop:
            setInfoLine(_scheduler->getNextEvent().isValid() ?
                        LcdInfoLine_TimerRemaining :
                        LcdInfoLine_EnergySession);
            break;
          default:
            setInfoLine(LcdInfoLine_EnergySession);
            break;
        }
        break;
      case OPENEVSE_STATE_SLEEPING:
      case OPENEVSE_STATE_DISABLED:
        // Line 1 "Time 03:14PM"
        //        "Date 08/25/2020"
        //        if timer "Start 10:00PM"
        //                 "Stop 06:00AM"

        switch(_infoLine)
        {
          case LcdInfoLine_Time:
            setInfoLine(LcdInfoLine_Date);
            break;
          case LcdInfoLine_Date:
            setInfoLine(_scheduler->getNextEvent().isValid() ?
                        LcdInfoLine_TimerStart :
                        LcdInfoLine_Time);
            break;
          case LcdInfoLine_TimerStart:
            setInfoLine(_scheduler->getNextEvent().isValid() ?
                        LcdInfoLine_TimerStop :
                        LcdInfoLine_Time);
            break;
          default:
            setInfoLine(LcdInfoLine_Time);
            break;
        }
        break;
      default:
        break;
    }

  }

  if(_updateStateDisplay)
  {
    _updateStateDisplay = false;

    switch(_evseState)
    {
      case OPENEVSE_STATE_NOT_CONNECTED:
        // Line 0 "Ready L2:48A"
        sprintf(temp, "Ready %dA", _evse->getMaxCurrent());
        showText(0, 0, temp, true);
        break;

      case OPENEVSE_STATE_CONNECTED:
        // Line 0 "Connected L2:48A"
        sprintf(temp, "Connected %dA", _evse->getMaxCurrent());
        showText(0, 0, temp, true);
        break;

      case OPENEVSE_STATE_CHARGING:
        // Line 0 "Charging 47.8A"
        sprintf(temp, "Charging %dA", _evse->getChargeCurrent());
        showText(0, 0, temp, true);
        break;

      case OPENEVSE_STATE_VENT_REQUIRED:
        // Line 0 "VEHICLE ERROR "
        // Line 1 "VENT REQUIRED "
        showText(0, 0, "VEHICLE ERROR", true);
        showText(0, 0, "VENT REQUIRED", true);
        break;

      case OPENEVSE_STATE_DIODE_CHECK_FAILED:
        // Line 0 "VEHICLE ERROR "
        // Line 1 "VEHICLE CHECK "
        showText(0, 0, "VEHICLE ERROR", true);
        showText(0, 0, "VEHICLE CHECK", true);
        break;

      case OPENEVSE_STATE_GFI_FAULT:
        // Line 0 "SAFETY ERROR "
        // Line 1 "GROUND FAULT "
        showText(0, 0, "SAFETY ERROR", true);
        showText(0, 0, "GROUND FAULT", true);
        break;

      case OPENEVSE_STATE_NO_EARTH_GROUND:
        // Line 0 "SAFETY ERROR "
        // Line 1 "GROUND MISSING "
        showText(0, 0, "SAFETY ERROR", true);
        showText(0, 0, "GROUND MISSING", true);
        break;

      case OPENEVSE_STATE_STUCK_RELAY:
        // Line 0 "SAFETY ERROR "
        // Line 1 "STUCK RELAY "
        showText(0, 0, "SAFETY ERROR", true);
        showText(0, 0, "STUCK RELAY", true);
        break;

      case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
        // Line 0 "SAFETY ERROR "
        // Line 1 "SELF TEST FAILED"
        showText(0, 0, "SAFETY ERROR", true);
        showText(0, 0, "SELF TEST FAILE", true);
        break;

      case OPENEVSE_STATE_OVER_TEMPERATURE:
        // Line 0 "SAFETY ERROR "
        // Line 1 "OVER TEMPERATURE"
        showText(0, 0, "SAFETY ERROR", true);
        showText(0, 0, "OVER TEMPERATUR", true);
        break;

      case OPENEVSE_STATE_OVER_CURRENT:
        // Line 0 "SAFETY ERROR "
        // Line 1 "OVER CURRENT "
        showText(0, 0, "SAFETY ERROR", true);
        showText(0, 0, "OVER CURRENT", true);
        break;

      case OPENEVSE_STATE_SLEEPING:
      case OPENEVSE_STATE_DISABLED:
        // Line 0 "zzZ Sleeping Zzz"
        showText(0, 0, "zzZ Sleeping Zzz", true);
        break;

      default:
        break;
    }
  }

  if(_updateInfoLine)
  {
    switch(_infoLine)
    {
      case LcdInfoLine_EnergySession:
        // Energy 1,018Wh
        _evse->getOpenEVSE().getEnergy([this](int ret, double session_wh, double total_kwh) {
          char temp[20];
          sprintf(temp, "Energy %.2fWh", session_wh);
          showText(0, 1, temp, true);
        });
        _updateInfoLine = false;
        break;

      case LcdInfoLine_EnergyTotal:
        // Lifetime 2313kWh
        _evse->getOpenEVSE().getEnergy([this](int ret, double session_wh, double total_kwh) {
          char temp[20];
          sprintf(temp, "Lifetime %.0fkWh", total_kwh);
          showText(0, 1, temp, true);
        });
        _updateInfoLine = false;
        break;

      case LcdInfoLine_Tempurature:
        // EVSE Temp 30.5C
        sprintf(temp, "EVSE Temp %.1fC", temp_monitor);
        showText(0, 1, temp, true);
        _updateInfoLine = false;
        break;

      case LcdInfoLine_Time:
      {
        // Time 03:14PM
        timeval local_time;
        gettimeofday(&local_time, NULL);
        struct tm timeinfo;
        localtime_r(&local_time.tv_sec, &timeinfo);
        //strftime(temp, sizeof(temp), "Time %H:%M:%S", &timeinfo);
        strftime(temp, sizeof(temp), "Time %l:%M:%S %p", &timeinfo);
        showText(0, 1, temp, true);
        nextUpdate = 1000 - (local_time.tv_usec / 1000);
      } break;

      case LcdInfoLine_Date:
      {
        // Date 08/25/2020
        timeval local_time;
        gettimeofday(&local_time, NULL);
        struct tm timeinfo;
        localtime_r(&local_time.tv_sec, &timeinfo);
        strftime(temp, sizeof(temp), "Date %d/%m/%Y", &timeinfo);
        showText(0, 1, temp, true);
        _updateInfoLine = false;
       } break;

      case LcdInfoLine_ElapsedTime:
        break;

      case LcdInfoLine_BatterySOC:
        // Charge level 79%
        break;

      case LcdInfoLine_ChargeLimit:
        // Charge limit 85%
        break;

      case LcdInfoLine_Range:
        break;

      case LcdInfoLine_RangeAdded:
        // Added 127 miles
        break;

      case LcdInfoLine_TimeLeft:
        break;

      case LcdInfoLine_Voltage:
        // Voltage 243v AC
        break;

      case LcdInfoLine_TimerStart:
      {
        // Start 10:00PM
        Scheduler::EventInstance &event = _scheduler->getNextEvent(EvseState::Active);
        if(event.isValid())
        {
          int hour = event.getEvent()->getHours();
          int min = event.getEvent()->getMinutes();

          bool pm = hour >= 12;
          hour = hour % 12;
          if(0 == hour) {
            hour = 12;
          }

          sprintf(temp, "Start %d:%02d %s", hour, min, pm ? "AM" : "PM");
          showText(0, 1, temp, true);
        } else {
          showText(0, 1, "Start --:--", true);
        }
      } break;

      case LcdInfoLine_TimerStop:
      {
        // Stop 06:00AM
        Scheduler::EventInstance &event = _scheduler->getNextEvent(EvseState::Disabled);
        if(event.isValid())
        {
          int hour = event.getEvent()->getHours();
          int min = event.getEvent()->getMinutes();

          bool pm = hour >= 12;
          hour = hour % 12;
          if(0 == hour) {
            hour = 12;
          }

          sprintf(temp, "Stop %d:%02d %s", hour, min, pm ? "AM" : "PM");
          showText(0, 1, temp, true);
        } else {
          showText(0, 1, "Stop --:--", true);
        }
        _updateInfoLine = false;
      } break;

      case LcdInfoLine_TimerRemaining:
      {
        // Remaining 6:23
        Scheduler::EventInstance &event = _scheduler->getNextEvent();
        if(event.isValid())
        {
          int currentDay;
          int32_t currentOffset;
          Scheduler::getCurrentTime(currentDay, currentOffset);
          uint32_t delay = event.getDelay(currentDay, currentOffset);
          int hour = delay / 3600;
          int min = (delay / 60) % 60;
          int sec = delay % 60;
          sprintf(temp, "Left %d:%02d:%02d", hour, min, sec);
          showText(0, 1, temp, true);
          nextUpdate = 1000;
        } else {
          showText(0, 1, "Left --:--:--", true);
          _updateInfoLine = false;
        }
      } break;
        break;


      default:
        break;
    }
  }

  unsigned long nextInfoDelay = _infoLineChageTime - millis();
  if(nextInfoDelay < nextUpdate) {
    nextUpdate = nextInfoDelay;
  }

  return nextUpdate;
}

void LcdTask::showText(int x, int y, const char *msg, bool clear)
{
  DBUGF("LCD: %d %d %s", x, y, msg);
  _evse->getOpenEVSE().lcdDisplayText(x, y, msg, IGNORE);
  if(clear)
  {
    for(int i = x + strlen(msg); i < LCD_MAX_LEN; i += 6)
    {
      // Older versions of the firmware crash if sending more than 6 spaces so clear the rest
      // of the line using blocks of 6 spaces
      _evse->getOpenEVSE().lcdDisplayText(i, y, "      ", IGNORE);
    }
  }
}

LcdTask lcd;
