// Host-side tests for the event-log repeat filter (event_log_repeat.h).
//
// The filter is what stands between the History view and the pilot-state
// flicker that used to fill it: on a live unit, 75 of 80 retained entries were
// the same charging row. These tests pin the two halves of that decision - what
// counts as "the same entry", and how long sameness suppresses - because
// getting either wrong silently either restores the flood or hides real events.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "event_log_repeat.h"

// The charging row that flooded the live unit's log.
static EventLogEntryKey charging()
{
  EventLogEntryKey key = {0, 1, 3, 1344, 47, 0, 0};
  return key;
}

TEST_CASE("the first entry is never a repeat")
{
  EventLogRepeatFilter filter;
  CHECK(false == filter.isRepeat(charging(), 1000));
}

TEST_CASE("an identical entry inside the interval is a repeat")
{
  EventLogRepeatFilter filter;
  filter.recordWritten(charging(), 1000);

  CHECK(true == filter.isRepeat(charging(), 1000));
  CHECK(true == filter.isRepeat(charging(), 1000 + EVENTLOG_REPEAT_INTERVAL - 1));
}

TEST_CASE("an identical entry at or past the interval is kept")
{
  EventLogRepeatFilter filter;
  filter.recordWritten(charging(), 1000);

  CHECK(false == filter.isRepeat(charging(), 1000 + EVENTLOG_REPEAT_INTERVAL));
  CHECK(false == filter.isRepeat(charging(), 1000 + EVENTLOG_REPEAT_INTERVAL + 1));
}

TEST_CASE("suppression is measured from the last entry written, not the last seen")
{
  EventLogRepeatFilter filter;
  filter.recordWritten(charging(), 1000);

  // A run of repeats must not keep pushing the deadline out, or an unchanging
  // session would never leave a trace at all.
  CHECK(true == filter.isRepeat(charging(), 1200));
  CHECK(true == filter.isRepeat(charging(), 1250));
  CHECK(false == filter.isRepeat(charging(), 1000 + EVENTLOG_REPEAT_INTERVAL));
}

TEST_CASE("any visible field differing makes it a new entry")
{
  EventLogRepeatFilter filter;
  filter.recordWritten(charging(), 1000);

  SUBCASE("type")         { EventLogEntryKey k = charging(); k.type = 2;          CHECK(false == filter.isRepeat(k, 1001)); }
  SUBCASE("managerState") { EventLogEntryKey k = charging(); k.managerState = 2;  CHECK(false == filter.isRepeat(k, 1001)); }
  SUBCASE("evseState")    { EventLogEntryKey k = charging(); k.evseState = 254;   CHECK(false == filter.isRepeat(k, 1001)); }
  SUBCASE("evseFlags")    { EventLogEntryKey k = charging(); k.evseFlags = 1280;  CHECK(false == filter.isRepeat(k, 1001)); }
  SUBCASE("pilot")        { EventLogEntryKey k = charging(); k.pilot = 32;        CHECK(false == filter.isRepeat(k, 1001)); }
  SUBCASE("divertMode")   { EventLogEntryKey k = charging(); k.divertMode = 1;    CHECK(false == filter.isRepeat(k, 1001)); }
  SUBCASE("shaper")       { EventLogEntryKey k = charging(); k.shaper = 1;        CHECK(false == filter.isRepeat(k, 1001)); }
}

TEST_CASE("a repeated fault is suppressed, but the fault itself is never lost")
{
  EventLogRepeatFilter filter;

  // A GFCI fault while the pilot flickers: on the bench this wrote eight
  // identical warning rows in eight seconds, evicting the context needed to
  // read them. The first one must always survive; its copies must not.
  EventLogEntryKey fault = charging();
  fault.type = 2;          // warning
  fault.evseState = 6;     // GFI fault

  CHECK(false == filter.isRepeat(fault, 1000));
  filter.recordWritten(fault, 1000);
  CHECK(true == filter.isRepeat(fault, 1001));

  // A different fault is a different key, so it is never hidden behind the one
  // before it.
  EventLogEntryKey other = fault;
  other.evseState = 8;     // stuck relay
  CHECK(false == filter.isRepeat(other, 1001));
}

TEST_CASE("an information entry does not hide behind a warning with the same fields")
{
  EventLogRepeatFilter filter;

  EventLogEntryKey warned = charging();
  warned.type = 2;
  filter.recordWritten(warned, 1000);

  CHECK(false == filter.isRepeat(charging(), 1001));
}

TEST_CASE("a clock that jumps backwards does not suppress")
{
  EventLogRepeatFilter filter;

  // Entries can be written with a plausible-but-wrong clock and then NTP lands.
  // Suppressing until real time overtakes the bad reading could silence the log
  // for years, so a backwards jump always writes.
  filter.recordWritten(charging(), 2000000000);
  CHECK(false == filter.isRepeat(charging(), 1000));
}

TEST_CASE("recording a different entry resets what counts as a repeat")
{
  EventLogRepeatFilter filter;
  filter.recordWritten(charging(), 1000);

  EventLogEntryKey sleeping = charging();
  sleeping.evseState = 254;
  filter.recordWritten(sleeping, 1010);

  CHECK(true == filter.isRepeat(sleeping, 1011));
  CHECK(false == filter.isRepeat(charging(), 1011));
}

TEST_CASE("an entry says which fields put it there")
{
  EventLogRepeatFilter filter;

  // Nothing to compare against yet.
  CHECK(EVENTLOG_CHANGE_FIRST == filter.changedFrom(charging()));

  filter.recordWritten(charging(), 1000);
  CHECK(0 == filter.changedFrom(charging()));

  // The case that prompted this: the History view shows neither the pilot
  // current nor the status flags, so a row that moved only in those reads as a
  // duplicate of the one above it unless it says what changed.
  EventLogEntryKey throttled = charging();
  throttled.pilot = 42;
  CHECK(EVENTLOG_CHANGE_PILOT == filter.changedFrom(throttled));

  EventLogEntryKey relay = charging();
  relay.evseFlags = 1280;
  CHECK(EVENTLOG_CHANGE_FLAGS == filter.changedFrom(relay));

  // Several at once is the normal case for a real transition.
  EventLogEntryKey stopped = charging();
  stopped.evseState = 254;
  stopped.evseFlags = 1280;
  stopped.managerState = 2;
  CHECK((EVENTLOG_CHANGE_EVSE_STATE | EVENTLOG_CHANGE_FLAGS | EVENTLOG_CHANGE_MANAGER)
        == filter.changedFrom(stopped));
}

TEST_CASE("bookkeeping flag bits do not make an entry worth keeping")
{
  // 0x0200 SESSION_ENDED and 0x0400 EV_CONNECTED_PREV mirror state the log
  // already records. EV_CONNECTED_PREV in particular lags EV_CONNECTED by one
  // update, so on a live unit every plug-in wrote a second entry whose only
  // difference was that bit - and which nothing in a UI could explain.
  CHECK(0x0140 == eventLogSignificantFlags(0x0740));
  CHECK(0x0100 == eventLogSignificantFlags(0x0700));

  // Bits that mean something to a reader are untouched: charging relay,
  // EV connected, boot lock, hard fault.
  CHECK(0x4142 == eventLogSignificantFlags(0x4142));

  EventLogRepeatFilter filter;
  EventLogEntryKey connected = charging();
  connected.evseFlags = eventLogSignificantFlags(0x0300);   // EV_CONNECTED set
  filter.recordWritten(connected, 1000);

  // The follow-up update that only sets EV_CONNECTED_PREV is now the same
  // entry, so it is suppressed rather than logged as an unexplainable row.
  EventLogEntryKey lagged = charging();
  lagged.evseFlags = eventLogSignificantFlags(0x0700);      // + EV_CONNECTED_PREV
  CHECK(true == filter.isRepeat(lagged, 1027));
  CHECK(0 == filter.changedFrom(lagged));

  // A real relay move in the same breath still logs.
  EventLogEntryKey relay = charging();
  relay.evseFlags = eventLogSignificantFlags(0x0740);       // + CHARGING_ON
  CHECK(false == filter.isRepeat(relay, 1027));
  CHECK(EVENTLOG_CHANGE_FLAGS == filter.changedFrom(relay));
}
