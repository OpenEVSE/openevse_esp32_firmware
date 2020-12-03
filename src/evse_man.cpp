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

EvseManager::Claims::Claims() :
  _client(EvseClient_NULL),
  _priority(0),
  _properties()
{
}

void EvseManager::Claims::claim(EvseClient client, int priority, EvseProperties &target)
{
  _client = client;
  _priority = priority;
  _properties = target;
}

void EvseManager::Claims::release()
{
  _client = EvseClient_NULL;
}

EvseManager::EvseStateEvent::EvseStateEvent() :
  MicroTasks::Event(),
  _state(OPENEVSE_STATE_STARTING)
{
}

EvseManager::EvseManager(Stream &port) :
  _sender(&port),
  _state(),
  _evseStateListener(this)
{
}

EvseManager::~EvseManager()
{
}

void EvseManager::setup()
{
  _state.Register(&_evseStateListener);

  OpenEVSE.onState([this](uint8_t evse_state, uint8_t pilot_state, uint32_t current_capacity, uint32_t vflags)
  {
    DBUGVAR(evse_state);
    _state.setState(evse_state);
  });
}

unsigned long EvseManager::loop(MicroTasks::WakeReason reason)
{
  if(!OpenEVSE.isConnected())
  {
    // Check state the OpenEVSE is in.
    OpenEVSE.begin(_sender, [this](bool connected)
    {
      if(connected)
      {
        OpenEVSE.getStatus([this](int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags) {
          _state.setState(evse_state);
        });
      } else {
        DBUGLN("OpenEVSE not responding or not connected");
      }
    });

    return 10 * 1000;
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
  Claims *slot = NULL;

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
  for (size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    if(_clients[i] == client)
    {
      _clients[i].release();
      MicroTask.wakeTask(this);
      return true;
    }
  }

  return false;
}

bool EvseManager::clientHasClaim(EvseClient client)
{
  for (size_t i = 0; i < EVSE_MANAGER_MAX_CLIENT_CLAIMS; i++)
  {
    if(_clients[i] == client) {
      return true;
    }
  }

  return false;
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
