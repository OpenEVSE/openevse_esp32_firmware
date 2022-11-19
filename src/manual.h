#ifndef _OPENEVSE_MANUAL_H
#define _OPENEVSE_MANUAL_H

#include "evse_man.h"
#include "json_serialize.h"

class ManualOverride
{
  private:
    EvseManager *_evse;
    uint8_t _version;
  public:
    ManualOverride(EvseManager &evse);
    ~ManualOverride();

    bool claim();
    bool claim(EvseProperties &props);
    bool release();
    bool toggle();

    bool isActive() {
      return _evse->clientHasClaim(EvseClient_OpenEVSE_Manual);
    }
    bool getProperties(EvseProperties &props);
    uint8_t getVersion();
};

extern ManualOverride manual;

#endif // _OPENEVSE_MANUAL_H
