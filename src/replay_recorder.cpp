#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_REPLAY)
#undef ENABLE_DEBUG
#endif

#include "replay_recorder.h"

#if ENABLE_REPLAY_RECORDER

#include <ArduinoJson.h>
#include <time.h>

#include "debug.h"
#include "app_config.h"
#include "emonesp.h"
#include "divert.h"
#include "current_shaper.h"
#include "limit.h"
#include "boost.h"
#include "rfid.h"
#include "scheduler.h"
#include "replay_redact.h"

// Same floor the energy tsdb uses before it trusts the wall clock.
#define REPLAY_TIME_VALID_FLOOR 1700000000UL

ReplayRecorder replayRecorder;

static uint8_t replayState(const char *state)
{
  if(!state) return REPLAY_STATE_NONE;
  if(0 == strcmp(state, "active")) return REPLAY_STATE_ACTIVE;
  if(0 == strcmp(state, "disabled")) return REPLAY_STATE_DISABLED;
  return REPLAY_STATE_NONE;
}

void ReplayRecorder::begin(EvseManager &evse, DivertTask &divert, CurrentShaperTask &shaper,
                           Limit &limit, Boost &boost, RfidTask &rfid, Scheduler &scheduler)
{
  _evse = &evse;
  _divert = &divert;
  _shaper = &shaper;
  _limit = &limit;
  _boost = &boost;
  _rfid = &rfid;
  _scheduler = &scheduler;

  if(!_samples.begin(REPLAY_SAMPLES) || !_events.begin(REPLAY_EVENTS)) {
    DBUGLN("ReplayRecorder: no memory, recording disabled");
    return;
  }
  MicroTask.startTask(this);
}

void ReplayRecorder::setup()
{
  _last_millis = millis();
  pushEvent(ReplayEventType::Boot);
}

unsigned long ReplayRecorder::loop(MicroTasks::WakeReason reason)
{
  unsigned long now = millis();
  _uptime_ms += (unsigned long)(now - _last_millis);
  _last_millis = now;

  pollChanges();

  if(_uptime_ms >= _next_sample_ms) {
    takeSample();
    _next_sample_ms = _uptime_ms + REPLAY_SAMPLE_INTERVAL_MS;
  }
  return 1000;
}

size_t ReplayRecorder::readClaims(ReplayClaim *out, size_t max)
{
  DynamicJsonDocument doc(JSON_ARRAY_SIZE(EVSE_MANAGER_MAX_CLIENT_CLAIMS) +
                          EVSE_MANAGER_MAX_CLIENT_CLAIMS * JSON_OBJECT_SIZE(6) + 256);
  _evse->serializeClaims(doc);
  size_t n = 0;
  for(JsonObjectConst c : doc.as<JsonArrayConst>()) {
    if(n >= max) break;
    ReplayClaim &r = out[n++];
    r.client = c["client"] | 0U;
    r.priority = c["priority"] | 0;
    r.state = replayState(c["state"] | "");
    r.charge_current = c.containsKey("charge_current") ? (int16_t)(c["charge_current"] | 0) : -1;
    r.max_current = c.containsKey("max_current") ? (int16_t)(c["max_current"] | 0) : -1;
    r.auto_release = (c["auto_release"] | false) ? 1 : 0;
  }
  return n;
}

void ReplayRecorder::pushEvent(ReplayEventType type, const ReplayClaim *claim,
                               uint8_t state, uint32_t value, bool auto_release)
{
  ReplayEvent e = {};
  e.up = uptime();
  e.type = (uint8_t)type;
  e.state = state;
  e.value = value;
  e.auto_release = auto_release ? 1 : 0;
  e.charge_current = -1;
  e.max_current = -1;
  if(claim) {
    e.client = claim->client;
    e.priority = claim->priority;
    e.state = claim->state;
    e.charge_current = claim->charge_current;
    e.max_current = claim->max_current;
    e.auto_release = claim->auto_release;
  }
  _events.push(e);
}

void ReplayRecorder::recordLimit()
{
  LimitProperties props = _limit->get();
  pushEvent(ReplayEventType::Limit, nullptr, (uint8_t)props.getType(),
            props.getValue(), props.getAutoRelease());
}

void ReplayRecorder::recordBoost()
{
  StaticJsonDocument<192> doc;
  _boost->serialize(doc);
  LimitType type = LimitType::None;
  type.fromString(doc["type"] | "none");
  pushEvent(ReplayEventType::Boost, nullptr, (uint8_t)type, doc["value"] | 0U);
}

void ReplayRecorder::pollChanges()
{
  uint8_t claims_version = _evse->getClaimsVersion();
  if(!_primed || claims_version != _claims_version) {
    ReplayClaim cur[EVSE_MANAGER_MAX_CLIENT_CLAIMS];
    size_t cur_count = readClaims(cur, EVSE_MANAGER_MAX_CLIENT_CLAIMS);
    replay_diff_claims(_claims, _claim_count, cur, cur_count,
      [this](const ReplayClaim &c) { pushEvent(ReplayEventType::Claim, &c); },
      [this](const ReplayClaim &c) { pushEvent(ReplayEventType::Release, &c); });
    memcpy(_claims, cur, sizeof(cur[0]) * cur_count);
    _claim_count = cur_count;
    _claims_version = claims_version;
  }

  bool auth = _rfid->getAuthenticatedTag().length() > 0;
  if(auth != _rfid_auth) {
    pushEvent(auth ? ReplayEventType::RfidAuth : ReplayEventType::RfidDeauth);
    _rfid_auth = auth;
  }

  if(!_primed || _limit->getVersion() != _limit_version) {
    _limit_version = _limit->getVersion();
    recordLimit();
  }
  if(!_primed || _boost->getVersion() != _boost_version) {
    _boost_version = _boost->getVersion();
    if(_primed || _boost->isActive()) {
      recordBoost();
    }
  }
  if(_primed && _scheduler->getVersion() != _schedule_version) {
    pushEvent(ReplayEventType::Schedule);
  }
  _schedule_version = _scheduler->getVersion();
  if(_primed && config_version() != _config_version) {
    pushEvent(ReplayEventType::Config);
  }
  _config_version = config_version();

  _primed = true;
}

void ReplayRecorder::takeSample()
{
  uint8_t flags = 0;
  if(_evse->isVehicleConnected()) flags |= REPLAY_FLAG_VEHICLE;
  if(_evse->isCharging()) flags |= REPLAY_FLAG_CHARGING;
  if(EvseState::Active == _evse->getState()) flags |= REPLAY_FLAG_TARGET_ACTIVE;
  if(DivertMode::Eco == _divert->getMode()) flags |= REPLAY_FLAG_ECO;
  if(_shaper->getState()) flags |= REPLAY_FLAG_SHAPER;
  if(_rfid_auth) flags |= REPLAY_FLAG_RFID_AUTH;

  int soc = _evse->isVehicleStateOfChargeValid() ? _evse->getVehicleStateOfCharge()
                                                 : REPLAY_SOC_INVALID;
  ReplaySample s;
  replay_sample_set(s, uptime(),
                    _divert->getSolar(), _divert->getGridIe(), _shaper->getLivePwr(),
                    _evse->getVoltage(), _evse->getAmps(),
                    _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR),
                    _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR),
                    _evse->getSessionEnergy(), _evse->getPilot(), _evse->getEvseState(),
                    soc, _evse->getChargeCurrent(), flags);
  _samples.push(s);
}

// Rough serialized sizes, used to fit a package into a byte budget.
#define REPLAY_SAMPLE_JSON_BYTES  64
#define REPLAY_EVENT_JSON_BYTES   128
#define REPLAY_FIXED_JSON_BYTES   4096

void ReplayRecorder::writeSamples(Print &out, size_t max_samples)
{
  size_t count = _samples.size();
  size_t skip = count > max_samples ? count - max_samples : 0;
  out.print(F("\"samples\":{\"interval\":"));
  out.print(REPLAY_SAMPLE_INTERVAL_MS / 1000);
  out.print(F(",\"columns\":["));
  for(size_t i = 0; i < REPLAY_SAMPLE_COLUMN_COUNT; i++) {
    if(i) out.print(',');
    out.print('"');
    out.print(REPLAY_SAMPLE_COLUMNS[i]);
    out.print('"');
  }
  out.print(F("],\"overwritten\":"));
  out.print(_samples.overwritten());
  out.print(F(",\"truncated\":"));
  out.print(skip);
  out.print(F(",\"data\":["));
  char row[160];
  bool first = true;
  for(size_t i = skip; i < count; i++) {
    if(replay_sample_row(_samples.at(i), row, sizeof(row))) {
      if(!first) out.print(',');
      out.print(row);
      first = false;
    }
  }
  out.print(F("]}"));
}

void ReplayRecorder::writeEvents(Print &out, size_t max_events)
{
  size_t count = _events.size();
  size_t skip = count > max_events ? count - max_events : 0;
  out.print(F("\"events\":{\"overwritten\":"));
  out.print(_events.overwritten());
  out.print(F(",\"truncated\":"));
  out.print(skip);
  out.print(F(",\"data\":["));
  for(size_t i = skip; i < count; i++) {
    const ReplayEvent &e = _events.at(i);
    StaticJsonDocument<256> doc;
    doc["up"] = e.up;
    doc["type"] = replay_event_type_name(e.type);
    switch((ReplayEventType)e.type) {
      case ReplayEventType::Claim:
      case ReplayEventType::Release:
        doc["client"] = e.client;
        doc["priority"] = e.priority;
        if(ReplayEventType::Claim == (ReplayEventType)e.type) {
          if(REPLAY_STATE_NONE != e.state) doc["state"] = replay_state_name(e.state);
          if(e.charge_current >= 0) doc["charge_current"] = e.charge_current;
          if(e.max_current >= 0) doc["max_current"] = e.max_current;
          doc["auto_release"] = (bool)e.auto_release;
        }
        break;
      case ReplayEventType::Limit:
      case ReplayEventType::Boost: {
        LimitType type((LimitType::Value)e.state);
        doc["limit_type"] = type.toString();
        doc["value"] = e.value;
        if(ReplayEventType::Limit == (ReplayEventType)e.type) {
          doc["auto_release"] = (bool)e.auto_release;
        }
      } break;
      default:
        break;
    }
    if(i > skip) out.print(',');
    serializeJson(doc, out);
  }
  out.print(F("]}"));
}

void ReplayRecorder::serialize(Print &out, size_t budget)
{
  // Split what is left after the fixed sections between events (claims are
  // what a replay can least do without) and samples, newest first.
  size_t schedule_bytes = _scheduler->scheduleJsonCapacity() / 2;
  size_t fixed = REPLAY_FIXED_JSON_BYTES + schedule_bytes;
  size_t room = budget > fixed ? budget - fixed : 0;
  size_t max_events = room / 2 / REPLAY_EVENT_JSON_BYTES;
  if(max_events > _events.size()) max_events = _events.size();
  size_t max_samples = (room - max_events * REPLAY_EVENT_JSON_BYTES) / REPLAY_SAMPLE_JSON_BYTES;

  time_t now = time(NULL);
  bool time_valid = now > (time_t)REPLAY_TIME_VALID_FLOOR;

  {
    StaticJsonDocument<768> meta;
    meta["format"] = "openevse-replay";
    meta["version"] = REPLAY_FORMAT_VERSION;
    meta["firmware"] = currentfirmware;
    meta["buildenv"] = buildenv;
    meta["evse_firmware"] = _evse->getFirmwareVersion();
    meta["uptime"] = uptime();
    if(time_valid) {
      meta["generated"] = (uint32_t)now;
      meta["boot_epoch"] = (uint32_t)(now - uptime());
    } else {
      meta["generated"] = nullptr;
      meta["boot_epoch"] = nullptr;
    }
    meta["time_zone"] = time_zone;
    JsonObject hw = meta.createNestedObject("hardware");
    hw["min_current"] = _evse->getMinCurrent();
    hw["max_current"] = _evse->getMaxHardwareCurrent();
    hw["max_configured"] = _evse->getMaxConfiguredCurrent();
    hw["voltage"] = _evse->getVoltage();
    // Re-open the object so the large sections can be streamed after it.
    String head;
    serializeJson(meta, head);
    head.remove(head.length() - 1);
    out.print(head);
  }

  {
    DynamicJsonDocument full(JSON_OBJECT_SIZE(128) + 1024);
    config_serialize(full, true, false, true);
    DynamicJsonDocument redacted(JSON_OBJECT_SIZE(64) + 512);
    replay_redact_config(full.as<JsonObjectConst>(), redacted.to<JsonObject>());
    out.print(F(",\"config\":"));
    serializeJson(redacted, out);
  }

  {
    DynamicJsonDocument schedule(_scheduler->scheduleJsonCapacity());
    _scheduler->serialize(schedule);
    out.print(F(",\"schedule\":"));
    serializeJson(schedule, out);
  }

  {
    DynamicJsonDocument state(2048);
    JsonObject limit = state.createNestedObject("limit");
    LimitProperties props = _limit->get();
    props.serialize(limit);
    DynamicJsonDocument boost(256);
    _boost->serialize(boost);
    state["boost"] = boost.as<JsonObjectConst>();
    DynamicJsonDocument claims(1024);
    _evse->serializeClaims(claims);
    state["claims"] = claims.as<JsonArrayConst>();
    DynamicJsonDocument target(512);
    _evse->serializeTarget(target);
    state["target"] = target.as<JsonObjectConst>();
    out.print(F(",\"state\":"));
    serializeJson(state, out);
  }

  out.print(',');
  writeSamples(out, max_samples);
  out.print(',');
  writeEvents(out, max_events);
  out.print('}');
}

#endif // ENABLE_REPLAY_RECORDER
