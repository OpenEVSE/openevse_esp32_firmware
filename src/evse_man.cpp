#include <openevse.h>

#include "evse_man.h"
#include "debug.h"

EvseProperties::EvseProperties() :
  _state(EvseState_NULL),
  _charge_current(UINT32_MAX),
  _max_current(UINT32_MAX),
  _energy_limit(UINT32_MAX),
  _time_limit(UINT32_MAX)
{
}

EvseProperties::EvseProperties(EvseState state) :
  _state(state),
  _charge_current(UINT32_MAX),
  _max_current(UINT32_MAX),
  _energy_limit(UINT32_MAX),
  _time_limit(UINT32_MAX)
{
}

EvseProperties & EvseProperties::operator = (EvseProperties &rhs)
{
  _state = rhs._state;
  _charge_current = rhs._charge_current;
  _max_current = rhs._max_current;
  _energy_limit = rhs._energy_limit;
  _time_limit = rhs._time_limit;
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
  _sleepForDisable(true)
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
  bool foundClaim = false;

  int statePriority = 0;
  int chargeCurrentPriority = 0;
  int maxCurrentPriority = 0;
  int energyLimitPriority = 0;
  int timeLimitPriority = 0;
  int autoReleasePriority = 0;

  for(size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    if(_clients[i].isValid())
    {
      EvseManager::Claim &claim = _clients[i];

      if(claim.getPriority() > statePriority) {
        properties.setState(claim.getState());
        statePriority = claim.getPriority();
      }

      if(claim.getPriority() > chargeCurrentPriority) {
        properties.setChargeCurrent(claim.getChargeCurrent());
        chargeCurrentPriority = claim.getPriority();
      }

      if(claim.getPriority() > maxCurrentPriority) {
        properties.setMaxCurrent(claim.getMaxCurrent());
        maxCurrentPriority = claim.getPriority();
      }

      if(claim.getPriority() > energyLimitPriority) {
        properties.setEnergyLimit(claim.getEnergyLimit());
        energyLimitPriority = claim.getPriority();
      }

      if(claim.getPriority() > timeLimitPriority) {
        properties.setTimeLimit(claim.getTimeLimit());
        timeLimitPriority = claim.getPriority();
      }

      if(claim.getPriority() > autoReleasePriority) {
        properties.setAutoRelease(claim.getAutoRelease());
        autoReleasePriority = claim.getPriority();
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
    _state.setState(evse_state, pilot_state);
  });
}

void EvseManager::setEvseState(EvseState state)
{
  if(EvseState_NULL != state && state != _state.getEvseState())
  {
    if(EvseState_Active == state)
    {
      OpenEVSE.enable([this](int ret) { });
    }
    else
    {
      if(_sleepForDisable) {
        OpenEVSE.sleep([this](int ret) { });
      } else {
        OpenEVSE.disable([this](int ret) { });
      }
    }
  }
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

  // If we are not connected yet try and connect to the EVSE module
  if(!OpenEVSE.isConnected())
  {
    initialiseEvse();
    return 10 * 1000;
  }

  // Work out the state we should try and get in too
  EvseProperties targetProperties;
  bool hasClients = evaluateClaims(targetProperties);

  DBUGVAR(hasClients);
  if(hasClients)
  {
    DBUGVAR(targetProperties.getState());
    DBUGVAR(targetProperties.getChargeCurrent());
    DBUGVAR(targetProperties.getMaxCurrent());
    DBUGVAR(targetProperties.getEnergyLimit());
    DBUGVAR(targetProperties.getTimeLimit());
    DBUGVAR(targetProperties.getAutoRelease());

    setEvseState(targetProperties.getState());
  }
  else
  {
    // No clients, make sure the EVSE module is enabled
    if(_state.isDisabled())
    {
      OpenEVSE.enable([this](int ret) {
      });
    }
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

  DBUGF("New claim from 0x%08x, priority %d, %s", client, priority,
    EvseState_Active == target.getState() ? "active" :
    EvseState_Disabled == target.getState() ? "disabled" :
    EvseState_NULL == target.getState() ? "NULL" :
    "unknown");

  for (size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    if(_clients[i] == client) {
      slot = &(_clients[i]);
      break;
    } else if (NULL == slot && !_clients[i].isValid()) {
      slot = &(_clients[i]);
    }
  }

  if(slot)
  {
    slot->claim(client, priority, target);
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
  return EvseState_NULL;
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
