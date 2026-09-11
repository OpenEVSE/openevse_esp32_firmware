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

  uint32_t now = (uint32_t)(millis() / 1000);
  bool changed = (_count != previous_count);

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

      // Raise edge: one event-log row, at most once per advisory id per boot.
      // Keyed on id (not array index) so clearing an earlier advisory can
      // never make a later one slide into a logged slot and re-log.
      bool already_logged = false;
      for(size_t l = 0; l < _logged_count; l++) {
        if(0 == strcmp(_logged_ids[l], _live[i].id)) {
          already_logged = true;
          break;
        }
      }
      if(!already_logged && _logged_count < NOTIFICATION_MAX) {
        _logged_ids[_logged_count++] = _live[i].id;
        // Argument list copied from EvseManager's own call site
        // (src/evse_man.cpp:386) so the row is shaped like every other row.
        eventLog.log(EventType::Notification, _evse->getState(),
                     _evse->getEvseState(), _evse->getFlags(), _evse->getPilotState(),
                     _evse->getPilot(), _evse->getSessionEnergy(), _evse->getSessionElapsed(),
                     _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR),
                     _evse->getTemperature(EVSE_MONITOR_TEMP_MAX),
                     divert.isActive(), shaper.getState());
      }
    }
  }

  // Acks for advisories that have gone away are dead weight; drop them so the
  // persisted blob cannot grow across a charger's lifetime.
  size_t pruned = notification_acks_prune(_acks, _ack_count, _live, _count);
  if(pruned != _ack_count) {
    _ack_count = pruned;
    saveAcks();
  }

  if(changed) {
    DynamicJsonDocument doc(1024);
    JsonObject o = doc.createNestedObject("notifications");
    o["count"] = count();
    o["severity"] = maxSeverity();
    event_send(doc);
  }

  return EVSE_NOTIFICATIONS_LOOP_TIME;
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
      return true;
    }
  }
  return false;
}

void Notifications::serialize(JsonDocument &doc)
{
  static const char *severity_name[] = { "info", "warning", "critical" };
  static const char *category_name[] = { "safety", "fault", "wear", "thermal" };

  doc["count"] = count();
  doc["max_severity"] = severity_name[maxSeverity()];

  JsonArray list = doc.createNestedArray("notifications");
  for(size_t i = 0; i < _count; i++) {
    JsonObject o = list.createNestedObject();
    o["id"] = _live[i].id;
    o["category"] = category_name[_live[i].category];
    o["severity"] = severity_name[_live[i].severity];
    o["sticky"] = _live[i].sticky;
    o["acked"] = notification_acks_is_acked(_acks, _ack_count, _live[i].key, _live[i].token);
    o["first_seen"] = _first_seen[i];
    o["last_seen"] = _last_seen[i];
  }
}
