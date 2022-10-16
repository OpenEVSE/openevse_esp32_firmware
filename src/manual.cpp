#include "manual.h"

ManualOverride::ManualOverride(EvseManager &evse) :
  _evse((&evse))
{
}

ManualOverride::~ManualOverride()
{
}

bool ManualOverride::claim()
{
  EvseProperties props(EvseState::Active == _evse->getState() ?  EvseState::Disabled : EvseState::Active);
  return claim(props);
}

bool ManualOverride::claim(EvseProperties &props)
{
  return _evse->claim(EvseClient_OpenEVSE_Manual, EvseManager_Priority_Manual, props);
}

bool ManualOverride::release()
{
  return _evse->release(EvseClient_OpenEVSE_Manual);
}

bool ManualOverride::toggle()
{
  if(!isActive())
  {
    return claim();
  }

  return release();
}

bool ManualOverride::getProperties(EvseProperties &props)
{
  if(isActive()) {
    props = _evse->getClaimProperties(EvseClient_OpenEVSE_Manual);
    return true;
  }

  return false;
}
