#ifndef __EVENT_LOG_H
#define __EVENT_LOG_H

#include <Arduino.h>
#include "evse_state.h"

#ifndef EVENTLOG_ROTATE_SIZE
#define EVENTLOG_ROTATE_SIZE        512
#endif

#ifndef EVENTLOG_MAX_ROTATE_COUNT
#define EVENTLOG_MAX_ROTATE_COUNT   10
#endif

#ifndef EVENTLOG_BASE_DIRECTORY
#define EVENTLOG_BASE_DIRECTORY     "/eventlog"
#endif

class EventType
{
  public:
    enum Value : uint8_t
    {
      Information,
      Notification,
      Warning
    };

    EventType() = default;
    constexpr EventType(Value value) : _value(value) { }

    const char *toString()
    {
      return EventType::Information == _value ? "information" :
             EventType::Notification == _value ? "notification" :
             EventType::Warning == _value ? "warning" :
             "unknown";
    }

    uint8_t toInt() {
      return _value;
    }
    bool fromInt(uint8_t value)
    {
      if (value <= Value::Warning) {
        _value = (EventType::Value)value;
        return true;
      }

      return false;
    }

    operator Value() const { return _value; }
    explicit operator bool() = delete;        // Prevent usage: if(state)
    EventType operator= (const Value val) {
      _value = val;
      return *this;
    }

  private:
    Value _value;
};

class EventLog
{
private:
  uint32_t _min_log_index;
  uint32_t _max_log_index;

  String filenameFromIndex(uint32_t index);
  uint32_t indexFromFilename(String &filename);

public:
  EventLog();
  ~EventLog();

  void begin();

  uint32_t getMinIndex() {
    return _min_log_index;
  }

  uint32_t getMaxIndex() {
    return _max_log_index;
  }

  void log(EventType type, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double tempurature, double tempuratureMax, uint8_t divertMode);
  void enumerate(uint32_t index, std::function<void(String time, EventType type, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double tempurature, double tempuratureMax, uint8_t divertMode)> callback);
};


#endif // !__EVENT_LOG_H
