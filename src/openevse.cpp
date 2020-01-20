#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_OPENEVSE)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <MicroDebug.h>

#include "openevse.h"

OpenEVSEClass::OpenEVSEClass() :
  _sender(NULL),
  _connected(false),
  _protocol(OPENEVSE_ENCODE_VERSION(1,0,0))
{
}

void OpenEVSEClass::begin(RapiSender &sender, std::function<void(bool connected)> callback)
{
  _connected = false;
  _sender = &sender;

  getVersion([this, callback](int ret, const char *firmware, const char *protocol) {
    if (RAPI_RESPONSE_OK == ret) {
      int major, minor, patch;
      if(3 == sscanf(protocol, "%d.%d.%d", &major, &minor, &patch))
      {
        _protocol = OPENEVSE_ENCODE_VERSION(major, minor, patch);
        DBUGVAR(_protocol);
        _connected = true;
      }
    }

    callback(_connected);
  });
}

void OpenEVSEClass::getVersion(std::function<void(int ret, const char *firmware, const char *protocol)> callback)
{
  // Check state the OpenEVSE is in.
  _sender->sendCmd("$GV", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 3)
      {
        const char *firmware = _sender->getToken(1);
        const char *protocol = _sender->getToken(2);

        callback(RAPI_RESPONSE_OK, firmware, protocol);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, NULL, NULL);
      }
    } else {
      callback(ret, NULL, NULL);
    }
  });
}

void OpenEVSEClass::getStatus(std::function<void(int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)> callback)
{
  // Check state the OpenEVSE is in.
  _sender->sendCmd("$GS", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      int tokens_required = (_protocol < OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION) ? 3 : 5;
      int state_base = (_protocol < OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION) ? 10 : 16;
      if(_sender->getTokenCnt() >= tokens_required)
      {
        const char *val = _sender->getToken(1);
        DBUGVAR(val);
        uint8_t evse_state = strtol(val, NULL, state_base);
        DBUGVAR(evse_state);
        val = _sender->getToken(2);
        DBUGVAR(val);
        uint32_t elapsed = strtol(val, NULL, 10);
        DBUGVAR(elapsed);

        uint8_t pilot_state = OPENEVSE_STATE_INVALID;
        uint32_t vflags = 0;

        if(_protocol >= OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION) {
          val = _sender->getToken(1);
          DBUGVAR(val);
          pilot_state = strtol(val, NULL, state_base);
          DBUGVAR(pilot_state);
          val = _sender->getToken(2);
          DBUGVAR(val);
          vflags = strtol(val, NULL, 16);
          DBUGVAR(vflags);
        }

        callback(RAPI_RESPONSE_OK, evse_state, elapsed, pilot_state, vflags);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, OPENEVSE_STATE_INVALID, 0, OPENEVSE_STATE_INVALID, 0);
      }
    } else {
      callback(ret, OPENEVSE_STATE_INVALID, 0, OPENEVSE_STATE_INVALID, 0);
    }
  });
}

OpenEVSEClass OpenEVSE;
