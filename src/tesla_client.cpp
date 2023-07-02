// -*- C++ -*-
/*
 * Copyright (c) 2020 Sam C. Lin and Chris Howell
 * Author: Sam C. Lin
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_TESLA_CLIENT)
#undef ENABLE_DEBUG
#endif

#include <ArduinoJson.h>
#include "tesla_client.h"
#include "debug.h"
#include "input.h"
#include "event.h"

#define TESLA_USER_AGENT "007"
#define TESLA_CLIENT_ID "81527cff06843c8634fdc09e8ac0abefb46ac849f38fe1e431c2ef2106796384"
#define TESLA_CLIENT_SECRET "c7257eb71a564034f9419ee651c7d0e5f7aa6bfbd18bafb5c5c033b093bb2fa3"

#define TESLA_BASE_URI "https://owner-api.teslamotors.com"
#define TESLA_HOST "owner-api.teslamotors.com"
#define TESLA_PORT 443

#define TESLA_REQ_INTERVAL (30*1000UL)
#define TESLA_REQ_TIMEOUT (10*1000UL)

const char *TeslaClient::_userAgent = TESLA_USER_AGENT;
const int TeslaClient::_httpPort = TESLA_PORT;
const char *TeslaClient::_teslaClientId = TESLA_CLIENT_ID;
const char *TeslaClient::_teslaClientSecret = TESLA_CLIENT_SECRET;
const char *TeslaClient::_badAuthStr = "authorization_required_for_txid";

// global instance
TeslaClient teslaClient;

TeslaClient::TeslaClient()
{
  _curVehIdx = -1;
  _lastRequestStart = 0;
  _activeRequest = TAR_NONE;
  _id = NULL;
  _vin = NULL;
  _displayName = NULL;
  _accessToken = "";
  _chargeInfo.isValid = false;

}

TeslaClient::~TeslaClient()
{
  _cleanVehicles();
}

void TeslaClient::_cleanVehicles()
{
  if (_id) {
    delete [] _id;
    _id = NULL;
  }

  if (_vin) {
    delete [] _vin;
    _vin = NULL;
  }

  if (_displayName) {
    delete [] _displayName;
    _displayName = NULL;
  }

  _vehicleCnt = 0;
}

void TeslaClient::setCredentials(
  String &accessToken,
  String &refreshToken,
  uint64_t created,
  uint64_t expires)
{
  _accessToken = "Bearer ";
  _accessToken += accessToken;
  _refreshToken = refreshToken;
  _created = created;
  _expires = expires;
  _cleanVehicles();
}


void TeslaClient::setVehicleId(String vehid)
{
  DBUGVAR(vehid);
  _curVehId = vehid;
  for(int i = 0; i < _vehicleCnt; i++) {
    if(_id[i] == vehid) {
      _curVehIdx = i;
      DBUGVAR(_curVehIdx);
      return;
    }
  }
}


String TeslaClient::getVehicleId(int vehidx)
{
  if ((vehidx >= 0) && (vehidx < _vehicleCnt)) {
    return _id[vehidx];
  }
  else return String("");
}

String TeslaClient::getVIN(int vehidx)
{
  if ((vehidx >= 0) && (vehidx < _vehicleCnt)) {
    return _vin[vehidx];
  }
  else return String("");
}

String TeslaClient::getVehicleDisplayName(int vehidx)
{
  if ((vehidx >= 0) && (vehidx < _vehicleCnt)) {
    return _displayName[vehidx];
  }
  else return String("");
}

void TeslaClient::loop()
{
  if ((_activeRequest != TAR_NONE) &&
      ((millis()-_lastRequestStart) > TESLA_REQ_TIMEOUT)) {
    _activeRequest = TAR_NONE;
  }

  if (!_isBusy()) {
    if ((millis()-_lastRequestStart) > TESLA_REQ_INTERVAL) {
      //if (_accessToken.length() == 0) {
      //  requestAccessToken();
      //}
      //else
      if (_vehicleCnt == 0) {
        requestVehicles();
      }
      else {
        requestChargeState();
      }
    }
  } // !busy
}

void printResponse(MongooseHttpClientResponse *response)
{
  DBUGF("%d %.*s", response->respCode(), response->respStatusMsg().length(), (const char *)response->respStatusMsg());
  int headers = response->headers();
  int i;
  for(i=0; i<headers; i++) {
    DBUGF("_HEADER[%.*s]: %.*s",
      response->headerNames(i).length(), (const char *)response->headerNames(i),
      response->headerValues(i).length(), (const char *)response->headerValues(i));
  }

  DBUGF("%.*s", response->body().length(), (const char *)response->body());
}

/*
void TeslaClient::requestAccessToken()
{
  DBUGLN("requestAccessToken()");
  _lastRequestStart = millis();

  if ((_teslaUser == 0) || (_teslaPass == 0)) {
    _activeRequest = TAR_NONE;
    return;
  }

  _activeRequest = TAR_ACC_TOKEN;

  String s = "{";
  s += "\"grant_type\":\"password\",";
  s += "\"client_id\":\"" + String(_teslaClientId) + "\",";
  s += "\"client_secret\":\"" + String(_teslaClientSecret) + "\",";
  s += "\"email\":\"" + _teslaUser + "\",";
  s += "\"password\":\"" + _teslaPass + "\"";
  s += "}";

  String uri = TESLA_BASE_URI;
  uri += "/oauth/token";

  DBUGLN(s);
  DBUGLN(uri);

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_POST);
  req->addHeader("Connection","keep-alive");
  req->addHeader("User-Agent",_userAgent);
  req->addHeader("Content-Type","application/json; charset=utf-8");
  //broken  req->setContentType("application/json; charset=utf-8");
  req->setContent((const uint8_t*)s.c_str(),s.length());
  req->onResponse([&](MongooseHttpClientResponse *response) {
      DBUGLN("resp");
      printResponse(response);

      // bad credentials - prevent locking of account
      // by clearing the user/pass so we don't retry w/ them
      if (response->respCode() == 401) {
        _teslaUser = "";
        _teslaPass = "";
        DBUGLN("bad user/pass");
      }
      else if (response->respCode() == 200) {
        const char *cjson = (const char *)response->body();
        if (cjson) {
          const size_t capacity = JSON_OBJECT_SIZE(5) + 220;
          DynamicJsonDocument doc(capacity);
          deserializeJson(doc, cjson);
          const char *token = (const char *)doc["access_token"];;
          if (token) {
            _accessToken = "Bearer ";
            _accessToken += token;
            DBUG("token: ");DBUGLN(_accessToken);
            _lastRequestStart = 0; // allow requestVehicles to happen immediately
          }
          else {
            _accessToken = "";
            DBUGLN("token error");
          }

          //const char* token_type = doc["token_type"];
          //long expires_in = doc["expires_in"];
          //const char* refresh_token = doc["refresh_token"];
          //long created_at = doc["created_at"]; // 1583079526
        }
      }
      _activeRequest = TAR_NONE;
    });
  _client.send(req);
}
*/

void TeslaClient::requestVehicles()
{
  DBUGLN("requestVehicles()");

  _activeRequest = TAR_VEHICLES;
  _lastRequestStart = millis();

  String uri = TESLA_BASE_URI;
  uri += "/api/1/vehicles/";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_GET);
  req->addHeader("Connection","keep-alive");
  req->addHeader("User-Agent",_userAgent);
  req->addHeader("Accept","*/*");
  req->addHeader("Authorization",_accessToken.c_str());
  req->onResponse([&](MongooseHttpClientResponse *response) {
      DBUGLN("resp");
      printResponse(response);

      if (response->respCode() == 401) {
          _accessToken = "";
          _activeRequest = TAR_NONE;
          return;
      }
      else if (response->respCode() == 200) {
        const char *json = (const char *)response->body();

        if (strstr(json,_badAuthStr)) {
          _accessToken = "";
          _activeRequest = TAR_NONE;
          return;
        }


        char *sc = strstr(json,"\"count\":");
        if (sc) {
          _cleanVehicles();
          sc += 8;
          sscanf(sc,"%d",&_vehicleCnt);
          DBUG("vcnt: ");DBUGLN(_vehicleCnt);

          _id = new String[_vehicleCnt];
          _vin = new String[_vehicleCnt];
          _displayName = new String[_vehicleCnt];

          // ArduinoJson has a bug, and parses the id as a float.
          // use this ugly workaround.
          const char *sj = json;
          for (int v=0;v < _vehicleCnt;v++) {
            const char *sid = strstr(sj,"\"id\":");
            if (sid) {
              sid += 5;
              _id[v] = "";
              while (*sid != ',') _id[v] += *(sid++);
              sj = sid;
            }
          }

          const size_t capacity = _vehicleCnt*JSON_ARRAY_SIZE(2) + JSON_ARRAY_SIZE(_vehicleCnt) + JSON_OBJECT_SIZE(2) + _vehicleCnt*JSON_OBJECT_SIZE(14) + _vehicleCnt*700;
          DynamicJsonDocument doc(capacity);
          deserializeJson(doc, json);
          JsonArray jresponse = doc["response"];

          for (int i=0;i < _vehicleCnt;i++) {
            JsonObject responsei = jresponse[i];
            _vin[i] = responsei["vin"].as<String>();
            // doesn't work.. returns converted float _id[i] = responsei["id"].as<String>();
            _displayName[i] = responsei["display_name"].as<String>();
            DBUG("id: ");DBUG(_id[i]);
            DBUG(" vin: ");DBUG(_vin[i]);
            DBUG(" name: ");DBUGLN(_displayName[i]);
            if(_id[i] == _curVehId) {
              _curVehIdx = i;
            }
          }

          if((_curVehIdx < 0) || (_curVehIdx >= _vehicleCnt)) {
            _curVehIdx = 0;
          }

          _curVehId = _id[_curVehIdx];

          DynamicJsonDocument data(128);
          data["tesla_vehicle_count"] = _vehicleCnt;
          data["tesla_vehicle_id"] = _id[_curVehIdx];
          data["tesla_vehicle_name"] = _displayName[_curVehIdx];
          event_send(data);
        }
      }
      _activeRequest = TAR_NONE;
    });
    _client.send(req);
}


void TeslaClient::requestChargeState()
{
  _chargeInfo.isValid = false;
  DBUG("getChargeState() vehidx=");DBUGLN(_curVehIdx);
  _lastRequestStart = millis();

  if ((_vehicleCnt <= 0) ||
      (_curVehIdx < 0) || (_curVehIdx > (_vehicleCnt-1))) {
    DBUGLN("vehicle idx out of range");
    _activeRequest = TAR_NONE;
    return;
  }

  _activeRequest = TAR_CHG_STATE;


  String uri = TESLA_BASE_URI;
  uri += "/api/1/vehicles/" + String(_id[_curVehIdx]) + "/data_request/charge_state";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_GET);
  req->addHeader("Connection","keep-alive");
  req->addHeader("User-Agent",_userAgent);
  req->addHeader("Accept","*/*");
  req->addHeader("Authorization",_accessToken.c_str());
  req->onResponse([&](MongooseHttpClientResponse *response)
  {
    DBUGLN("resp");
    printResponse(response);

    if (response->respCode() == 200)
    {
      const char *json = (const char *)response->body();
      const size_t capacity = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(43) + 1500;
      DynamicJsonDocument doc(capacity);
      deserializeJson(doc, json);

      JsonObject jresponse = doc["response"];

      _chargeInfo.batteryRange = jresponse["battery_range"];
      _chargeInfo.chargeEnergyAdded = jresponse["charge_energy_added"];
      _chargeInfo.chargeMilesAddedRated = jresponse["charge_miles_added_rated"];
      _chargeInfo.batteryLevel = jresponse["battery_level"];
      _chargeInfo.chargeLimitSOC = jresponse["charge_limit_soc"];
      _chargeInfo.timeToFullCharge = jresponse["time_to_full_charge"];
      _chargeInfo.chargerVoltage = jresponse["charger_voltage"];
      _chargeInfo.isValid = true;

#ifdef notyet
      bool response_battery_heater_on = jresponse["battery_heater_on"]; // false
      int response_charge_current_request = jresponse["charge_current_request"]; // 24
      int response_charge_current_request_max = jresponse["charge_current_request_max"]; // 48
      bool response_charge_enable_request = jresponse["charge_enable_request"]; // true

      int response_charge_limit_soc_max = jresponse["charge_limit_soc_max"]; // 100
      int response_charge_limit_soc_min = jresponse["charge_limit_soc_min"]; // 50
      int response_charge_limit_soc_std = jresponse["charge_limit_soc_std"]; // 90
      float response_charge_miles_added_ideal = jresponse["charge_miles_added_ideal"]; // 205.5
      bool response_charge_port_door_open = jresponse["charge_port_door_open"]; // true
      const char* response_charge_port_latch = jresponse["charge_port_latch"]; // "Engaged"
      int response_charge_rate = jresponse["charge_rate"]; // 0
      bool response_charge_to_max_range = jresponse["charge_to_max_range"]; // false
      int response_charger_actual_current = jresponse["charger_actual_current"]; // 0
      int response_charger_pilot_current = jresponse["charger_pilot_current"]; // 48
      int response_charger_power = jresponse["charger_power"]; // 0

      const char* response_charging_state = jresponse["charging_state"]; // "Complete"
      const char* response_conn_charge_cable = jresponse["conn_charge_cable"]; // "IEC"
      float response_est_battery_range = jresponse["est_battery_range"]; // 187.27
      const char* response_fast_charger_brand = jresponse["fast_charger_brand"]; // ""
      bool response_fast_charger_present = jresponse["fast_charger_present"]; // false
      const char* response_fast_charger_type = jresponse["fast_charger_type"]; // ""
      float response_ideal_battery_range = jresponse["ideal_battery_range"]; // 230.21
      bool response_managed_charging_active = jresponse["managed_charging_active"]; // false
      bool response_managed_charging_user_canceled = jresponse["managed_charging_user_canceled"]; // false
      int response_max_range_charge_counter = jresponse["max_range_charge_counter"]; // 0
      int response_minutes_to_full_charge = jresponse["minutes_to_full_charge"]; // 0
      bool response_not_enough_power_to_heat = jresponse["not_enough_power_to_heat"]; // false
      bool response_scheduled_charging_pending = jresponse["scheduled_charging_pending"]; // false

      long response_timestamp = jresponse["timestamp"]; // 1583079530271
      bool response_trip_charging = jresponse["trip_charging"]; // false
      int response_usable_battery_level = jresponse["usable_battery_level"]; // 83
#endif // notyet

      evse.setVehicleStateOfCharge(_chargeInfo.batteryLevel);
      evse.setVehicleRange(_chargeInfo.batteryRange);
      evse.setVehicleEta(_hoursToSeconds(_chargeInfo.timeToFullCharge));
      evse.setVoltage(_chargeInfo.chargerVoltage);

      DynamicJsonDocument data(4096);
      getChargeInfoJson(data);
      event_send(data);
    }
    else
    {
      const char *json = (const char *)response->body();
      const size_t capacity = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(43) + 1500;
      DynamicJsonDocument doc(capacity);
      deserializeJson(doc, json);

      DynamicJsonDocument data(4096);
      data["tesla_error"] = doc["error"];
      event_send(data);
    }

    _activeRequest = TAR_NONE;
  });
  _client.send(req);
}

void TeslaClient::getChargeInfoJson(JsonDocument &doc)
{
  if (_chargeInfo.isValid)
  {
    doc["battery_range"] = _chargeInfo.batteryRange;
    doc["battery_level"] = _chargeInfo.batteryLevel;
    doc["charge_energy_added"] = _chargeInfo.chargeEnergyAdded;
    doc["charge_miles_added_rated"] = _chargeInfo.chargeMilesAddedRated;
    doc["charge_limit_soc"] = _chargeInfo.chargeLimitSOC;
    doc["time_to_full_charge"] = _hoursToSeconds(_chargeInfo.timeToFullCharge);
    doc["charger_voltage"] = _chargeInfo.chargerVoltage;
    doc["tesla_error"] = false;
  }
}

int TeslaClient::_hoursToSeconds(float hours)
{
  // time_to_full_charge is in hours (float) with ~5min intervals, needs to be seconds for
  // our internal representation
  int minutes = (int)round(60.0 * hours);
  return minutes * 60;
}
