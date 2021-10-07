#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_EVSE_MAN)
#undef ENABLE_DEBUG
#endif

#include <openevse.h>

#include "evse_man.h"
#include "debug.h"

static EvseProperties nullProperties;

EvseProperties::EvseProperties() :
  _state(EvseState::None),
  _charge_current(UINT32_MAX),
  _max_current(UINT32_MAX),
  _energy_limit(UINT32_MAX),
  _time_limit(UINT32_MAX),
  _auto_release(false)
{
}

EvseProperties::EvseProperties(EvseState state) :
  _state(state),
  _charge_current(UINT32_MAX),
  _max_current(UINT32_MAX),
  _energy_limit(UINT32_MAX),
  _time_limit(UINT32_MAX),
  _auto_release(false)
{
}

void EvseProperties::clear()
{
  _state = EvseState::None;
  _charge_current = UINT32_MAX;
  _max_current = UINT32_MAX;
  _energy_limit = UINT32_MAX;
  _time_limit = UINT32_MAX;
  _auto_release = false;
}

EvseProperties & EvseProperties::operator = (EvseProperties &rhs)
{
  _state = rhs._state;
  _charge_current = rhs._charge_current;
  _max_current = rhs._max_current;
  _energy_limit = rhs._energy_limit;
  _time_limit = rhs._time_limit;
  _auto_release = rhs._auto_release;
  return *this;
}

bool EvseProperties::deserialize(JsonObject &obj)
{
  if(obj.containsKey("state")) {
    _state.fromString(obj["state"]);
  }

  if(obj.containsKey("charge_current")) {
    _charge_current = obj["charge_current"];
  }

  if(obj.containsKey("max_current")) {
    _max_current = obj["max_current"];
  }

  if(obj.containsKey("energy_limit")) {
    _energy_limit = obj["energy_limit"];
  }

  if(obj.containsKey("time_limit")) {
    _time_limit = obj["time_limit"];
  }

  if(obj.containsKey("auto_release")) {
    _auto_release = obj["auto_release"];
  }

  return true;
}

bool EvseProperties::serialize(JsonObject &obj)
{
  if(EvseState::None != _state) {
    obj["state"] = _state.toString();
  }
  if(UINT32_MAX != _charge_current) {
    obj["charge_current"] = _charge_current;
  }
  if(UINT32_MAX != _max_current) {
    obj["max_current"] = _max_current;
  }
  if(UINT32_MAX != _energy_limit) {
    obj["energy_limit"] = _energy_limit;
  }
  if(UINT32_MAX != _time_limit) {
    obj["time_limit"] = _time_limit;
  }

  obj["auto_release"] = _auto_release;

  return true;
}

EvseManager::Claim::Claim() :
  _client(EvseClient_NULL),
  _priority(0),
  _properties()
{
}

bool EvseManager::Claim::claim(EvseClient client, int priority, EvseProperties &target)
{
  if(_client != client ||
     _priority != priority ||
     _properties != target)
  {
    _client = client;
    _priority = priority;
    _properties = target;
    return true;
  }

  return false;
}

void EvseManager::Claim::release()
{
  _client = EvseClient_NULL;
}

EvseManager::EvseManager(Stream &port) :
  MicroTasks::Task(),
  _sender(&port),
  _monitor(OpenEVSE),
  _clients(),
  _evseStateListener(this),
  _sessionCompleteListener(this),
  _targetProperties(EvseState::Active),
  _hasClaims(false),
  _sleepForDisable(true),
  _evaluateClaims(true),
  _evaluateTargetState(false),
  _waitingForEvent(0),
  _vehicleValid(0),
  _vehicleUpdated(0),
  _vehicleLastUpdated(0),
  _vehicleStateOfCharge(0),
  _vehicleRange(0),
  _vehicleEta(0)
{
}

EvseManager::~EvseManager()
{
}

void EvseManager::initialiseEvse()
{
  // Check state the OpenEVSE is in.
  _monitor.begin(_sender);
}

bool EvseManager::findClaim(EvseClient client, Claim **claim)
{
  for(size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    if(_clients[i].isValid() && _clients[i] == client)
    {
      if(claim) {
        *claim = &(_clients[i]);
      }
      return true;
    }
  }

  return false;
}

bool EvseManager::evaluateClaims(EvseProperties &properties)
{
  properties.clear();

  bool foundClaim = false;

  int statePriority = 0;
  int chargeCurrentPriority = 0;
  int maxCurrentPriority = 0;
  int energyLimitPriority = 0;
  int timeLimitPriority = 0;

  for(size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    if(_clients[i].isValid())
    {
      foundClaim = true;

      DBUGF("Found client, slot %d", i);
      EvseManager::Claim &claim = _clients[i];

      DBUGVAR(claim.getPriority());
      DBUGVAR(claim.getState().toString());
      DBUGVAR(claim.getChargeCurrent());
      DBUGVAR(claim.getMaxCurrent());
      DBUGVAR(claim.getEnergyLimit());
      DBUGVAR(claim.getTimeLimit());

      if(claim.getPriority() > statePriority &&
         claim.getState() != EvseState::None)
      {
        properties.setState(claim.getState());
        statePriority = claim.getPriority();
      }

      if(claim.getPriority() > chargeCurrentPriority &&
         claim.getChargeCurrent() != UINT32_MAX)
      {
        properties.setChargeCurrent(claim.getChargeCurrent());
        chargeCurrentPriority = claim.getPriority();
      }

      if(claim.getPriority() > maxCurrentPriority &&
         claim.getMaxCurrent() != UINT32_MAX)
      {
        properties.setMaxCurrent(claim.getMaxCurrent());
        maxCurrentPriority = claim.getPriority();
      }

      if(claim.getPriority() > energyLimitPriority &&
         claim.getEnergyLimit() != UINT32_MAX)
      {
        properties.setEnergyLimit(claim.getEnergyLimit());
        energyLimitPriority = claim.getPriority();
      }

      if(claim.getPriority() > timeLimitPriority &&
         claim.getTimeLimit() != UINT32_MAX)
      {
        properties.setTimeLimit(claim.getTimeLimit());
        timeLimitPriority = claim.getPriority();
      }
    }
  }

  return foundClaim;
}

void EvseManager::setup()
{
  _monitor.onStateChange(&_evseStateListener);
  _monitor.onSessionComplete(&_sessionCompleteListener);
}

bool EvseManager::setTargetState(EvseProperties &target)
{
  bool changeMade = false;
  DBUGVAR(target.getState().toString());
  DBUGVAR(getActiveState().toString());

  EvseState state = target.getState();
  if(EvseState::None != state && state != getActiveState())
  {
    _waitingForEvent++;
    if(EvseState::Active == state)
    {
      DBUGLN("EVSE: enable");
      _monitor.enable();
    }
    else
    {
      if(_sleepForDisable) {
        DBUGLN("EVSE: sleep");
        _monitor.sleep();
      } else {
        DBUGLN("EVSE: disable");
        _monitor.disable();
      }
    }

    changeMade = true;
  }

  // Work out what the max current should be
  uint32_t charge_current = _monitor.getMaxConfiguredCurrent();
  DBUGVAR(charge_current);
  if(UINT32_MAX != target.getMaxCurrent() && target.getMaxCurrent() < charge_current) {
    charge_current = target.getMaxCurrent();
  }
  DBUGVAR(charge_current);

  // Work out the charge current
  if(UINT32_MAX != target.getChargeCurrent() && target.getChargeCurrent() < charge_current) {
    charge_current = target.getChargeCurrent();
  }
  DBUGVAR(charge_current);

  if(charge_current < _monitor.getMinCurrent()) {
    charge_current = _monitor.getMinCurrent();
  }
  DBUGVAR(charge_current);

  if(charge_current != _monitor.getPilot())
  {
    DBUGF("Set pilot to %d", charge_current);
    _monitor.setPilot(charge_current);
    changeMade = true;
  }

  return changeMade;
}

unsigned long EvseManager::loop(MicroTasks::WakeReason reason)
{
  DBUG("EVSE manager woke: ");
  DBUG(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");
  DBUG(" connected: ");
  DBUGLN(OpenEVSE.isConnected());

  DBUGVAR(getActiveState().toString());
  DBUGVAR(_monitor.getEvseState());
  DBUGVAR(_monitor.getPilotState());

  // If we are not connected yet try and connect to the EVSE module
  if(!OpenEVSE.isConnected())
  {
    initialiseEvse();
    return 10 * 1000;
  }

  DBUGVAR(_evseStateListener.IsTriggered());
  if(_evseStateListener.IsTriggered())
  {
    DBUGVAR(_waitingForEvent);
    if(_waitingForEvent > 0) {
      _evaluateTargetState = true;
      _waitingForEvent--;
    }
  }

  DBUGVAR(_sessionCompleteListener.IsTriggered());
  if(_sessionCompleteListener.IsTriggered())
  {
    // Session complete, clear any auto release claims
    releaseAutoReleaseClaims();
  }

  DBUGVAR(_evaluateClaims);
  if(_evaluateClaims)
  {
    _evaluateClaims = false;

    // Work out the state we should try and get in too
    _hasClaims = evaluateClaims(_targetProperties);

    DBUGVAR(_hasClaims);
    if(_hasClaims)
    {
      DBUGVAR(_targetProperties.getState().toString());
      DBUGVAR(_targetProperties.getChargeCurrent());
      DBUGVAR(_targetProperties.getMaxCurrent());
      DBUGVAR(_targetProperties.getEnergyLimit());
      DBUGVAR(_targetProperties.getTimeLimit());
    } else {
      DBUGLN("No claims");
    }

    _evaluateTargetState = true;
  }

  DBUGVAR(_evaluateTargetState);
  if(_evaluateTargetState)
  {
    _evaluateTargetState = false;

    if(!_hasClaims)
    {
      // No claims, make sure the targetProperties are the defaults with charger active
      _targetProperties.clear();
      _targetProperties.setState(EvseState::Active);
    }
    setTargetState(_targetProperties);
  }

  return MicroTask.Infinate;
}

bool EvseManager::begin()
{
  MicroTask.startTask(this);

  return true;
}

bool EvseManager::claim(EvseClient client, int priority, EvseProperties &target)
{
  Claim *slot = NULL;

  DBUGF("Claim from 0x%08x, priority %d, %s", client, priority, target.getState().toString());

  for (size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    DBUGF("Slot %d %s %p", i, _clients[i].isValid() ? "valid" : "free", slot);
    if(_clients[i] == client) {
      slot = &(_clients[i]);
      break;
    } else if (NULL == slot && !_clients[i].isValid()) {
      slot = &(_clients[i]);
    }
  }

  if(slot)
  {
    DBUGF("Found slot");
    if(slot->claim(client, priority, target))
    {
      DBUGF("Claim added/updated, waking task");
      _evaluateClaims = true;
      MicroTask.wakeTask(this);
    }
    return true;
  }

  return false;
}

bool EvseManager::release(EvseClient client)
{
  Claim *claim;

  if(findClaim(client, &claim))
  {
    claim->release();
    _evaluateClaims = true;
    MicroTask.wakeTask(this);
    return true;
  }

  return false;
}

void EvseManager::releaseAutoReleaseClaims()
{
  for (size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    if(_clients[i].isValid() && _clients[i].isAutoRelease())
    {
      DBUGF("Release claim from 0x%08x, priority %d, %s", _clients[i].getClient(), _clients[i].getPriority(), _clients[i].getState().toString());
      _clients[i].release();
      _evaluateClaims = true;
    }
  }
}

bool EvseManager::clientHasClaim(EvseClient client) {
  return findClaim(client);
}

EvseProperties &EvseManager::getClaimProperties(EvseClient client)
{
  if(EvseClient_NULL == client) {
    return _targetProperties;
  }

  Claim *claim;
  if(findClaim(client, &claim)) {
    return claim->getProperties();
  }

  return nullProperties;
}

EvseState EvseManager::getState(EvseClient client)
{
  return getClaimProperties(client).getState();
}

uint8_t EvseManager::getStateColour()
{
  switch(getEvseState())
  {
    case OPENEVSE_STATE_STARTING:
      // Do nothing
      break;
    case OPENEVSE_STATE_NOT_CONNECTED:
      return OPENEVSE_LCD_GREEN;

    case OPENEVSE_STATE_CONNECTED:
      return OPENEVSE_LCD_YELLOW;

    case OPENEVSE_STATE_CHARGING:
      // TODO: Colour should also take into account the tempurature, >60 YELLOW
      return OPENEVSE_LCD_TEAL;

    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:
      return OPENEVSE_LCD_RED;

    case OPENEVSE_STATE_SLEEPING:
    case OPENEVSE_STATE_DISABLED:
      return isVehicleConnected() ? OPENEVSE_LCD_TEAL : OPENEVSE_LCD_VIOLET;
      break;
  }

  return OPENEVSE_LCD_OFF;
}

uint32_t EvseManager::getChargeCurrent(EvseClient client)
{
  if(EvseClient_NULL == client) {
    return _monitor.getPilot();
  }

  return getClaimProperties(client).getChargeCurrent();
}

uint32_t EvseManager::getMaxCurrent(EvseClient client)
{
  if(EvseClient_NULL == client) {
    return _monitor.getMaxConfiguredCurrent();
  }

  return getClaimProperties(client).getMaxCurrent();
}

uint32_t EvseManager::getEnergyLimit(EvseClient client)
{
  return getClaimProperties(client).getEnergyLimit();
}

uint32_t EvseManager::getTimeLimit(EvseClient client)
{
  return getClaimProperties(client).getTimeLimit();
}

void EvseManager::setVehicleStateOfCharge(int vehicleStateOfCharge)
{
  _vehicleStateOfCharge = vehicleStateOfCharge;
  _vehicleValid |= EVSE_VEHICLE_SOC;
  _vehicleUpdated |= EVSE_VEHICLE_SOC;
  _vehicleLastUpdated = millis();
  MicroTask.wakeTask(this);
}

void EvseManager::setVehicleRange(int vehicleRange)
{
  _vehicleRange = vehicleRange;
  _vehicleValid |= EVSE_VEHICLE_RANGE;
  _vehicleUpdated |= EVSE_VEHICLE_RANGE;
  _vehicleLastUpdated = millis();
  MicroTask.wakeTask(this);
}

void EvseManager::setVehicleEta(int vehicleEta)
{
  _vehicleEta = vehicleEta;
  _vehicleValid |= EVSE_VEHICLE_ETA;
  _vehicleUpdated |= EVSE_VEHICLE_ETA;
  _vehicleLastUpdated = millis();
  MicroTask.wakeTask(this);
}

bool EvseManager::isRapiCommandBlocked(String rapi)
{
  return rapi.startsWith("$ST");
}

bool EvseManager::serializeClaims(DynamicJsonDocument &doc)
{
  doc.to<JsonArray>();

  for(size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    Claim &claim = _clients[i];
    if(claim.isValid())
    {
      JsonObject obj = doc.createNestedObject();
      obj["client"] = claim.getClient();
      obj["priority"] = claim.getPriority();
      claim.getProperties().serialize(obj);
    }
  }

  return true;
}

bool EvseManager::serializeClaim(DynamicJsonDocument &doc, EvseClient client)
{
  Claim *claim;

  if(findClaim(client, &claim))
  {
    doc["priority"] = claim->getPriority();
    claim->getProperties().serialize(doc);
    return true;
  }

  return false;
}
