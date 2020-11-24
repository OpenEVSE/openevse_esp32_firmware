#include <openevse.h>

#include "evse_man.h"

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

EvseManager::EvseManager(Stream &port) :
  _sender(&port)
{
}

EvseManager::~EvseManager()
{
}

void EvseManager::setup()
{
  
}

unsigned long EvseManager::loop(MicroTasks::WakeReason reason)
{
  return MicroTask.Infinate;
}

bool EvseManager::begin()
{
  MicroTask.startTask(this);

  return true;
}

bool EvseManager::claim(EvseClient client, int priority, EvseProperties &target)
{
  return false;
}

bool EvseManager::release(EvseClient client)
{
  return false;
}

bool EvseManager::clientHasClaim(EvseClient client)
{
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
