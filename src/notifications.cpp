#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_NOTIFICATIONS)
#undef ENABLE_DEBUG
#endif

#include "notifications.h"
#include "debug.h"
#include "app_config.h"
#include "emonesp.h"          // currentfirmware
#include "event_log.h"
#include "input.h"            // extern EventLog eventLog
#include "temp_throttle.h"
#include "divert.h"
#include "current_shaper.h"
#include "event.h"            // event_send()

#include <string.h>

Notifications notifications;

// Epoch seconds, or 0 while the clock has not been synced yet. The spec's
// first_seen / last_seen are epoch, and an advisory raised seconds after boot
// - which is the common case for the safety rules - would otherwise be stamped
// somewhere in 1970 and rendered as such. 0 is the documented "unknown", which
// a consumer can tell apart from a real reading; the tm_year test is the same
// one event_log.cpp uses to decide a clock is trustworthy.
static uint32_t notification_epoch_now()
{
  time_t now = time(NULL);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  if(timeinfo.tm_year < (2021 - 1900)) {
    return 0;
  }
  return (uint32_t)now;
}

Notifications::Notifications() :
  MicroTasks::Task(),
  _evse(NULL),
  _count(0),
  _ack_count(0),
  _logged_count(0)
{
  memset(_first_seen, 0, sizeof(_first_seen));
  memset(_last_seen, 0, sizeof(_last_seen));
}

void Notifications::begin(EvseManager &evse)
{
  _evse = &evse;
  MicroTask.startTask(this);
}

void Notifications::setup()
{
  // A firmware update is a service event, so the acks that preceded it are
  // dropped wholesale. A power cut is not, so they survive one.
  if(notification_acks_fw != currentfirmware) {
    DBUGF("Notifications: firmware changed (%s -> %s), dropping acks",
          notification_acks_fw.c_str(), currentfirmware.c_str());
    notification_acks = "";
    notification_acks_fw = currentfirmware;
    config_commit();
  }
  _ack_count = notification_acks_decode(notification_acks.c_str(), _acks, NOTIFICATION_ACK_MAX);
}

void Notifications::gather(NotificationInputs &in)
{
  in.ground_check = _evse->isGroundCheckEnabled();
  in.gfci_check   = _evse->isGfiTestEnabled();
  in.relay_check  = _evse->isStuckRelayCheckEnabled();
  in.diode_check  = _evse->isDiodeCheckEnabled();
  in.vent_check   = _evse->isVentRequiredEnabled();
  in.temp_check   = _evse->isTemperatureCheckEnabled();

  in.gfci_count        = (uint32_t)_evse->getFaultCountGFCI();
  in.no_ground_count   = (uint32_t)_evse->getFaultCountNoGround();
  in.stuck_relay_count = (uint32_t)_evse->getFaultCountStuckRelay();

  in.temp_throttling = tempThrottle.isThrottling();
  in.temp_valid   = false;
  in.temp_c_x10   = 0;
  for(uint8_t s = 0; s < EVSE_MONITOR_TEMP_COUNT; s++) {
    if(_evse->isTemperatureValid(s)) {
      int32_t t = (int32_t)(_evse->getTemperature(s) * 10.0);
      if(!in.temp_valid || t > in.temp_c_x10) {
        in.temp_c_x10 = t;
      }
      in.temp_valid = true;
    }
  }
  in.panic_temp_c = (int32_t)_evse->getPanicTemperature();

  in.relay_health_known = _evse->isRelayHealthKnown();
  in.relay_life_pct                = _evse->getRelayLifeRemainingPct();
  in.relay_cold_open_count         = _evse->getRelayColdOpenCount();
  in.relay_transit_drift           = _evse->isRelayTransitDriftWarning();
  in.relay_thermal_warning_level   = _evse->getRelayThermalWarningLevel();
  in.relay_stuck_recovery_count    = _evse->getRelayStuckRecoveryCount();

  in.settings_flags = _evse->getSettingsFlags();
}

void Notifications::saveAcks()
{
  // 20 entries of "xx:ffffffff;" is 240 bytes; encode() degrades by dropping
  // entries rather than overrunning, so an undersized buffer here would
  // silently LOSE acks.
  char buf[320];
  notification_acks_encode(_acks, _ack_count, buf, sizeof(buf));
  if(notification_acks != buf) {
    notification_acks = buf;
    notification_acks_fw = currentfirmware;
    config_commit();
  }
}

size_t Notifications::unmuted(Notification *out)
{
  size_t n = 0;
  for(size_t i = 0; i < _count; i++) {
    if(!notification_acks_is_acked(_acks, _ack_count, _live[i].key, _live[i].token)) {
      out[n++] = _live[i];
    }
  }
  return n;
}

unsigned long Notifications::loop(MicroTasks::WakeReason reason)
{
  Notification previous[NOTIFICATION_MAX];
  uint32_t previous_first[NOTIFICATION_MAX];
  size_t previous_count = _count;
  memcpy(previous, _live, sizeof(previous));
  memcpy(previous_first, _first_seen, sizeof(previous_first));

  NotificationInputs in;
  gather(in);
  _count = notifications_evaluate(in, _live, NOTIFICATION_MAX);

  uint32_t now = notification_epoch_now();
  bool changed = (_count != previous_count);

  // At most one event-log row per pass. EventLog::log() costs two full
  // LittleFS traversals (totalBytes() + usedBytes(), 30-140 ms each) for its
  // free-space guard, and six to nine advisories can become knowable on the
  // same pass -- the boot pass, typically -- which would be seconds of
  // blocking work in one MicroTask iteration. The rest are still deduped by
  // id and take their row on a later 5 s tick, because eligibility below is
  // "this id has no row yet", not "this is the raise edge".
  bool logged_this_pass = false;

  // Don't attempt a row until the clock is trusted. Eligibility is now "not
  // yet logged" rather than "raise edge", so an advisory that log() refuses
  // is retried on every pass until it lands -- and before NTP syncs, log()
  // refuses all of them at its tm_year < 2021 check, which is the common case
  // for the safety rules that are knowable within seconds of boot. Testing it
  // here (notification_epoch_now() returns 0 for exactly that case, using the
  // same test) keeps the retry loop from running against a clock that can
  // only ever say no, and keeps the invariant in this function instead of
  // depending on the order of the guards inside log(). Once the clock does
  // sync, the advisories that are still live take their rows one per pass.
  bool clock_trusted = (0 != now);

  for(size_t i = 0; i < _count; i++) {
    // Carry first_seen across from the previous pass; a rule that was absent
    // last pass is a raise edge.
    bool was_live = false;
    for(size_t j = 0; j < previous_count; j++) {
      if(0 == strcmp(previous[j].key, _live[i].key)) {
        _first_seen[i] = previous_first[j];
        was_live = true;
        if(previous[j].severity != _live[i].severity ||
           previous[j].token != _live[i].token)
        {
          changed = true;
        }
        break;
      }
    }
    _last_seen[i] = now;
    if(!was_live) {
      _first_seen[i] = now;
      changed = true;
    }

    // One event-log row per advisory id per boot. NOT conditional on the
    // raise edge: log() can refuse a row (see below), and an advisory that is
    // refused stays live, so a raise-edge-only attempt would be the only
    // attempt this boot and the row would simply be lost. Eligibility is
    // therefore "this id has no row yet", which an advisory keeps for as long
    // as it stays live. Keyed on id (not array index) so clearing an earlier
    // advisory can never make a later one slide into a logged slot and
    // re-log.
    bool already_logged = false;
    for(size_t l = 0; l < _logged_count; l++) {
      if(0 == strcmp(_logged_ids[l], _live[i].id)) {
        already_logged = true;
        break;
      }
    }
    if(clock_trusted && !already_logged && !logged_this_pass &&
       _logged_count < NOTIFICATION_MAX)
    {
      // Argument list copied from EvseManager's own call site
      // (src/evse_man.cpp:386) so the row is shaped like every other row.
      bool written =
        eventLog.log(EventType::Notification, _evse->getState(),
                     _evse->getEvseState(), _evse->getFlags(), _evse->getPilotState(),
                     _evse->getPilot(), _evse->getSessionEnergy(), _evse->getSessionElapsed(),
                     _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR),
                     _evse->getTemperature(EVSE_MONITOR_TEMP_MAX),
                     divert.isActive(), shaper.getState(), _live[i].id);
      // Only a row that actually reached the file counts as logged. log()
      // drops entries silently in two cases that still apply here: the repeat
      // filter (its key carries no advisory id, so a second advisory logged
      // inside the same 300 s window looks like a repeat of the first) and low
      // LittleFS space. Marking the id logged regardless would mean the
      // advisory never got a row on this boot or any later one; leaving it
      // unmarked means the next pass tries again.
      if(written) {
        _logged_ids[_logged_count++] = _live[i].id;
        logged_this_pass = true;
      }
    }
  }

  // Acks for advisories that have gone away are dead weight; drop them so the
  // persisted blob cannot grow across a charger's lifetime.
  //
  // DO NOT remove this gate. Every RAPI read is asynchronous, so on the first
  // pass after boot - which runs within milliseconds of begin() - the monitor
  // has answered nothing: the settings word is 0 (reads as "every check
  // enabled"), the fault counters are 0 and relay health is unknown. The rule
  // table therefore returns an empty list, prune() would find nothing live to
  // keep, and every persisted ack would be erased before the controller had
  // said a word - plus an EEPROM write on every single boot.
  //
  // Both reads are gated, not just $GE: the wear.* and thermal.relay_* rules
  // are evaluated from the $GL relay-health snapshot, which evseBoot() queues
  // separately, so pruning while that is still outstanding would drop exactly
  // those acks. Both flags are cleared in evseBoot(), so a controller reboot -
  // which this hardware does in normal operation - closes this gate again
  // until the new controller has answered both. On a controller with no
  // RELAY_HEALTH support the gate never opens and acks are never pruned; that
  // is deliberate and harmless - the list is bounded at NOTIFICATION_ACK_MAX
  // and stale acks only cost bytes, where an over-eager prune costs the user
  // the mute they deliberately set.
  if(_evse->isSettingsKnown() && _evse->isRelayHealthKnown()) {
    size_t pruned = notification_acks_prune(_acks, _ack_count, _live, _count);
    if(pruned != _ack_count) {
      _ack_count = pruned;
      saveAcks();
    }
  }

  if(changed) {
    pushEvent();
  }

  return EVSE_NOTIFICATIONS_LOOP_TIME;
}

void Notifications::pushEvent()
{
  DynamicJsonDocument doc(1024);
  JsonObject o = doc.createNestedObject("notifications");
  o["count"] = count();
  o["severity"] = notification_severity_name(maxSeverity());
  event_send(doc);
}

size_t Notifications::count()
{
  Notification tmp[NOTIFICATION_MAX];
  return unmuted(tmp);
}

uint8_t Notifications::maxSeverity()
{
  Notification tmp[NOTIFICATION_MAX];
  size_t n = unmuted(tmp);
  return notifications_max_severity(tmp, n);
}

bool Notifications::worst(const char *&id, uint8_t &severity)
{
  Notification tmp[NOTIFICATION_MAX];
  size_t n = unmuted(tmp);
  int w = notifications_worst(tmp, n);
  if(w < 0) {
    return false;
  }
  id = tmp[w].id;
  severity = tmp[w].severity;
  return true;
}

bool Notifications::ack(const char *id)
{
  for(size_t i = 0; i < _count; i++) {
    if(0 == strcmp(_live[i].id, id)) {
      _ack_count = notification_acks_set(_acks, _ack_count, NOTIFICATION_ACK_MAX,
                                         _live[i].key, _live[i].token);
      saveAcks();
      pushEvent();
      return true;
    }
  }
  return false;
}

void Notifications::serialize(JsonDocument &doc)
{
  static const char *category_name[] = { "safety", "fault", "wear", "thermal" };

  doc["count"] = count();
  doc["max_severity"] = notification_severity_name(maxSeverity());

  JsonArray list = doc.createNestedArray("notifications");
  for(size_t i = 0; i < _count; i++) {
    JsonObject o = list.createNestedObject();
    o["id"] = _live[i].id;
    o["category"] = category_name[_live[i].category];
    o["severity"] = notification_severity_name(_live[i].severity);
    o["sticky"] = _live[i].sticky;
    o["acked"] = notification_acks_is_acked(_acks, _ack_count, _live[i].key, _live[i].token);
    o["first_seen"] = _first_seen[i];
    o["last_seen"] = _last_seen[i];
  }
}

const char *notification_severity_name(uint8_t severity)
{
  static const char *severity_name[] = { "info", "warning", "critical" };
  if(severity >= (sizeof(severity_name) / sizeof(severity_name[0]))) {
    return "info";
  }
  return severity_name[severity];
}

const char *notification_short_text(const char *id)
{
  if(0 == strcmp(id, "safety.ground_check"))      return "GROUND CHECK OFF";
  if(0 == strcmp(id, "safety.gfci_check"))        return "GFCI SELF TEST OFF";
  if(0 == strcmp(id, "safety.relay_check"))       return "RELAY CHECK OFF";
  if(0 == strcmp(id, "safety.diode_check"))       return "DIODE CHECK OFF";
  if(0 == strcmp(id, "safety.vent_check"))        return "VENT CHECK OFF";
  if(0 == strcmp(id, "safety.temp_check"))        return "TEMP MONITOR OFF";
  if(0 == strcmp(id, "fault.gfci_tripped"))       return "GFCI HAS TRIPPED";
  if(0 == strcmp(id, "fault.no_ground"))          return "GROUND FAULT LOGGED";
  if(0 == strcmp(id, "fault.stuck_relay"))        return "STUCK RELAY LOGGED";
  if(0 == strcmp(id, "thermal.throttling"))       return "REDUCING CURRENT - HOT";
  if(0 == strcmp(id, "thermal.high_temp"))        return "TEMPERATURE HIGH";
  if(0 == strcmp(id, "thermal.relay_thermal"))    return "RELAY RUNNING HOT";
  if(0 == strcmp(id, "wear.relay_life"))          return "RELAY NEAR END OF LIFE";
  if(0 == strcmp(id, "wear.relay_transit_drift")) return "RELAY SLOWING";
  if(0 == strcmp(id, "wear.relay_cold_open"))     return "RELAY OPENED UNDER LOAD";
  if(0 == strcmp(id, "wear.stuck_relay_recovery"))return "RELAY RECOVERY RUN";
  return "CHECK THE APP";
}
