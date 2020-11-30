
#include "debug.h"
#include "scheduler.h"
#include "time_man.h"

#include <algorithm>
#include <vector>

uint32_t Scheduler::Event::_next_id = 1;

const char * days_of_the_week_strings[] = {
  "sunday",
  "monday",
  "tuesday",
  "wednesday",
  "thursday",
  "friday",
  "saturday"
};

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

Scheduler::Event::Event(uint32_t id, uint8_t hour, uint8_t minute, uint8_t second, uint8_t days, bool repeat, EvseState state) :
  Event(id,
        (hour * 3600) + (minute * 60) + second,
        days | (repeat ? SCHEDULER_REPEAT : 0),
        state)
{
}

Scheduler::Event::Event(uint32_t id, uint32_t second, uint8_t days, EvseState state)
{
  if(id >= _next_id) {
    _next_id = id + 1;
  }
}

void Scheduler::Event::setId(uint32_t id) {
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
  uint32_t hours, minutes, seconds;

  DBUGVAR(time);
  if(3 == sscanf(time, "%u:%02u:%02u", &hours, &minutes, &seconds))
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

void Scheduler::Event::setTime(uint32_t hours, uint32_t seconds, uint32_t minutes)
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

uint32_t Scheduler::EventInstance::getDuration()
{
  int lengthInDays = getNext()._day - _day;
  if(lengthInDays < 0) {
    lengthInDays += SCHEDULER_DAYS_IN_A_WEEK;
  }

  uint32_t duration = (lengthInDays * 24 * 60 * 60) + (getNext().getStartOffset() - getStartOffset());
  // Handle special case where the duration is 0 (IE only single event so the next event is this event)
  if(0 == duration) {
    // Event duration is a week
    duration = 7 * 24 * 60 * 60;
  }
  return duration;
}

int32_t Scheduler::EventInstance::getStartOffset(int fromDay, int dayOffset) {
  int offsetInDays = (_day + dayOffset) - fromDay;
  return (offsetInDays * 24 * 60 * 60) + getStartOffset();
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
  EventInstance currentEvent = getCurrentEvent();

  if(currentEvent != _activeEvent)
  {

  }

  return MicroTask.Infinate;
}

bool Scheduler::begin()
{
  MicroTask.startTask(this);

  return true;
}

void Scheduler::buildSchedule()
{
  std::vector<Event *>by_week[SCHEDULER_DAYS_IN_A_WEEK];

  // Sort out which events happen on which days
  for(int i = 0; i < SCHEDULER_MAX_EVENTS; i++)
  {
    Event *event = &_events[i];
    if(event->isValid())
    {
      uint8_t days = event->getDays();
      for(int day = 0; day < SCHEDULER_DAYS_IN_A_WEEK; day++)
      {
        if(days & 1 << day) {
          by_week[day].push_back(event);
        }
      }
    }
  }

  // Sort each day
  for(int day = 0; day < SCHEDULER_DAYS_IN_A_WEEK; day++) {
    sort(by_week[day].begin(), by_week[day].end(), [](Event *a, Event *b)->bool {
      return a->getOffset() < b->getOffset();
    });
  }

  // Create linked list of ordered events
  _firstEvent.invalidate();
  EventInstance last_event;
  for(int day = 0; day < SCHEDULER_DAYS_IN_A_WEEK; day++)
  {
    for(auto event : by_week[day])
    {
      DBUGF("Event %d: %s %s %s", event->getId(), days_of_the_week_strings[day], event->getTime().c_str(), event->getStateText());
      if(!_firstEvent.isValid()) {
        _firstEvent.setEvent(event, day);
      }

      if(last_event.isValid()) {
        last_event.setNext(event, day);
      }

      last_event.setEvent(event, day);
    }
  }

  if(_firstEvent.isValid())
  {
    DBUGF("first event: %s %s %d", days_of_the_week_strings[_firstEvent.getDay()],
      _firstEvent.getEvent()->getTime().c_str(),
      _firstEvent.getEvent()->getId());
    DBUGF("last event: %s %s %d", days_of_the_week_strings[last_event.getDay()],
      last_event.getEvent()->getTime().c_str(),
      last_event.getEvent()->getId());

    last_event.setNext(_firstEvent);

    #ifdef ENABLE_DEBUG
    EventInstance e = _firstEvent;
    do
    {
      if(!e.isValid()) {
        DBUGLN("*** NULL ***");
        break;
      }

      Event *event = e.getEvent();
      int day = e.getDay();
      DBUGF("Event %d: %s %s %s", event->getId(), days_of_the_week_strings[day], event->getTime().c_str(), event->getStateText());

      e.moveToNext();
    } while(e != _firstEvent);
    #endif // ENABLE_DEBUG
  }

  // wake the main task to see if we actually need to do something
  MicroTask.wakeTask(this);
}

Scheduler::EventInstance &Scheduler::getCurrentEvent()
{
  timeval utc_time;
  gettimeofday(&utc_time, NULL);

  tm local_time;
  localtime_r(&utc_time.tv_sec, &local_time);

  DBUGF("Local time: %s", time_format_time(local_time).c_str());

  int currentDay = local_time.tm_wday;
  int32_t currentOffset =
    (local_time.tm_hour * 3600) +
    (local_time.tm_min * 60) +
    local_time.tm_sec;

  DBUGVAR(currentDay);
  DBUGVAR(currentOffset);

  // This is very much a brute force method of resolving the event
  Scheduler::EventInstance *e = &_firstEvent;
  int dayOffset = -7;
  do
  {
    if(!e->isValid()) {
      DBUGLN("No events");
      break;
    }

    int32_t startOffset = e->getStartOffset(currentDay, dayOffset);
    int32_t endOffset = e->getEndOffset(currentDay, dayOffset);
    DBUGVAR(startOffset);
    DBUGVAR(endOffset);

    if(startOffset <= currentOffset && currentOffset < endOffset)
    {
      DBUGF("Found event %d: %s %s %s",
        e->getEvent()->getId(),
        days_of_the_week_strings[e->getDay()],
        e->getEvent()->getTime().c_str(),
        e->getEvent()->getStateText());
      break;
    }

    e = &e->getNext();
    if(_firstEvent == e) {
      dayOffset += SCHEDULER_DAYS_IN_A_WEEK;
    }
  } while(dayOffset <= SCHEDULER_DAYS_IN_A_WEEK);
  return *e;
}

bool Scheduler::findEvent(uint32_t id, Scheduler::Event **event)
{
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

bool Scheduler::addEvent(uint32_t event_id, const char *time, uint8_t days, const char *state)
{
  Event *event;
  if(findEvent(event_id, &event))
  {
    event->setId(event_id);
    event->setTime(time);
    DBUGVAR(event->getHours());
    DBUGVAR(event->getMinutes());
    DBUGVAR(event->getSeconds());
    event->setState(state);

    event->setDays(days);

    buildSchedule();

    return true;
  }

  return false;
}

bool Scheduler::addEvent(String& json)
{
  return deserialize(json, SCHEDULER_EVENT_NULL);
}

bool Scheduler::addEvent(const char *json)
{
  return deserialize(json, SCHEDULER_EVENT_NULL);
}

bool Scheduler::addEvent(DynamicJsonDocument &doc)
{
  return deserialize(doc, SCHEDULER_EVENT_NULL);
}

bool Scheduler::removeEvent(uint32_t id)
{
  Scheduler::Event *event = NULL;

  DBUGVAR(id);
  if(findEvent(id, &event) && NULL != event)
  {
    DBUGF("event = %p", event);
    event->invalidate();

    MicroTask.wakeTask(this);

    return true;
  }

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

bool Scheduler::deserialize(String& json, uint32_t event)
{
  return Scheduler::deserialize(json.c_str(), event);
}

bool Scheduler::deserialize(const char *json, uint32_t event)
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

bool Scheduler::deserialize(DynamicJsonDocument &doc, uint32_t event)
{
  JsonObject object = doc.as<JsonObject>();

  return Scheduler::deserialize(object, event);
}

bool Scheduler::deserialize(JsonObject &obj, uint32_t event_id)
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
    const char *time = obj["time"].as<const char *>();
    const char *state = obj["state"].as<const char *>();

    uint8_t days = SCHEDULER_REPEAT;

    for (JsonVariant value : obj["days"].as<JsonArray>())
    {
      String day = value.as<String>();
      DBUGVAR(day);
      for(int i = 0; i < SCHEDULER_DAYS_IN_A_WEEK; i++) {
        if(day == days_of_the_week_strings[i]) {
          days |= 1 << i;
        }
      }
    }

    DBUGVAR(days);

    if(addEvent(event_id, time, days, state)) {
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

bool Scheduler::serialize(String& json, uint32_t event)
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

bool Scheduler::serialize(DynamicJsonDocument &doc, uint32_t event)
{
  JsonObject object = doc.to<JsonObject>();
  return serialize(object, event);
}

bool Scheduler::serialize(JsonObject &object, uint32_t id)
{
  Scheduler::Event *event = NULL;

  DBUGVAR(id);
  if(findEvent(id, &event) && NULL != event)
  {
    DBUGF("event = %p", event);
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
  for(int day = 0; day < SCHEDULER_DAYS_IN_A_WEEK; day++) {
    if(event->getDays() & 1<<day) {
      days.add(days_of_the_week_strings[day]);
    }
  }

  return true;
}
