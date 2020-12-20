#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_EVSE_MAN)
#undef ENABLE_DEBUG
#endif

#include <openevse.h>

#include "evse_man.h"
#include "debug.h"

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

EvseManager::Claim::Claim() :
  _client(EvseClient_NULL),
  _priority(0),
  _properties()
{
}

void EvseManager::Claim::claim(EvseClient client, int priority, EvseProperties &target)
{
  _client = client;
  _priority = priority;
  _properties = target;
}

void EvseManager::Claim::release()
{
  _client = EvseClient_NULL;
}

EvseManager::EvseStateEvent::EvseStateEvent() :
  MicroTasks::Event(),
  _evse_state(OPENEVSE_STATE_STARTING)
{
}

EvseManager::EvseManager(Stream &port) :
  _sender(&port),
  _clients(),
  _state(),
  _evseStateListener(this),
  _targetProperties(),
  _hasClaims(false),
  _sleepForDisable(true),
  _evaluateClaims(false),
  _evaluateTargetState(false),
  _waitingForEvent(0)
{
}

EvseManager::~EvseManager()
{
}

void EvseManager::initialiseEvse()
{
  // Check state the OpenEVSE is in.
  OpenEVSE.begin(_sender, [this](bool connected)
  {
    if(connected)
    {
      OpenEVSE.getStatus([this](int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)
      {
        DBUGVAR(evse_state);
        _state.setState(evse_state, pilot_state);
      });
    } else {
      DBUGLN("OpenEVSE not responding or not connected");
    }
  });
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
  _state.Register(&_evseStateListener);

  OpenEVSE.onState([this](uint8_t evse_state, uint8_t pilot_state, uint32_t current_capacity, uint32_t vflags)
  {
    DBUGVAR(evse_state);
    DBUGVAR(_waitingForEvent);
    if(_waitingForEvent > 0) {
      _evaluateTargetState = true;
      _waitingForEvent--;
    }
    _state.setState(evse_state, pilot_state);
  });
}

bool EvseManager::setTargetState(EvseProperties &target)
{
  bool changeMade = false;
  DBUGVAR(target.getState().toString());
  DBUGVAR(_state.getState().toString());

  EvseState state = target.getState();
  if(EvseState::None != state && state != _state.getState())
  {
    _waitingForEvent++;
    if(EvseState::Active == state)
    {
      DBUGLN("EVSE: enable");
      OpenEVSE.enable([this](int ret) {
        DBUGF("EVSE: enable - complete %d", ret);
      });
    }
    else
    {
      if(_sleepForDisable) {
        DBUGLN("EVSE: sleep");
        OpenEVSE.sleep([this](int ret) {
          DBUGF("EVSE: enable - complete %d", ret);
        });
      } else {
        DBUGLN("EVSE: disable");
        OpenEVSE.disable([this](int ret) {
          DBUGF("EVSE: enable - complete %d", ret);
        });
      }
    }

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

  DBUGVAR(_state.getState().toString());
  DBUGVAR(_state.getEvseState());
  DBUGVAR(_state.getPilotState());

  // If we are not connected yet try and connect to the EVSE module
  if(!OpenEVSE.isConnected())
  {
    initialiseEvse();
    return 10 * 1000;
  }

  DBUGVAR(_evaluateClaims);
  if(_evaluateClaims)
  {
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

    _evaluateClaims = false;
    _evaluateTargetState = true;
  }

  DBUGVAR(_evaluateTargetState);
  if(_evaluateTargetState)
  {
    if(_hasClaims) {
      setTargetState(_targetProperties);
    }
    else
    {
      // No clients, make sure the EVSE module is enabled
      if(_state.isDisabled())
      {
        _waitingForEvent++;
        DBUGLN("EVSE: enable");
        OpenEVSE.enable([this](int ret) {
          DBUGF("EVSE: enable - complete %d", ret);
        });
      }
    }
    _evaluateTargetState = false;
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

  DBUGF("New claim from 0x%08x, priority %d, %s", client, priority, target.getState().toString());

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
    DBUGF("Found slot, waking task");
    slot->claim(client, priority, target);
    _evaluateClaims = true;
    MicroTask.wakeTask(this);
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

bool EvseManager::clientHasClaim(EvseClient client) {
  return findClaim(client);
}

EvseState EvseManager::getState(EvseClient client)
{
  return EvseState::None;
}

uint32_t EvseManager::getChargeCurrent(EvseClient client)
{
  return 0;
}

uint32_t EvseManager::getMaxCurrent(EvseClient client)
{
  return 0;
}

uint32_t EvseManager::getEnergyLimit(EvseClient client)
{
  return 0;
}

uint32_t EvseManager::getTimeLimit(EvseClient client)
{
  return 0;
}
