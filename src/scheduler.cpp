
#include "debug.h"
#include "scheduler.h"

uint16_t Scheduler::Event::_next_id = 1;

Scheduler::Event::Event() :
  Event(SCHEDULER_EVENT_NULL, 0, 0, EvseState_NULL)
{

}

Scheduler::Event::Event(uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state) :
  Event(_next_id++,
        (hour * 3600) + (minute * 60) + second,
        days | (repeat ? SCHEDULER_REPEAT : 0),
        state)
{

}

Scheduler::Event::Event(uint16_t id, uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state) :
  Event(id,
        (hour * 3600) + (minute * 60) + second,
        days | (repeat ? SCHEDULER_REPEAT : 0),
        state)
{
}

Scheduler::Event::Event(uint16_t id, uint16_t second, uint8_t days, EvseState state)
{
  if(id >= _next_id) {
    _next_id = id + 1;
  }
}

void Scheduler::Event::setId(uint16_t id) {
  _id = SCHEDULER_EVENT_NULL == id ? _next_id++ : id;
}

String Scheduler::Event::getTime()
{
  char time[9];
  snprintf(time, sizeof(time), "%d:%02d:%02d", getHours(), getMinutes(), getSeconds());
  String timeString(time);
  return timeString;
}

bool Scheduler::Event::setTime(const char *time)
{
  uint16_t hours, minutes, seconds;

  DBUGVAR(time);
  if(3 == sscanf(time, "%hu:%02hu:%02hu", &hours, &minutes, &seconds))
  {
    DBUGVAR(hours);
    DBUGVAR(minutes);
    DBUGVAR(seconds);
    setHours(hours);
    setMinutes(minutes);
    setSeconds(seconds);
    return true;
  }

  return false;
}

void Scheduler::Event::setTime(uint16_t hours, uint16_t seconds, uint16_t minutes)
{
  setHours(hours);
  setMinutes(minutes);
  setSeconds(seconds);
}

const char *Scheduler::Event::getStateText()
{
  return EvseState_Active == _state ? "active" :
         EvseState_Disabled == _state ? "disabled" :
         "unknown";
}

bool Scheduler::Event::setState(const char *state)
{
  // Cheat a bit and just check the first char
  if('a' == state[0] || 'd' == state[0]) {
    _state = 'a' == state[0] ? EvseState_Active : EvseState_Disabled;
    return true;
  }
  return false;
}

Scheduler::Scheduler(EvseManager &evse) :
  _evse(&evse)
{

}

void Scheduler::setup()
{

}

unsigned long Scheduler::loop(MicroTasks::WakeReason reason)
{
  return MicroTask.Infinate;
}

bool Scheduler::begin()
{
  MicroTask.startTask(this);

  return true;
}

bool Scheduler::getNextEvent(Scheduler::Event **event) {
  return false;
}

bool Scheduler::findEvent(uint16_t id, Scheduler::Event **event) {
  for(int i = 0; i < SCHEDULER_MAX_EVENTS; i++)
  {
    if(id == _events[i].getId())
    {
      *event = &_events[i];
      return true;
    }
  }

  return false;
}

bool Scheduler::addEvent(uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state, uint16_t &id)
{
  return false;
}

bool Scheduler::addEvent(String& json)
{
  return false;
}

bool Scheduler::addEvent(const char *json)
{
  return deserialize(json, SCHEDULER_EVENT_NULL);
}

bool Scheduler::addEvent(DynamicJsonDocument &doc)
{
  return deserialize(doc, SCHEDULER_EVENT_NULL);
}

bool Scheduler::removeEvent(uint16_t id)
{
  return false;
}

bool Scheduler::deserialize(String& json)
{
  return Scheduler::deserialize(json.c_str());
}

bool Scheduler::deserialize(const char *json)
{
  // IMPROVE: work out a better value
  const size_t capacity = 1024;
  DynamicJsonDocument doc(capacity);

  DeserializationError err = deserializeJson(doc, json);
  if(DeserializationError::Code::Ok == err) {
    return Scheduler::deserialize(doc);
  }

  return false;
}

bool Scheduler::deserialize(DynamicJsonDocument &doc)
{
  if (doc.is<JsonObject>()) {
    return Scheduler::deserialize(doc, SCHEDULER_EVENT_NULL);
  }

  if(doc.is<JsonArray>())
  {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant value : arr)
    {
      if (value.is<JsonObject>())
      {
        JsonObject obj = value.as<JsonObject>();
        if(false == Scheduler::deserialize(obj, SCHEDULER_EVENT_NULL)) {
          return false;
        }
      }
    }
    return true;
  }

  return false;
}

bool Scheduler::deserialize(String& json, uint16_t event)
{
  return Scheduler::deserialize(json.c_str(), event);
}

bool Scheduler::deserialize(const char *json, uint16_t event)
{
  // IMPROVE: work out a better value
  const size_t capacity = 1024;
  DynamicJsonDocument doc(capacity);

  DBUGVAR(json);

  DeserializationError err = deserializeJson(doc, json);
  if(DeserializationError::Code::Ok == err) {
    return Scheduler::deserialize(doc, event);
  }

  return false;
}

bool Scheduler::deserialize(DynamicJsonDocument &doc, uint16_t event)
{
  JsonObject object = doc.as<JsonObject>();

  return Scheduler::deserialize(object, event);
}

bool Scheduler::deserialize(JsonObject &obj, uint16_t event_id)
{
  if(SCHEDULER_EVENT_NULL == event_id)
  {
    // Try and get the key from the JSON
    if(obj.containsKey("id")) {
      event_id = obj["id"];
    }
  }
  else
  {
    if(obj.containsKey("id")) {
      if(event_id != obj["id"]) {
        return false;
      }
    }
  }

  if(obj.containsKey("state") &&
     obj.containsKey("time") &&
     obj.containsKey("days"))
  {
    Event *event;
    if(findEvent(event_id, &event))
    {
      event->setId(event_id);
      event->setTime(obj["time"].as<const char *>());
      DBUGVAR(event->getHours());
      DBUGVAR(event->getMinutes());
      DBUGVAR(event->getSeconds());
      event->setState(obj["state"].as<const char *>());

      uint8_t days = SCHEDULER_REPEAT;

      for (JsonVariant value : obj["days"].as<JsonArray>())
      {
        const char *day = value.as<const char *>();
        DBUGVAR(day);
        // Cheat and just check the min chars we need
        switch(day[0])
        {
          case 'm':
            days |= SCHEDULER_DAY_MONDAY;
            break;
          case 't':
            days |= 'u' == day[1] ? SCHEDULER_DAY_TUESDAY : SCHEDULER_DAY_THURSDAY;
            break;
          case 'w':
            days |= SCHEDULER_DAY_WEDNESDAY;
            break;
          case 'f':
            days |= SCHEDULER_DAY_FRIDAY;
            break;
          case 's':
            days |= 'a' == day[1] ? SCHEDULER_DAY_SATURDAY : SCHEDULER_DAY_SUNDAY;
            break;
        }
      }

      DBUGVAR(days);
      event->setDays(days);

      return true;
    }
  }

  return false;
}

bool Scheduler::serialize(String& json)
{
  // IMPROVE: do a better calculation of required space
  const size_t capacity = 4096;
  DynamicJsonDocument doc(capacity);

  if(Scheduler::serialize(doc))
  {
    serializeJson(doc, json);
    return true;
  }

  return false;
}

bool Scheduler::serialize(DynamicJsonDocument &doc)
{
  doc.to<JsonArray>();

  for(int i = 0; i < SCHEDULER_MAX_EVENTS; i++)
  {
    if(_events[i].isValid())
    {
      JsonObject obj = doc.createNestedObject();
      serialize(obj, &_events[i]);
    }
  }

  return true;
}

bool Scheduler::serialize(String& json, uint16_t event)
{
  // IMPROVE: do a better calculation of required space
  const size_t capacity = 4096;
  DynamicJsonDocument doc(capacity);

  if(Scheduler::serialize(doc, event))
  {
    serializeJson(doc, json);
    return true;
  }

  return false;
}

bool Scheduler::serialize(DynamicJsonDocument &doc, uint16_t event)
{
  JsonObject object = doc.to<JsonObject>();
  return serialize(object, event);
}

bool Scheduler::serialize(JsonObject &object, uint16_t id)
{
  Scheduler::Event *event = NULL;

  if(findEvent(id, &event) && NULL != event)
  {
    if(Scheduler::serialize(object, event)) {
      return true;
    }
  }

  return false;
}

bool Scheduler::serialize(JsonObject &object, Scheduler::Event *event)
{
  object["id"] = event->getId();
  object["state"] = event->getStateText();
  object["time"] = event->getTime();
  JsonArray days = object.createNestedArray("days");
  if(event->getDays() & SCHEDULER_DAY_MONDAY) {
    days.add("monday");
  }
  if(event->getDays() & SCHEDULER_DAY_TUESDAY) {
    days.add("tuesday");
  }
  if(event->getDays() & SCHEDULER_DAY_WEDNESDAY) {
    days.add("wednesday");
  }
  if(event->getDays() & SCHEDULER_DAY_THURSDAY) {
    days.add("thursday");
  }
  if(event->getDays() & SCHEDULER_DAY_FRIDAY) {
    days.add("friday");
  }
  if(event->getDays() & SCHEDULER_DAY_SATURDAY) {
    days.add("saturday");
  }
  if(event->getDays() & SCHEDULER_DAY_SUNDAY) {
    days.add("sunday");
  }

  return false;
}
