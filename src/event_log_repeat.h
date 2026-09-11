// src/event_log_repeat.h -- deciding when a log entry says nothing new.
//
// The state event that drives event logging fires on any change of
// (evse_state, pilot_state, vflags), but only evse_state and vflags are
// written to the log, so a pilot_state flicker produces an entry identical to
// the one above it in every field a reader can see. Those repeats are worth
// nothing on their own, and in a log capped at EVENTLOG_MAX_ROTATE_COUNT
// files of EVENTLOG_ROTATE_SIZE bytes they are actively harmful: they push the
// session starts, stops and faults out of the retained window.
//
// The key below is deliberately only the fields a reader can distinguish.
// Time, energy, elapsed and temperature are excluded because they change on
// essentially every entry, so including them would suppress nothing; the
// repeat interval still leaves a periodic sample of them. The pilot state is
// excluded too: it is recorded in the entry so a reader can see what changed,
// but a flicker in it must not be the thing that keeps the entry.
//
// Deliberately free of Arduino, LittleFS and ArduinoJson so the decision can
// be unit tested on the host.
#ifndef __EVENT_LOG_REPEAT_H
#define __EVENT_LOG_REPEAT_H

#include <stdint.h>

// How long an entry suppresses identical successors. A long unchanging session
// still leaves a trace at this cadence rather than vanishing from the log.
#ifndef EVENTLOG_REPEAT_INTERVAL
#define EVENTLOG_REPEAT_INTERVAL    300
#endif

// Why an entry was kept. The event log records state, but a reader diffing two
// rows to work out what moved needs the row before it - which may have rotated
// away, or sit in another block. So each entry carries the fields that differ
// from the last one written.
//
// The pilot state is never a reason: it is recorded, but a flicker in it is
// exactly what the repeat filter exists to swallow.
#define EVENTLOG_CHANGE_FIRST       0x0001  // nothing to compare against (boot)
#define EVENTLOG_CHANGE_TYPE        0x0002
#define EVENTLOG_CHANGE_MANAGER     0x0004
#define EVENTLOG_CHANGE_EVSE_STATE  0x0008
#define EVENTLOG_CHANGE_FLAGS       0x0010
#define EVENTLOG_CHANGE_PILOT       0x0020
#define EVENTLOG_CHANGE_DIVERT      0x0040
#define EVENTLOG_CHANGE_SHAPER      0x0080
#define EVENTLOG_CHANGE_PERIODIC    0x0100  // nothing visible moved; the interval lapsed

// evseFlags bits that must not, on their own, create an entry: they mirror
// state the log already records, so a row whose only difference is one of them
// has nothing to tell a reader.
//
//   SESSION_ENDED     (0x0200) session-timing bookkeeping
//   EV_CONNECTED_PREV (0x0400) literally the previous value of EV_CONNECTED,
//                              so it lags it by one update and turns every
//                              plug-in into two entries instead of one
//
// The full evseFlags value is still recorded in the entry; this only excludes
// these bits from the comparison that decides whether an entry is worth
// keeping. Values mirror the OpenEVSE library's OPENEVSE_VFLAG_*, spelled out
// here so this header stays free of the EVSE library.
#define EVENTLOG_FLAGS_NOT_SIGNIFICANT (0x0200 | 0x0400)

// The part of evseFlags worth comparing entries on.
inline uint32_t eventLogSignificantFlags(uint32_t evseFlags)
{
  return evseFlags & ~((uint32_t)EVENTLOG_FLAGS_NOT_SIGNIFICANT);
}

struct EventLogEntryKey
{
  uint8_t type;
  uint8_t managerState;
  uint8_t evseState;
  uint32_t evseFlags;
  uint32_t pilot;
  uint8_t divertMode;
  uint8_t shaper;

  bool operator==(const EventLogEntryKey &rhs) const
  {
    return type == rhs.type &&
           managerState == rhs.managerState &&
           evseState == rhs.evseState &&
           evseFlags == rhs.evseFlags &&
           pilot == rhs.pilot &&
           divertMode == rhs.divertMode &&
           shaper == rhs.shaper;
  }

  bool operator!=(const EventLogEntryKey &rhs) const
  {
    return !(*this == rhs);
  }
};

class EventLogRepeatFilter
{
  public:
    EventLogRepeatFilter() :
      _have_last(false),
      _last_time(0),
      _last({0, 0, 0, 0, 0, 0, 0})
    {
    }

    // True when an entry with this key would tell a reader nothing that the
    // last written entry did not already say. The event type is part of the
    // key, so a fault always logs when it first appears; only its exact
    // repeats are dropped.
    bool isRepeat(const EventLogEntryKey &key, int64_t now) const
    {
      if(!_have_last) {
        return false;
      }

      if(_last != key) {
        return false;
      }

      // A clock that has jumped backwards - the first NTP sync after boot, say
      // - must not suppress entries until real time catches up again.
      if(now < _last_time) {
        return false;
      }

      return (now - _last_time) < EVENTLOG_REPEAT_INTERVAL;
    }

    // Which fields differ from the last entry written, as EVENTLOG_CHANGE_*
    // bits. EVENTLOG_CHANGE_FIRST when there is nothing to compare against;
    // zero when nothing differs, which only reaches the log once the repeat
    // interval has lapsed.
    uint16_t changedFrom(const EventLogEntryKey &key) const
    {
      if(!_have_last) {
        return EVENTLOG_CHANGE_FIRST;
      }

      uint16_t changed = 0;
      if(_last.type != key.type)                 changed |= EVENTLOG_CHANGE_TYPE;
      if(_last.managerState != key.managerState) changed |= EVENTLOG_CHANGE_MANAGER;
      if(_last.evseState != key.evseState)       changed |= EVENTLOG_CHANGE_EVSE_STATE;
      if(_last.evseFlags != key.evseFlags)       changed |= EVENTLOG_CHANGE_FLAGS;
      if(_last.pilot != key.pilot)               changed |= EVENTLOG_CHANGE_PILOT;
      if(_last.divertMode != key.divertMode)     changed |= EVENTLOG_CHANGE_DIVERT;
      if(_last.shaper != key.shaper)             changed |= EVENTLOG_CHANGE_SHAPER;

      return changed;
    }

    // Call after an entry is successfully written, never before: a write that
    // failed must not start suppressing its successors.
    void recordWritten(const EventLogEntryKey &key, int64_t now)
    {
      _have_last = true;
      _last = key;
      _last_time = now;
    }

  private:
    bool _have_last;
    int64_t _last_time;
    EventLogEntryKey _last;
};

#endif // !__EVENT_LOG_REPEAT_H
