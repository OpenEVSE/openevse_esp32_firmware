#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifndef ENABLE_SCREEN_LCD_TFT

#include "emonesp.h"
#include "lcd.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"
#include "app_config.h"
#include <sys/time.h>

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
  _infoLine(LcdInfoLine::Off),
  _evseState(OPENEVSE_STATE_STARTING),
  _evse(NULL),
  _scheduler(NULL),
  _nextMessageTime(0),
  _evseStateEvent(this),
  _evseSettingsEvent(this)
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
    displayNextMessage();
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

void LcdTask::setEvseState(uint8_t lcdColour)
{
  _evse->getOpenEVSE().lcdSetColour(lcdColour, IGNORE);
  setInfoLine(getNextInfoLine(LcdInfoLine::Off));
}

void LcdTask::setInfoLine(LcdInfoLine info)
{
  if(_infoLine != info)
  {
    _infoLine = info;
    _updateInfoLine = true;

    if(LcdInfoLine::Off != info &&
       LcdInfoLine::ManualOverride != info)
    {
      _infoLineChageTime = millis() + LCD_DISPLAY_CHANGE_TIME;
    }
  }
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
  _evse->onStateChange(&_evseStateEvent);
  _evse->onSettingsChanged(&_evseSettingsEvent);
}

unsigned long LcdTask::loop(MicroTasks::WakeReason reason)
{
  DBUG("LCD UI woke: ");
  DBUG(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");
  DBUG(" ");
  DBUGLN(LcdInfoLine::Off == _infoLine ? "LcdInfoLine::Off" :
         LcdInfoLine::EnergySession == _infoLine ? "LcdInfoLine::EnergySession" :
         LcdInfoLine::EnergyTotal == _infoLine ? "LcdInfoLine::EnergyTotal" :
         LcdInfoLine::Temperature == _infoLine ? "LcdInfoLine::Temperature" :
         LcdInfoLine::Time == _infoLine ? "LcdInfoLine::Time" :
         LcdInfoLine::Date == _infoLine ? "LcdInfoLine::Date" :
         LcdInfoLine::ElapsedTime == _infoLine ? "LcdInfoLine::ElapsedTime" :
         LcdInfoLine::BatterySOC == _infoLine ? "LcdInfoLine::BatterySOC" :
         LcdInfoLine::ChargeLimit == _infoLine ? "LcdInfoLine::ChargeLimit" :
         LcdInfoLine::Range == _infoLine ? "LcdInfoLine::Range" :
         LcdInfoLine::RangeAdded == _infoLine ? "LcdInfoLine::RangeAdded" :
         LcdInfoLine::TimeLeft == _infoLine ? "LcdInfoLine::TimeLeft" :
         LcdInfoLine::Voltage == _infoLine ? "LcdInfoLine::Voltage" :
         LcdInfoLine::TimerStart == _infoLine ? "LcdInfoLine::TimerStart" :
         LcdInfoLine::TimerStop == _infoLine ? "LcdInfoLine::TimerStop" :
         LcdInfoLine::TimerRemaining == _infoLine ? "LcdInfoLine::TimerRemaining" :
         LcdInfoLine::ManualOverride == _infoLine ? "LcdInfoLine::ManualOverride" :
         "UNKNOWN");

  bool evseStateChanged = false;
  uint8_t newEvseState = _evse->getEvseState();
  if(newEvseState != _evseState)
  {
    if(OPENEVSE_STATE_STARTING == _evseState)
    {
      // We have just started, disable the LCD and button, we are going to handle them
      if(!_evse->isButtonDisabled()) {
        _evse->getOpenEVSE().feature(OPENEVSE_FEATURE_BUTTON, false, IGNORE);
      }
      _evse->getOpenEVSE().lcdEnable(false, IGNORE);
      _evse->getOpenEVSE().onButton([this](uint8_t long_press) { onButton(long_press); });
    }

    _evseState = newEvseState;
    DBUGVAR(_evseState);

    evseStateChanged = true;
  }

  //bool pilotStateChanged = false;
  uint8_t newPilotState = _evse->getPilotState();
  if(newPilotState != _pilotState)
  {
    _pilotState = newPilotState;
    DBUGVAR(_pilotState);

    //pilotStateChanged = true;
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

  if(evseStateChanged || flagsChanged)
  {
    // Set the LCD background colour based on the EVSE state and deal with any
    // resulting state changes
    _evse->getOpenEVSE().lcdSetColour(_evse->getStateColour(), IGNORE);
    if(evseStateChanged) {
      setInfoLine(getNextInfoLine(LcdInfoLine::Off));
      _updateStateDisplay = true;
    }
  }

  // If we have messages to display, do it
  if(_head) {
    return displayNextMessage();
  }

  if(_evseSettingsEvent.IsTriggered()) {
    _updateStateDisplay = true;
    _updateInfoLine = true;
  }

  // Else display the status screen
  unsigned long nextUpdate = MicroTask.Infinate;

  if(millis() >= _infoLineChageTime) {
    setInfoLine(getNextInfoLine(_infoLine));
  }

  if(_updateStateDisplay) {
    displayStateLine(_evseState, nextUpdate);
  }

  if(_updateInfoLine) {
    displayInfoLine(_infoLine, nextUpdate);
  }

  unsigned long nextInfoDelay = _infoLineChageTime - millis();
  if(nextInfoDelay < nextUpdate) {
    nextUpdate = nextInfoDelay;
  }

  DBUGVAR(nextUpdate);
  return nextUpdate;
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
    showText(msg->getX(), msg->getY(), msg->getMsg(), msg->getClear());

    _nextMessageTime = millis() + msg->getTime();

    // delete the message
    delete msg;

    _updateStateDisplay = true;
    _updateInfoLine = true;
  }

  unsigned long nextUpdate = _nextMessageTime - millis();
  DBUGVAR(nextUpdate);
  return nextUpdate;
}


LcdTask::LcdInfoLine LcdTask::getNextInfoLine(LcdInfoLine info)
{
  if(_manual->isActive()) {
    return LcdInfoLine::ManualOverride;
  }

  switch(_evseState)
  {
    case OPENEVSE_STATE_NOT_CONNECTED:
    case OPENEVSE_STATE_CONNECTED:
      // Line 1 "Energy 1,018Wh"
      //        "Lifetime 2313kWh"
      //        "EVSE Temp 30.5C"
      //        "Time 03:14PM"
      //        "Date 08/25/2020"

      switch(info)
      {
        case LcdInfoLine::EnergySession:
          return LcdInfoLine::EnergyTotal;
        case LcdInfoLine::EnergyTotal:
          return LcdInfoLine::Temperature;
        case LcdInfoLine::Temperature:
          return LcdInfoLine::Time;
        case LcdInfoLine::Time:
          return LcdInfoLine::Date;
        case LcdInfoLine::Date:
          if(_scheduler->getNextEvent(EvseState::Active).isValid()) {
            return LcdInfoLine::TimerStart;
          }
        case LcdInfoLine::TimerStart:
          if(_scheduler->getNextEvent(EvseState::Disabled).isValid()) {
            return LcdInfoLine::TimerStop;
          }
        default:
          return LcdInfoLine::EnergySession;
      }

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

      switch(info)
      {
        case LcdInfoLine::EnergySession:
          return LcdInfoLine::EnergyTotal;
        case LcdInfoLine::EnergyTotal:
          return LcdInfoLine::Temperature;
        case LcdInfoLine::Temperature:
          if(_scheduler->getNextEvent(EvseState::Disabled).isValid()) {
            return LcdInfoLine::TimerStop;
          }
        case LcdInfoLine::TimerStop:
          if(_scheduler->getNextEvent().isValid()) {
            return LcdInfoLine::TimerRemaining;
          }
        case LcdInfoLine::TimerRemaining:
          if(_evse->isVehicleStateOfChargeValid()) {
            return LcdInfoLine::BatterySOC;
          }
        case LcdInfoLine::BatterySOC:
          if(_evse->isVehicleRangeValid()) {
            return LcdInfoLine::Range;
          }
        case LcdInfoLine::Range:
          if(_evse->isVehicleEtaValid()) {
            return LcdInfoLine::TimeLeft;
          }
        default:
          return LcdInfoLine::EnergySession;
      }

    case OPENEVSE_STATE_SLEEPING:
    case OPENEVSE_STATE_DISABLED:
      // Line 1 "Time 03:14PM"
      //        "Date 08/25/2020"
      //        if timer "Start 10:00PM"
      //                 "Stop 06:00AM"

      switch(info)
      {
        case LcdInfoLine::Time:
          return LcdInfoLine::Date;
        case LcdInfoLine::Date:
          if(_scheduler->getNextEvent(EvseState::Active).isValid()) {
            return LcdInfoLine::TimerStart;
          }
        case LcdInfoLine::TimerStart:
          if(_scheduler->getNextEvent(EvseState::Disabled).isValid()) {
            return LcdInfoLine::TimerStop;
          }
        default:
          return LcdInfoLine::Time;
      }
  }

  return LcdInfoLine::Off;
}

void LcdTask::displayStateLine(uint8_t evseState, unsigned long &nextUpdate)
{
  switch(evseState)
  {
    case OPENEVSE_STATE_NOT_CONNECTED:
      // Line 0 "Ready L2:48A"
      displayNumberValue(0, "Ready", _evse->getChargeCurrent(), 0, "A");
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_CONNECTED:
      // Line 0 "Connected L2:48A"
      displayNumberValue(0, "Connected", _evse->getChargeCurrent(), 0, "A");
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_CHARGING:
      // Line 0 "Charging 47.8A"
      displayNumberValue(0, "Charging", _evse->getAmps(), 2, "A");
      nextUpdate = 1000;
      break;

    case OPENEVSE_STATE_VENT_REQUIRED:
      // Line 0 "VEHICLE ERROR "
      // Line 1 "VENT REQUIRED "
      showText(0, 0, "VEHICLE ERROR", true);
      showText(0, 1, "VENT REQUIRED", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
      // Line 0 "VEHICLE ERROR "
      // Line 1 "VEHICLE CHECK "
      showText(0, 0, "VEHICLE ERROR", true);
      showText(0, 1, "VEHICLE CHECK", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_GFI_FAULT:
      // Line 0 "SAFETY ERROR "
      // Line 1 "GROUND FAULT "
      showText(0, 0, "SAFETY ERROR", true);
      showText(0, 1, "GROUND FAULT", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_NO_EARTH_GROUND:
      // Line 0 "SAFETY ERROR "
      // Line 1 "GROUND MISSING "
      showText(0, 0, "SAFETY ERROR", true);
      showText(0, 1, "GROUND MISSING", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_STUCK_RELAY:
      // Line 0 "SAFETY ERROR "
      // Line 1 "STUCK RELAY "
      showText(0, 0, "SAFETY ERROR", true);
      showText(0, 1, "STUCK RELAY", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
      // Line 0 "SAFETY ERROR "
      // Line 1 "SELF TEST FAILED"
      showText(0, 0, "SAFETY ERROR", true);
      showText(0, 1, "SELF TEST FAILE", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_OVER_TEMPERATURE:
      // Line 0 "SAFETY ERROR "
      // Line 1 "OVER TEMPERATURE"
      showText(0, 0, "SAFETY ERROR", true);
      showText(0, 1, "OVER TEMPERATUR", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_OVER_CURRENT:
      // Line 0 "SAFETY ERROR "
      // Line 1 "OVER CURRENT "
      showText(0, 0, "SAFETY ERROR", true);
      showText(0, 1, "OVER CURRENT", true);
      _updateStateDisplay = false;
      break;

    case OPENEVSE_STATE_SLEEPING:
    case OPENEVSE_STATE_DISABLED:
      // Line 0 "zzZ Sleeping Zzz"
      showText(0, 0, "zzZ Sleeping Zzz", true);
      _updateStateDisplay = false;
      break;

    default:
      break;
  }
}

void LcdTask::displayInfoLine(LcdInfoLine line, unsigned long &nextUpdate)
{
  char temp[20];

  switch(line)
  {
    case LcdInfoLine::EnergySession:
      // Energy 1,018Wh
      displayScaledNumberValue(1, "Energy", _evse->getSessionEnergy(), 1, "Wh");
      _updateInfoLine = false;
      break;

    case LcdInfoLine::EnergyTotal: {
      // Lifetime 2313kWh
      double totalEnergy = _evse->getTotalEnergy() * 1000;
      displayScaledNumberValue(1, "Lifetime", totalEnergy, 0, "Wh");
      _updateInfoLine = false;
    } break;

    case LcdInfoLine::Temperature:
      // EVSE Temp 30.5C
      displayNumberValue(1, "EVSE Temp", _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR), 1, "C");
      _updateInfoLine = false;
      break;

    case LcdInfoLine::Time:
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

    case LcdInfoLine::Date:
    {
      // Date 08/25/2020
      timeval local_time;
      gettimeofday(&local_time, NULL);
      struct tm timeinfo;
      localtime_r(&local_time.tv_sec, &timeinfo);
      strftime(temp, sizeof(temp), "%Y-%m-%d", &timeinfo);
      displayNameValue(1, "Date", temp);
      _updateInfoLine = false;
      } break;

    case LcdInfoLine::ElapsedTime:
      break;

    case LcdInfoLine::BatterySOC:
      // Charge level 79%
      displayNumberValue(1, "Charge level", _evse->getVehicleStateOfCharge(), 0, "%");
      _updateInfoLine = false;
      break;

    case LcdInfoLine::ChargeLimit:
      // Charge limit 85%
      break;

    case LcdInfoLine::Range:
      displayNumberValue(1, "Range", _evse->getVehicleRange(), 0, config_vehicle_range_miles() ? " miles" : " km");
      break;

    case LcdInfoLine::RangeAdded:
      // Added 127 miles
      break;

    case LcdInfoLine::TimeLeft:
      displayStopWatchTime("ETA", _evse->getVehicleEta());
      break;

    case LcdInfoLine::Voltage:
      // Voltage 243v AC
      break;

    case LcdInfoLine::TimerStart:
      // Start 10:00PM
      displayInfoEventTime("Start", _scheduler->getNextEvent(EvseState::Active));
      _updateInfoLine = false;
      break;

    case LcdInfoLine::TimerStop:
      // Stop 06:00AM
      displayInfoEventTime("Stop", _scheduler->getNextEvent(EvseState::Disabled));
      _updateInfoLine = false;
      break;

    case LcdInfoLine::TimerRemaining:
    {
      // Remaining 6:23
      Scheduler::EventInstance &event = _scheduler->getNextEvent();
      if(event.isValid())
      {
        int currentDay;
        int32_t currentOffset;
        Scheduler::getCurrentTime(currentDay, currentOffset);
        uint32_t delay = event.getDelay(currentDay, currentOffset);
        displayStopWatchTime("Left", delay);
        nextUpdate = 1000;
      } else {
        displayNameValue(1, "Left", "--:--:--");
        _updateInfoLine = false;
      }
    } break;

    case LcdInfoLine::ManualOverride:
      showText(0, 1, "Manual Override", true);
      break;

    default:
      break;
  }
}

void LcdTask::displayScaledNumberValue(int line, const char *name, double value, int precision, const char *unit)
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

  char newUnit[20];
  sprintf(newUnit, "%s%s", mod[index], unit);

  displayNumberValue(line, name, value, precision, newUnit);
}

void LcdTask::displayNumberValue(int line, const char *name, double value, int precision, const char *unit)
{
  char number[20];
  snprintf(number, sizeof(number), "%.*f%s", precision, value, unit);

  displayNameValue(line, name, number);
}

void LcdTask::displayInfoEventTime(const char *name, Scheduler::EventInstance &event)
{
  char temp[20];
  if(event.isValid())
  {
    int hour = event.getEvent()->getHours();
    int min = event.getEvent()->getMinutes();

    bool pm = hour >= 12;
    hour = hour % 12;
    if(0 == hour) {
      hour = 12;
    }

    sprintf(temp, "%d:%02d %s", hour, min, pm ? "PM" : "AM");
  } else {
    sprintf(temp, "--:--");
  }
  displayNameValue(1, name, temp);
}

void LcdTask::displayStopWatchTime(const char *name, uint32_t time)
{
  char temp[20];
  int hour = time / 3600;
  int min = (time / 60) % 60;
  int sec = time % 60;
  sprintf(temp, "%d:%02d:%02d", hour, min, sec);
  displayNameValue(1, name, temp);
}

void LcdTask::displayNameValue(int line, const char *name, const char *value)
{
  int nameLen = strlen(name);
  int valueLen = strlen(value) + 1;
  if(nameLen + valueLen > LCD_MAX_LEN) {
    nameLen = LCD_MAX_LEN - valueLen;
  } else {
    valueLen = LCD_MAX_LEN - nameLen;
  }

  DBUGVAR(nameLen);
  DBUGVAR(name);
  DBUGVAR(valueLen);
  DBUGVAR(value);

  char temp[20];
  snprintf(temp, sizeof(temp), "%.*s%*s", nameLen, name, valueLen, value);
  showText(0, line, temp, true);
}

void LcdTask::showText(int x, int y, const char *msg, bool clear)
{
  DBUGF("LCD: %d %d %s, clear=%s", x, y, msg, clear ? "true" : "false");
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

  // HACK: wait for the display to finish, fixes issues with updating the display while updating the firmware
  _evse->getSender().flush();
}

void LcdTask::onButton(int long_press)
{
  DBUGVAR(long_press);
  if(long_press)
  {
    // Boost timer?
  }
  else
  {
    _manual->toggle();
  }
}

void LcdTask::setWifiMode(bool client, bool connected)
{
//only used by the TFT display
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_TFT
