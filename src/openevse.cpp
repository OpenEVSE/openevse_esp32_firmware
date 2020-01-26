#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_OPENEVSE)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <MicroDebug.h>

#include "openevse.h"

OpenEVSEClass::OpenEVSEClass() :
  _sender(NULL),
  _connected(false),
  _protocol(OPENEVSE_ENCODE_VERSION(1,0,0)),
  _boot(NULL),
  _state(NULL),
  _wifi(NULL)
{
}

void OpenEVSEClass::begin(RapiSender &sender, std::function<void(bool connected)> callback)
{
  _connected = false;
  _sender = &sender;

  _sender->setOnEvent([this]() { onEvent(); });
  _sender->enableSequenceId(0);

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
  // Check OpenEVSE version is in.
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
        uint8_t evse_state = strtol(val, NULL, state_base);

        val = _sender->getToken(2);
        uint32_t elapsed = strtol(val, NULL, 10);

        uint8_t pilot_state = OPENEVSE_STATE_INVALID;
        uint32_t vflags = 0;

        if(_protocol >= OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION) {
          val = _sender->getToken(3);
          pilot_state = strtol(val, NULL, state_base);

          val = _sender->getToken(4);
          vflags = strtol(val, NULL, 16);
        }

        DBUGF("evse_state = %02x, elapsed = %d, pilot_state = %02x, vflags = %08x", evse_state, elapsed, pilot_state, vflags);
        callback(RAPI_RESPONSE_OK, evse_state, elapsed, pilot_state, vflags);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, OPENEVSE_STATE_INVALID, 0, OPENEVSE_STATE_INVALID, 0);
      }
    } else {
      callback(ret, OPENEVSE_STATE_INVALID, 0, OPENEVSE_STATE_INVALID, 0);
    }
  });
}

void OpenEVSEClass::getTime(std::function<void(int ret, time_t time)> callback)
{
  // GT - get time (RTC)
  // response: $OK yr mo day hr min sec       yr=2-digit year
  // $GT^37

  _sender->sendCmd("$GT", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 7)
      {
        long year = strtol(_sender->getToken(1), NULL, 10);
        long month = strtol(_sender->getToken(2), NULL, 10);
        long day = strtol(_sender->getToken(3), NULL, 10);
        long hour = strtol(_sender->getToken(4), NULL, 10);
        long minute = strtol(_sender->getToken(5), NULL, 10);
        long second = strtol(_sender->getToken(6), NULL, 10);

        DBUGF("Got time %ld %ld %ld %ld %ld %ld", year, month, day, hour, minute, second);

        if(165 != year && 165 != month && 165 != day && 165 != hour && 165 != minute && 85 != second)
        {
          struct tm tm = {0};
          
          tm.tm_year = 100+year;
          tm.tm_mon = month;
          tm.tm_mday = day;
          tm.tm_hour = hour;
          tm.tm_min = minute;
          tm.tm_sec = second;

          time_t time = mktime(&tm);
          callback(RAPI_RESPONSE_OK, time);
        } else {
          callback(RAPI_RESPONSE_FEATURE_NOT_SUPPORTED, 0);
        }
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, 0);
      }
    } else {
      callback(ret, 0);
    }
  });
}

void OpenEVSEClass::setTime(time_t time, std::function<void(int ret)> callback)
{
  // S1 yr mo day hr min sec - set clock (RTC) yr=2-digit year

  struct tm tm;
  gmtime_r(&time, &tm);

  char command[64];
  snprintf(command, sizeof(command), "$S1 %d %d %d %d %d %d", 
    tm.tm_year % 100, 
    tm.tm_mon, 
    tm.tm_mday, 
    tm.tm_hour, 
    tm.tm_min, 
    tm.tm_sec);

  _sender->sendCmd(command, [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 1)
      {
        callback(RAPI_RESPONSE_OK);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE);
      }
    } else {
      callback(ret);
    }
  });

}

void OpenEVSEClass::onEvent()
{
  DBUGF("Got ASYNC event %s", _sender->getToken(0));

  if(!strcmp(_sender->getToken(0), "$ST"))
  {
    const char *val = _sender->getToken(1);
    DBUGVAR(val);
    uint8_t state = strtol(val, NULL, 16);
    DBUGVAR(state);

    if(_state) {
      _state(state, OPENEVSE_STATE_INVALID, 0, 0);
    }
  }
  else if(!strcmp(_sender->getToken(0), "$WF"))
  {
    const char *val = _sender->getToken(1);
    DBUGVAR(val);

    uint8_t wifiMode = strtol(val, NULL, 10);
    DBUGVAR(wifiMode);

    if(_wifi) {
      _wifi(wifiMode);
    }
  }
  else if(!strcmp(_sender->getToken(0), "$AT"))
  {
    const char *val = _sender->getToken(1);
    uint8_t evse_state = strtol(val, NULL, 16);

    val = _sender->getToken(2);
    uint8_t pilot_state = strtol(val, NULL, 16);

    val = _sender->getToken(3);
    uint32_t current_capacity = strtol(val, NULL, 10);

    val = _sender->getToken(4);
    uint32_t vflags = strtol(val, NULL, 16);

    DBUGF("evse_state = %02x, pilot_state = %02x, current_capacity = %d, vflags = %08x", evse_state, pilot_state, current_capacity, vflags);

    if(_state) {
      _state(evse_state, pilot_state, current_capacity, vflags);
    }
  }
  else if(!strcmp(_sender->getToken(0), "$AB"))
  {
    const char *val = _sender->getToken(1);
    uint8_t post_code = strtol(val, NULL, 16);

    if(_boot) {
      _boot(post_code, _sender->getToken(2));
    }
  }
}

OpenEVSEClass OpenEVSE;
