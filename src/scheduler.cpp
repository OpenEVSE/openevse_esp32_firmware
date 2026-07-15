#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_SCHEDULER)
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "scheduler.h"
#include "scheduler_time.h"
#include "fs_util.h"
#include "time_man.h"
#include "emonesp.h"
#include "app_config.h"
#include "event.h"
#include "mqtt.h"
#include "divert.h"
#include "current_shaper.h"
#include "rfid.h"
#include "limit.h"

#include <algorithm>
#include <vector>
#include <LittleFS.h>

uint32_t Scheduler::Event::_next_id = 1;

Scheduler::EventInstance nullEventInstance;

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
  Event(SCHEDULER_EVENT_NULL, 0, 0, EvseState::None)
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

Scheduler::Event::Event(uint32_t id, uint32_t second, uint8_t days, EvseState state) :
  _id(id), _seconds(second), _days(days), _state(state), _next(0),
  _feature(SchedulerFeature::None), _feature_value(0),
  _limit_type(SchedulerLimitType::None), _limit_value(0)
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
  char time[14];
  snprintf(time, sizeof(time), "%02d:%02d:%02d", getHours(), getMinutes(), getSeconds());
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

  if(2 == sscanf(time, "%u:%02u", &hours, &minutes))
  {
    DBUGVAR(hours);
    DBUGVAR(minutes);
    setHours(hours);
    setMinutes(minutes);
    setSeconds(0);
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
  return _state.toString();
}

bool Scheduler::Event::setState(const char *state)
{
  return _state.fromString(state);
}

uint32_t Scheduler::EventInstance::getDuration()
{
  EventInstance &next = getNext();

  // Signed weekly-wheel maths (see scheduler_time.h; was an unsigned
  // underflow that produced a week-long, never-ending window).  A
  // non-positive span wraps to the following week: a lone event (its "next"
  // is itself), OR the last event of a day wrapping back to an earlier event
  // on the SAME day — e.g. a Sunday-only window where the 17:50 stop's next
  // is the 17:49 start a week later.  Without this the disabled "gap"
  // between windows is never current, so the feature looks active all week
  // and never turns off.
  return SchedulerTime::weeklySpan(_day, (int32_t)getStartOffset(),
                                   next._day, (int32_t)next.getStartOffset(),
                                   true);
}

int32_t Scheduler::EventInstance::getStartOffset(int fromDay, int dayOffset) {
  int offsetInDays = (_day + dayOffset) - fromDay;
  return (offsetInDays * 24 * 60 * 60) + getStartOffset();
}

uint32_t Scheduler::EventInstance::getDelay(int fromDay, uint32_t fromOffset)
{
  // Signed weekly-wheel maths (see scheduler_time.h): when the event already
  // passed today the subtraction underflowed to ~49 days as uint32_t.  A
  // delay of zero means "now" and does not wrap.
  return SchedulerTime::weeklySpan(fromDay, (int32_t)fromOffset,
                                   _day, (int32_t)getStartOffset(),
                                   false);
}

uint32_t Scheduler::EventInstance::randomiseStartOffset()
{
  if(!_event) {
    return 0;
  }

  int32_t offset = _event->getOffset();
  // scheduler_start_window staggers the start of plain charge sessions across a
  // fleet to spread grid load. Feature windows (divert/shaper/rfid/ocpp and
  // explicit charge-current windows) are precise control windows — randomising
  // their start can push it past the window's stop event, inverting the window
  // (see getDuration) and leaving the feature stuck on. Never stagger those.
  if(_event->getState() == EvseState::Active && _event->getFeature() == SchedulerFeature::None) {
    offset += random(-min((int32_t)scheduler_start_window, offset), scheduler_start_window);
  }
  return offset;
}

Scheduler::Scheduler(EvseManager &evse) :
  _evse(&evse),
  _events(),
  _firstEvent(),
  _activeEvent(),
  _loading(false),
  _timeChangeListener(this),
  _version(0),
  _plan_version(0),
  _activeFeature(SchedulerFeature::None),
  _activeLimitType(SchedulerLimitType::None)
{

}

void Scheduler::setup()
{
  _loading = true;

  // Load the schedule from storage.  Size the JSON doc to the actual file so
  // schedules with per-event feature/limit fields don't overflow the old fixed
  // 1024-byte budget and silently load as empty (ArduinoJson 6 NoMemory).
  File file = LittleFS.open(SCHEDULE_PATH);
  if(file)
  {
    size_t capacity = max((size_t)file.size() * 2, (size_t)4096);
    DynamicJsonDocument doc(capacity);
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err == DeserializationError::Code::Ok && !doc.overflowed()) {
      deserialize(doc);
    } else {
      DBUGLN("Scheduler: failed to load schedule (parse error or doc overflow)");
    }
  }

  timeManager.onTimeChange(&_timeChangeListener);

  _loading = false;
}

unsigned long Scheduler::loop(MicroTasks::WakeReason reason)
{
  DBUG("Scheduler woke: ");
  DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
         WakeReason_Event == reason ? "WakeReason_Event" :
         WakeReason_Message == reason ? "WakeReason_Message" :
         WakeReason_Manual == reason ? "WakeReason_Manual" :
         "UNKNOWN");

  EventInstance &currentEvent = getCurrentEvent();

  DBUGF("Current event %d: %s %s %s",
    currentEvent.isValid() ? currentEvent.getEvent()->getId() : -1,
    currentEvent.isValid() ? days_of_the_week_strings[currentEvent.getDay()] : "none",
    currentEvent.isValid() ? currentEvent.getEvent()->getTime().c_str() : "none",
    currentEvent.isValid() ? currentEvent.getEvent()->getStateText() : "none");

  DBUGF("Active event %d: %s %s %s",
    _activeEvent.isValid() ? _activeEvent.getEvent()->getId() : -1,
    _activeEvent.isValid() ? days_of_the_week_strings[_activeEvent.getDay()] : "none",
    _activeEvent.isValid() ? _activeEvent.getEvent()->getTime().c_str() : "none",
    _activeEvent.isValid() ? _activeEvent.getEvent()->getStateText() : "none");

  if(currentEvent != _activeEvent)
  {
    DBUG("New event: ");

    // Clean up any feature/limit applied by the previous active event
    if(_activeFeature != SchedulerFeature::None) {
      cleanupFeature(_activeFeature);
      _activeFeature = SchedulerFeature::None;
    }
    if(_activeLimitType != SchedulerLimitType::None) {
      // Restore the user's persistent default limit rather than wiping it.
      limit.setDefaultLimit(limit_default_type.c_str(), limit_default_value);
      _activeLimitType = SchedulerLimitType::None;
    }

    // We need to change state
    if(currentEvent.isValid())
    {
      DBUGF("Starting %s claim",
        currentEvent.getState().toString());
      EvseProperties properties(currentEvent.getState());
      int priority = EvseManager_Priority_Default;
      if(EvseState::Active == currentEvent.getState())
      {
        priority = EvseManager_Priority_Timer;
        Event *e = currentEvent.getEvent();

        // Charge current: use feature_value if current feature selected, else hardware max
        if(e->getFeature() == SchedulerFeature::Current && e->getFeatureValue() > 0) {
          properties.setChargeCurrent(e->getFeatureValue());
        } else {
          properties.setChargeCurrent(_evse->getMaxHardwareCurrent());
        }

        // Apply feature (divert, shaper, rfid, etc.)
        applyFeature(e);
        _activeFeature = e->getFeature();

        // Apply session limit (applyLimit returns false for Cost/unimplemented
        // types so we don't mark them active and trigger a spurious cleanup).
        if(e->getLimitType() != SchedulerLimitType::None && applyLimit(e)) {
          _activeLimitType = e->getLimitType();
        }
      }
      _evse->claim(EvseClient_OpenEVSE_Schedule, priority, properties);
    } else {
      // No scheduled events, release any claims
      DBUGLN("releasing claims");
      _evse->release(EvseClient_OpenEVSE_Schedule);
    }

    _activeEvent = currentEvent;

    StaticJsonDocument<128> doc;
    doc["schedule_plan_version"] = ++_plan_version;
    event_send(doc);
  }

  int currentDay;
  int32_t currentOffset;

  uint32_t delay = MicroTask.Infinate;
  if(_activeEvent.isValid())
  {
    DBUGF("Next event %d: %s %s %s",
      _activeEvent.getNext().getEvent()->getId(),
      days_of_the_week_strings[_activeEvent.getNext().getDay()],
      _activeEvent.getNext().getEvent()->getTime().c_str(),
      _activeEvent.getNext().getEvent()->getStateText());

    Scheduler::getCurrentTime(currentDay, currentOffset);
    delay = _activeEvent.getNext().getDelay(currentDay, currentOffset) * 1000;
    DBUGVAR(delay);
  }
  return delay;
}

bool Scheduler::begin()
{
  MicroTask.startTask(this);

  return true;
}

bool Scheduler::commit()
{
  bool ret = false;

  buildSchedule();

  if(_loading) {
    return true;
  }

  // Serialize first and ensure there's room, so a full filesystem never
  // truncates a previously-valid schedule file into a corrupt one.
  DynamicJsonDocument doc(scheduleJsonCapacity());
  if(!serialize(doc) || doc.overflowed() || !littlefs_has_space(measureJson(doc))) {
    DBUGLN("Scheduler: insufficient space or doc overflow, keeping existing file");
    return false;
  }

  // Save the schedule to storage
  File file = LittleFS.open(SCHEDULE_PATH, FILE_WRITE);
  if(file)
  {
    ret = serializeJson(doc, file) > 0;
    file.close();
    if(!ret) {
      // Write failed part-way — drop the corrupt file rather than keep it.
      LittleFS.remove(SCHEDULE_PATH);
    }
  }

  return ret;
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
        if(days & (1 << day)) {
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
      DBUGF("Event %d: %s %s %s %d %d", event->getId(), days_of_the_week_strings[day], event->getTime().c_str(), event->getStateText(), event->getOffset(), e.getStartOffset());

      e.moveToNext();
    } while(e != _firstEvent);
    #endif // ENABLE_DEBUG
  }

  StaticJsonDocument<128> doc;
  doc["schedule_version"] = ++_version;
  doc["schedule_plan_version"] = ++_plan_version;
  event_send(doc);

  // publish updated schedule to mqtt
  mqtt.publishSchedule();

  // wake the main task to see if we actually need to do something
  MicroTask.wakeTask(this);
}

Scheduler::EventInstance &Scheduler::getCurrentEvent()
{
  int currentDay;
  int32_t currentOffset;

  Scheduler::getCurrentTime(currentDay, currentOffset);

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

Scheduler::EventInstance &Scheduler::getNextEvent(EvseState type)
{
  DBUGVAR(type);
  EventInstance *event = &_activeEvent; // Assume active event is correct
  if(event->isValid())
  {
    event = &event->getNext();
    if(EvseState::None != type)
    {
      EventInstance *startEvent = event;

      while(event->getState() != type)
      {
        event = &event->getNext();
        DBUGVAR((uint32_t)event, HEX);
        DBUGVAR((uint32_t)startEvent, HEX);
        if(startEvent == event) {
          return nullEventInstance;
        }
      }
    }
  }

  return *event;
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

Scheduler::Event *Scheduler::addEventInternal(uint32_t event_id, const char *time, uint8_t days, const char *state)
{
  Event *event = NULL;
  bool foundEvent = findEvent(event_id, &event);
  if(!foundEvent && SCHEDULER_EVENT_NULL != event_id) {
    foundEvent = findEvent(SCHEDULER_EVENT_NULL, &event);
  }

  if(foundEvent)
  {
    event->setId(event_id);
    event->setTime(time);
    event->setState(state);
    event->setDays(days);
    // Clear feature/limit when event is overwritten
    event->setFeature(SchedulerFeature::None);
    event->setFeatureValue(0);
    event->setLimitType(SchedulerLimitType::None);
    event->setLimitValue(0);
    return event;
  }

  return nullptr;
}

bool Scheduler::addEvent(uint32_t event_id, const char *time, uint8_t days, const char *state)
{
  if(addEventInternal(event_id, time, days, state) != nullptr)
  {
    return commit();
  }

  return false;
}

bool Scheduler::addEvent(uint32_t event_id, int hour, int minute, int second, uint8_t days, EvseState state)
{
  Event *event = NULL;
  bool foundEvent = findEvent(event_id, &event);
  if(!foundEvent && SCHEDULER_EVENT_NULL != event_id) {
    foundEvent = findEvent(SCHEDULER_EVENT_NULL, &event);
  }

  if(foundEvent)
  {
    event->setId(event_id);
    event->setHours(hour);
    event->setMinutes(minute);
    event->setSeconds(second);
    event->setState(state);

    event->setDays(days);

    commit();

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

    commit();
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
  // Parsing from const char* copies keys/values into the pool, so a
  // multi-rule Charge Manager schedule overflows a fixed 1024 budget at
  // ~2 events.  Size from the input instead (2x covers ArduinoJson overhead).
  const size_t capacity = max((size_t)4096, strlen(json) * 2);
  DynamicJsonDocument doc(capacity);

  DeserializationError err = deserializeJson(doc, json);
  if(DeserializationError::Code::Ok == err && !doc.overflowed()) {
    return Scheduler::deserialize(doc);
  }

  return false;
}

bool Scheduler::deserialize(Stream &stream)
{
  // Size from the bytes remaining in the stream (see deserialize(const char*)).
  const size_t capacity = max((size_t)4096, (size_t)stream.available() * 2);
  DynamicJsonDocument doc(capacity);

  DeserializationError err = deserializeJson(doc, stream);
  if(DeserializationError::Code::Ok == err && !doc.overflowed()) {
    return Scheduler::deserialize(doc);
  }

  DBUGLN("Failed to load schedule");

  return false;
}

bool Scheduler::deserialize(DynamicJsonDocument &doc)
{
  if (doc.is<JsonObject>())
  {
    JsonObject obj = doc.as<JsonObject>();
    if(Scheduler::deserializeInternal(obj, SCHEDULER_EVENT_NULL)) {
      return commit();
    }
  }
  else if(doc.is<JsonArray>())
  {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant value : arr)
    {
      if (value.is<JsonObject>())
      {
        JsonObject obj = value.as<JsonObject>();
        if(false == Scheduler::deserializeInternal(obj, SCHEDULER_EVENT_NULL)) {
          return false;
        }
      }
    }

    return commit();
  }

  return false;
}

bool Scheduler::deserialize(String& json, uint32_t event)
{
  return Scheduler::deserialize(json.c_str(), event);
}

bool Scheduler::deserialize(const char *json, uint32_t event)
{
  // Single event, but with feature/limit fields 1024 was borderline; size
  // from the input like the bulk path.
  const size_t capacity = max((size_t)2048, strlen(json) * 2);
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
  if(deserializeInternal(obj, event_id)) {
    buildSchedule();
    return true;
  }

  return false;
}

bool Scheduler::deserializeInternal(JsonObject &obj, uint32_t event_id)
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

    Event *event = addEventInternal(event_id, time, days, state);
    if(event != nullptr) {
      if(obj.containsKey("feature")) {
        event->setFeature(obj["feature"].as<const char *>());
      }
      if(obj.containsKey("feature_value")) {
        event->setFeatureValue((uint32_t)obj["feature_value"]);
      }
      if(obj.containsKey("limit")) {
        event->setLimitType(obj["limit"].as<const char *>());
      }
      if(obj.containsKey("limit_value")) {
        event->setLimitValue((uint32_t)obj["limit_value"]);
      }
      return true;
    }
  }

  return false;
}

size_t Scheduler::scheduleJsonCapacity()
{
  size_t count = 0;
  for(int i = 0; i < SCHEDULER_MAX_EVENTS; i++)
  {
    if(_events[i].isValid()) {
      count++;
    }
  }

  // Per-event budget: object of up to 8 members (128) + days array of up to 7
  // (112) + copied key/value strings (time/state/feature/limit names, ~100),
  // rounded up to 384; 512 headroom for the enclosing array and slop.
  return 512 + count * 384;
}

bool Scheduler::serialize(String& json)
{
  DynamicJsonDocument doc(scheduleJsonCapacity());

  if(Scheduler::serialize(doc))
  {
    serializeJson(doc, json);
    return true;
  }

  return false;
}

bool Scheduler::serialize(Stream &stream)
{
  DynamicJsonDocument doc(scheduleJsonCapacity());

  if(Scheduler::serialize(doc))
  {
    serializeJson(doc, stream);
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

  // On overflow createNestedObject() returns null objects and events are
  // silently dropped from the output — report that as a failure rather than
  // serving a truncated schedule.
  return !doc.overflowed();
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

  if(event->getFeature() != SchedulerFeature::None) {
    object["feature"] = event->getFeatureName();
    object["feature_value"] = event->getFeatureValue();
  }

  if(event->getLimitType() != SchedulerLimitType::None) {
    object["limit"] = event->getLimitName();
    object["limit_value"] = event->getLimitValue();
  }

  return true;
}

void Scheduler::serializeEventInstance(JsonObject &object, Scheduler::EventInstance *e, bool includeDay)
{
  object["id"] = e->getEvent()->getId();
  object["state"] = e->getEvent()->getStateText();
  object["time"] = e->getEvent()->getTime();
  object["day"] = days_of_the_week_strings[e->getDay()];
  object["offset"] = e->getEvent()->getOffset();
  object["start_offset"] = e->getStartOffset();
  object["diff"] = (int32_t)(e->getStartOffset()) - (int32_t)(e->getEvent()->getOffset());
  object["duration"] = e->getDuration();
}

bool Scheduler::serializePlan(DynamicJsonDocument &doc)
{
  JsonObject root = doc.to<JsonObject>();

  int currentDay;
  int32_t currentOffset;
  Scheduler::getCurrentTime(currentDay, currentOffset);

  root["current_day"] = days_of_the_week_strings[currentDay];
  root["current_offset"] = currentOffset;

  Scheduler::EventInstance *e = &_activeEvent;
  if(e->isValid())
  {
    root["next_event_delay"] = e->getNext().getDelay(currentDay, currentOffset);

    JsonObject object = root.createNestedObject("current_event");
    serializeEventInstance(object, e, true);
    e = &e->getNext();
    object = root.createNestedObject("next_event");
    serializeEventInstance(object, e, true);
  } else {
    root["next_event_delay"] = false;
    root["current_event"] = false;
    root["next_event"] = false;
  }

  e = &_firstEvent;
  if(e->isValid())
  {
    int day = e->getDay();
    JsonArray currentDay = root.createNestedArray(days_of_the_week_strings[day]);

    do
    {

      if(e->getDay() != day)
      {
        day = e->getDay();
        currentDay = root.createNestedArray(days_of_the_week_strings[day]);
      }

      JsonObject object = currentDay.createNestedObject();
      serializeEventInstance(object, e);

      e = &e->getNext();
    } while(_firstEvent != e);
  }

  return true;
}

void Scheduler::notifyConfigChanged()
{
  buildSchedule();
}

void Scheduler::getCurrentTime(int &day, int32_t &offset)
{
  timeval utc_time;
  gettimeofday(&utc_time, NULL);

  tm local_time;
  localtime_r(&utc_time.tv_sec, &local_time);

  DBUGF("Local time: %s", time_format_time(local_time).c_str());

  day = local_time.tm_wday;
  offset =
    (local_time.tm_hour * 3600) +
    (local_time.tm_min * 60) +
    local_time.tm_sec;
}

// ── Event feature/limit string converters ────────────────────────────────────

static const char * const feature_names[] = {
  "none", "divert", "shaper", "ocpp", "rfid", "current"
};

bool Scheduler::Event::setFeature(const char *name)
{
  for(uint8_t i = 0; i < sizeof(feature_names)/sizeof(feature_names[0]); i++) {
    if(0 == strcmp(name, feature_names[i])) {
      _feature = (SchedulerFeature)i;
      return true;
    }
  }
  _feature = SchedulerFeature::None;
  return false;
}

const char *Scheduler::Event::getFeatureName()
{
  uint8_t idx = (uint8_t)_feature;
  if(idx < sizeof(feature_names)/sizeof(feature_names[0])) {
    return feature_names[idx];
  }
  return feature_names[0];
}

static const char * const limit_names[] = {
  "none", "time", "energy", "soc", "cost"
};

bool Scheduler::Event::setLimitType(const char *name)
{
  for(uint8_t i = 0; i < sizeof(limit_names)/sizeof(limit_names[0]); i++) {
    if(0 == strcmp(name, limit_names[i])) {
      _limit_type = (SchedulerLimitType)i;
      return true;
    }
  }
  _limit_type = SchedulerLimitType::None;
  return false;
}

const char *Scheduler::Event::getLimitName()
{
  uint8_t idx = (uint8_t)_limit_type;
  if(idx < sizeof(limit_names)/sizeof(limit_names[0])) {
    return limit_names[idx];
  }
  return limit_names[0];
}

// ── Scheduler feature/limit helpers ──────────────────────────────────────────

void Scheduler::applyFeature(Event *event)
{
  switch(event->getFeature())
  {
    case SchedulerFeature::Divert:
      // Enter eco mode at elevated priority (1100) so it overrides OCPP/RFID/Manual
      divert.setTimerDivertActive(true);
      break;
    case SchedulerFeature::Shaper:
      shaper.setTimerEnabled(true);
      break;
    case SchedulerFeature::RFID:
      // Re-probe at window start — the boot-time presence check can
      // false-negative and never recovers on its own.
      if(!rfid.probeReader()) {
        // No reader — timer-RFID cannot function; skip enforcement so the
        // rest of the scheduled event (state/current) still applies, but
        // surface it: silently failing open is wrong for an access-control
        // feature.
        DBUGLN("Scheduler: no RFID reader present, skipping timer-RFID feature");
        StaticJsonDocument<64> evt;
        evt["schedule_feature_skipped"] = "rfid";
        event_send(evt);
        break;
      }
      rfid.setTimerRequired(true);
      break;
    case SchedulerFeature::OCPP:
      // OCPP manages its own claim state; no additional action here
      break;
    case SchedulerFeature::Current:
      // Handled above in loop(): charge current set in the EVSE claim
      break;
    case SchedulerFeature::None:
    default:
      break;
  }
}

void Scheduler::cleanupFeature(SchedulerFeature feature)
{
  switch(feature)
  {
    case SchedulerFeature::Divert:
      divert.setTimerDivertActive(false);
      break;
    case SchedulerFeature::Shaper:
      shaper.setTimerEnabled(false);
      break;
    case SchedulerFeature::RFID:
      rfid.setTimerRequired(false);
      break;
    case SchedulerFeature::Current:
      // Charge current resets automatically when the schedule claim is re-made
      // with getMaxHardwareCurrent() on the next active event or released here
      break;
    default:
      break;
  }
}

bool Scheduler::applyLimit(Event *event)
{
  LimitProperties props;
  LimitType type;

  switch(event->getLimitType())
  {
    case SchedulerLimitType::Time:   type = LimitType::Time;   break;
    case SchedulerLimitType::Energy: type = LimitType::Energy; break;
    case SchedulerLimitType::Soc:    type = LimitType::Soc;    break;
    case SchedulerLimitType::Cost:
      // Cost limit not yet enforced (requires tariff config); skip activation
      // so _activeLimitType stays None and cleanup is never triggered.
      DBUGLN("Scheduler: cost limit stored but not yet enforced");
      return false;
    default:
      return false;
  }

  props.setType(type);
  props.setValue(event->getLimitValue());
  props.setAutoRelease(true);
  limit.set(props);
  return true;
}
