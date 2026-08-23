#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_BOOST)
#undef ENABLE_DEBUG
#endif

#include <time.h>

#include "boost.h"
#include "charge_threshold.h"
#include "debug.h"
#include "event.h"

Boost boost;

Boost::Boost() :
  MicroTasks::Task(),
  _sessionCompleteListener(this)
{
}

Boost::~Boost()
{
  // The global (and the simulator's per-peer instances) may be destroyed
  // without begin() ever running.
  if(_evse && _evse->clientHasClaim(EvseClient_OpenEVSE_Boost)) {
    _evse->release(EvseClient_OpenEVSE_Boost);
  }
}

void Boost::setup()
{
}

void Boost::begin(EvseManager &evse)
{
  DBUGLN("Starting Boost task");
  _evse = &evse;
  MicroTask.startTask(this);
  _evse->onSessionComplete(&_sessionCompleteListener);
}

void Boost::sendEvent(bool active, const char *reason)
{
  StaticJsonDocument<96> doc;
  doc["boost"] = active;
  doc["boost_version"] = _version;
  if(reason) {
    doc["boost_reason"] = reason;
  }
  event_send(doc);
}

int Boost::arm(LimitType type, uint32_t value)
{
  // Whitelist the dimensions we can actually evaluate. A bare
  // `None` check is not enough: LimitType's default ctor leaves the value
  // indeterminate and LimitType::fromString() has no default arm, so an
  // unrecognised string can yield a garbage code >= 5. Arming on one of those
  // would take the priority-200 Active claim with a type the tick's switch
  // never matches, and the boost would never end on its own.
  switch(type) {
    case LimitType::Time:
    case LimitType::Energy:
    case LimitType::Soc:
    case LimitType::Range:
      break;
    default:
      return Boost_BadRequest;
  }
  if(0 == value) {
    return Boost_BadRequest;
  }
  if((LimitType::Soc == type && !_evse->isVehicleStateOfChargeValid()) ||
     (LimitType::Range == type && !_evse->isVehicleRangeValid()))
  {
    return Boost_Unsupported;
  }

  bool replaced = isActive();

  _type = type;
  _value = value;
  _started = time(NULL);
  _deadline_ms = 0;
  _energy_basis_wh = 0;

  if(LimitType::Time == type) {
    if(_value > BOOST_MAX_TIME_S) {
      _value = BOOST_MAX_TIME_S;
    }
    _deadline_ms = millis() + _value * 1000UL;
    if(0 == _deadline_ms) {
      _deadline_ms = 1; // 0 means "no deadline" to the timer math
    }
  } else if(LimitType::Energy == type) {
    _energy_basis_wh = (uint32_t)_evse->getSessionEnergy();
  }

  EvseProperties props(EvseState::Active);
  props.setAutoRelease(true);
  _evse->claim(EvseClient_OpenEVSE_Boost, EvseManager_Priority_Boost, props);

  _version++;
  sendEvent(true, replaced ? "replaced" : NULL);

  // Wake the task so an already-met absolute target releases immediately.
  MicroTask.wakeTask(this);

  DBUGF("Boost armed: %s %u", _type.toString(), _value);
  return Boost_Armed;
}

int Boost::arm(const char *json)
{
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, json);
  if(err || !doc.containsKey("type") || !doc.containsKey("value")) {
    return Boost_BadRequest;
  }
  // "type" must be a string: JSON null, a number or an object all satisfy
  // containsKey() but as<const char *>() then hands back NULL, and
  // LimitType::fromString() dereferences value[0] unguarded.
  if(!doc["type"].is<const char *>()) {
    return Boost_BadRequest;
  }
  const char *type_str = doc["type"].as<const char *>();
  if(NULL == type_str) {
    return Boost_BadRequest;
  }
  LimitType type = LimitType::None;   // fromString() may not assign
  type.fromString(type_str);
  return arm(type, doc["value"].as<uint32_t>());
}

void Boost::endBoost(const char *reason)
{
  if(_evse->clientHasClaim(EvseClient_OpenEVSE_Boost)) {
    _evse->release(EvseClient_OpenEVSE_Boost);
  }
  _type = LimitType::None;
  _value = 0;
  _deadline_ms = 0;
  _energy_basis_wh = 0;
  _started = 0;
  _version++;
  sendEvent(false, reason);
}

bool Boost::cancel()
{
  if(!isActive()) {
    return false;
  }
  DBUGLN("Boost cancelled");
  endBoost("cancelled");
  return true;
}

uint32_t Boost::getRemaining()
{
  switch(_type) {
    case LimitType::Time:
      return deadline_timer_remaining_s(_deadline_ms, millis());
    case LimitType::Energy:
    {
      // Mirror the tick's session-reset heuristic (read-only: the tick owns
      // the re-snapshot) so a report taken between the reset and the next
      // tick does not overstate what is left.
      uint32_t session_wh = (uint32_t)_evse->getSessionEnergy();
      uint32_t basis = session_wh < _energy_basis_wh ? session_wh : _energy_basis_wh;
      return ChargeThreshold::remaining(_type, _value, basis, session_wh);
    }
    case LimitType::Soc:
      return ChargeThreshold::remaining(_type, _value, 0,
                                        (uint32_t)_evse->getVehicleStateOfCharge());
    case LimitType::Range:
      return ChargeThreshold::remaining(_type, _value, 0,
                                        (uint32_t)_evse->getVehicleRange());
    default:
      return 0;
  }
}

void Boost::serialize(JsonDocument &doc)
{
  doc["type"] = _type.toString();
  doc["value"] = _value;
  doc["remaining"] = getRemaining();
  char buf[24];
  struct tm tm;
  gmtime_r(&_started, &tm);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  doc["started"] = buf;
}

unsigned long Boost::loop(MicroTasks::WakeReason reason)
{
  if(_sessionCompleteListener.IsTriggered() && isActive()) {
    // Vehicle gone: the boost's session is over. The Active claim has
    // auto-release set so EvseManager already dropped it; clear our state.
    DBUGLN("Session complete, ending boost");
    endBoost("cancelled");
  }

  if(!isActive()) {
    return MicroTask.Infinate;
  }

  bool reached = false;
  switch(_type) {
    case LimitType::Time:
      reached = deadline_timer_expired(_deadline_ms, millis());
      break;
    case LimitType::Energy:
    {
      // Session energy is monotonic within a session and resets to 0 when the
      // next session starts. A boost armed while unplugged snapshots the old
      // session's total, so once the vehicle plugs in the basis is stale and
      // the target would be met only after basis + target Wh. A decrease can
      // never happen inside one session, so treat it as "new session" and
      // re-snapshot the basis.
      uint32_t session_wh = (uint32_t)_evse->getSessionEnergy();
      if(session_wh < _energy_basis_wh) {
        DBUGF("Session energy reset (%u < %u), re-basing boost",
              session_wh, _energy_basis_wh);
        _energy_basis_wh = session_wh;
      }
      reached = ChargeThreshold::reached(_type, _value, _energy_basis_wh,
                                         session_wh);
      break;
    }
    case LimitType::Soc:
      reached = ChargeThreshold::reached(_type, _value, 0,
                                         (uint32_t)_evse->getVehicleStateOfCharge());
      break;
    case LimitType::Range:
      reached = ChargeThreshold::reached(_type, _value, 0,
                                         (uint32_t)_evse->getVehicleRange());
      break;
    default:
      break;
  }

  if(reached) {
    DBUGLN("Boost target reached, releasing");
    endBoost("reached");
    return MicroTask.Infinate;
  }

  return EVSE_BOOST_LOOP_TIME;
}
