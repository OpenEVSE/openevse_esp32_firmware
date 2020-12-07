#ifndef _OPENEVSE_SCHEDULER_H
#define _OPENEVSE_SCHEDULER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include "evse_man.h"

#ifndef SCHEDULER_MAX_EVENTS
#define SCHEDULER_MAX_EVENTS 50
#endif // !SCHEDULER_MAX_EVENTS

#define SCHEDULER_DAYS_IN_A_WEEK 7 // 7 days a week

#define SCHEDULER_DAY_SUNDAY      (1 << 0)
#define SCHEDULER_DAY_MONDAY      (1 << 1)
#define SCHEDULER_DAY_TUESDAY     (1 << 2)
#define SCHEDULER_DAY_WEDNESDAY   (1 << 3)
#define SCHEDULER_DAY_THURSDAY    (1 << 4)
#define SCHEDULER_DAY_FRIDAY      (1 << 5)
#define SCHEDULER_DAY_SATURDAY    (1 << 6)
#define SCHEDULER_REPEAT          (1 << 7)

#define SCHEDULER_EVENT_NULL      ((uint32_t)0)

class Scheduler : public MicroTasks::Task
{
  public:
    class Event;
    class EventInstance
    {
      private:
        Event *_event;
        int _day;
      public:
        EventInstance() :
          _event(NULL), _day(0) { }
        EventInstance(Event &event, int day) :
          _event(&event), _day(day) { }
        EventInstance(Event *event, int day) :
          _event(event), _day(day) { }

        EventInstance &operator=(const EventInstance &rhs) {
          _event = rhs._event;
          _day = rhs._day;
          return *this;
        };

        void moveToNext() {
          if(isValid()) {
            *this = getNext();
          }
        }

        EventInstance& operator++() {
          moveToNext();
          return *this;
        }

        bool operator==(const EventInstance &rhs) const {
          return _event == rhs._event && _day == rhs._day;
        };

        bool operator!=(const EventInstance &rhs) const {
          return !(*this == rhs);
        };

        bool operator==(const EventInstance *rhs) const {
          return _event == rhs->_event && _day == rhs->_day;
        };

        bool operator!=(const EventInstance *rhs) const {
          return !(*this == rhs);
        };

        Event *getEvent() {
          return _event;
        }

        int getDay() {
          return _day;
        }

        uint32_t getStartOffset() {
          return _event->getOffset();
        }

        int32_t getStartOffset(int fromDay, int dayOffset = 0);

        uint32_t getEndOffset() {
          return getStartOffset() + getDuration();
        }

        int32_t getEndOffset(int fromDay, int dayOffset = 0) {
          return getStartOffset(fromDay, dayOffset) + getDuration();
        }

        uint32_t getDuration();
        uint32_t getDelay(int fromDay, uint32_t fromOffset);

        EvseState getState() {
          return isValid() ? _event->getState() : EvseState_NULL;
        }

        bool isValid() {
          return NULL != _event;
        };

        void invalidate() {
          _event = NULL;
        }

        void setEvent(Event *event, int day) {
          _event = event;
          _day = day;
        };

        void setNext(Event *event, int day) {
          EventInstance e(event, day);
          setNext(e);
        };

        void setNext(EventInstance &e) {
          _event->setNext(_day, e);
        };

        EventInstance &getNext() {
          return _event->getNext(_day);
        }
    };

    class Event
    {
      private:
        static uint32_t _next_id;
        uint32_t _id;
        uint32_t _seconds;
        uint8_t _days;
        uint8_t _state;
        time_t _next;

        EventInstance _nextEvents[SCHEDULER_DAYS_IN_A_WEEK];
      public:
        Event();
        Event(uint32_t id, uint32_t second, uint8_t days, EvseState state);
        Event(uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state);
        Event(uint32_t id, uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state);

        uint32_t getId() {
          return _id;
        };
        void setId(uint32_t id);

        uint8_t getDays() {
          return _days;
        };
        void setDays(uint8_t days) {
          _days = days;
        }
        EvseState getState() {
          return (EvseState)_state;
        };
        void setState(EvseState state) {
          _state = state;
        }
        uint32_t getSeconds() {
          return _seconds % 60;
        };
        void setSeconds(uint32_t seconds) {
          _seconds = (getHours() * 3600) + (getMinutes() * 60) + seconds;
        }
        uint32_t getMinutes() {
          return (_seconds / 60) % 60;
        };
        void setMinutes(uint32_t minutes) {
          _seconds = (getHours() * 3600) + (minutes * 60) + getSeconds();
        }
        uint32_t getHours() {
          return _seconds / 3600;
        };
        void setHours(uint32_t hours) {
          _seconds = (hours * 3600) + (getMinutes() * 60) + getSeconds();
        }
        uint32_t getOffset() {
          return _seconds;
        }

        String getTime();
        bool setTime(String &time) {
          return setTime(time.c_str());
        }
        bool setTime(const char *time);
        void setTime(uint32_t hours, uint32_t seconds, uint32_t minutes);

        const char *getStateText();
        bool setState(String &state) {
          return setState(state.c_str());
        }
        bool setState(const char *state);

        bool isValid() {
          return SCHEDULER_EVENT_NULL != _id;
        }

        void invalidate() {
          _id = SCHEDULER_EVENT_NULL;
        }

        // List management
        void setNext(int day, EventInstance &event) {
          _nextEvents[day] = event;
        }

        EventInstance &getNext(int day) {
          return _nextEvents[day];
        }
    };

  private:
    EvseManager *_evse;
    Event _events[SCHEDULER_MAX_EVENTS];

    EventInstance _firstEvent;
    EventInstance _activeEvent;

    void buildSchedule();
    EventInstance &getCurrentEvent();
    bool findEvent(uint32_t id, Event **event);
    bool serialize(JsonObject &obj, Event *event);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);
  public:
    Scheduler(EvseManager &evse);

    bool begin();

    bool addEvent(uint32_t id, const char *time, uint8_t days, const char *state);
    bool addEvent(String& json);
    bool addEvent(const char *json);
    bool addEvent(DynamicJsonDocument &doc);

    bool removeEvent(uint32_t id);

    bool deserialize(String& json);
    bool deserialize(const char *json);
    bool deserialize(DynamicJsonDocument &doc);

    bool deserialize(String& json, uint32_t event);
    bool deserialize(const char *json, uint32_t event);
    bool deserialize(DynamicJsonDocument &doc, uint32_t event);
    bool deserialize(JsonObject &obj, uint32_t event);

    bool serialize(String& json);
    bool serialize(DynamicJsonDocument &doc);

    bool serialize(String& json, uint32_t event);
    bool serialize(DynamicJsonDocument &doc, uint32_t event);
    bool serialize(JsonObject &obj, uint32_t event);

    static void getCurrentTime(int &day, int32_t &offset);
};

extern Scheduler scheduler;

#endif // !_OPENEVSE_SCHEDULER_H
