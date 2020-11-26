#ifndef _OPENEVSE_SCHEDULER_H
#define _OPENEVSE_SCHEDULER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include "evse_man.h"

#ifndef SCHEDULER_MAX_EVENTS
#define SCHEDULER_MAX_EVENTS 50
#endif // !SCHEDULER_MAX_EVENTS

#define SCHEDULER_DAY_MONDAY      (1 << 0)
#define SCHEDULER_DAY_TUESDAY     (1 << 1)
#define SCHEDULER_DAY_WEDNESDAY   (1 << 2)
#define SCHEDULER_DAY_THURSDAY    (1 << 3)
#define SCHEDULER_DAY_FRIDAY      (1 << 4)
#define SCHEDULER_DAY_SATURDAY    (1 << 5)
#define SCHEDULER_DAY_SUNDAY      (1 << 6)
#define SCHEDULER_REPEAT          (1 << 7)

#define SCHEDULER_EVENT_NULL      ((uint16_t)0)

class Scheduler : public MicroTasks::Task
{
  public:
    class Event
    {
      private:
        static uint16_t _next_id;
        uint16_t _id;
        uint16_t _seconds;
        uint8_t _days;
        uint8_t _state;
        time_t _next;
      public:
        Event();
        Event(uint16_t id, uint16_t second, uint8_t days, EvseState state);
        Event(uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state);
        Event(uint16_t id, uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state);

        uint16_t getId() {
          return _id;
        };
        void setId(uint16_t id);

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
        uint16_t getSeconds() {
          return _seconds % 60;
        };
        void setSeconds(uint16_t seconds) {
          _seconds = (getHours() * 3600) + (getMinutes() * 60) + seconds;
        }
        uint16_t getMinutes() {
          return (_seconds / 60) % 60;
        };
        void setMinutes(uint16_t minutes) {
          _seconds = (getHours() * 3600) + (minutes * 60) + getSeconds();
        }
        uint16_t getHours() {
          return _seconds / 3600;
        };
        void setHours(uint16_t hours) {
          _seconds = (hours * 3600) + (getMinutes() * 60) + getSeconds();
        }

        String getTime();
        bool setTime(String &time) {
          return setTime(time.c_str());
        }
        bool setTime(const char *time);
        void setTime(uint16_t hours, uint16_t seconds, uint16_t minutes);

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
    };

  private:
    EvseManager *_evse;
    Event _events[SCHEDULER_MAX_EVENTS];

    bool getNextEvent(Event **event);
    bool findEvent(uint16_t id, Event **event);
    bool serialize(JsonObject &obj, Event *event);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);
  public:
    Scheduler(EvseManager &evse);

    bool begin();

    bool addEvent(uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state, uint16_t &id);
    bool addEvent(String& json);
    bool addEvent(const char *json);
    bool addEvent(DynamicJsonDocument &doc);

    bool removeEvent(uint16_t id);

    bool deserialize(String& json);
    bool deserialize(const char *json);
    bool deserialize(DynamicJsonDocument &doc);

    bool deserialize(String& json, uint16_t event);
    bool deserialize(const char *json, uint16_t event);
    bool deserialize(DynamicJsonDocument &doc, uint16_t event);
    bool deserialize(JsonObject &obj, uint16_t event);

    bool serialize(String& json);
    bool serialize(DynamicJsonDocument &doc);

    bool serialize(String& json, uint16_t event);
    bool serialize(DynamicJsonDocument &doc, uint16_t event);
    bool serialize(JsonObject &obj, uint16_t event);
};

extern Scheduler scheduler;

#endif // !_OPENEVSE_SCHEDULER_H
