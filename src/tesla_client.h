// -*- C++ -*-
/*
 * Copyright (c) 2020 Sam C. Lin and Chris Howell
 * Author: Sam C. Lin
 */

#ifndef _TESLA_CLIENT_H_
#define _TESLA_CLIENT_H_

#include <Arduino.h>
#include <ArduinoJson.h>

#include <MongooseString.h>
#include <MongooseHttpClient.h>


//TAR = Tesla_Active_Request
#define TAR_NONE      0
#define TAR_ACC_TOKEN 1
#define TAR_VEHICLES  2
#define TAR_CHG_STATE 3

typedef struct telsa_charge_info {
  bool isValid;
  float batteryRange;
  float chargeEnergyAdded;
  float chargeMilesAddedRated;
  int batteryLevel;
  int chargeLimitSOC;
  float timeToFullCharge;
  int chargerVoltage;
} TESLA_CHARGE_INFO;

class TeslaClient {
  static const int _httpPort;
  static const char *_userAgent;
  static const char *_teslaClientId;
  static const char *_teslaClientSecret;
  static const char *_badAuthStr;

  int _activeRequest; // TAR_xxx
  unsigned long _lastRequestStart;
  String _accessToken;
  String _refreshToken;
  uint64_t _created;
  uint64_t _expires;

  int _vehicleCnt;
  int _curVehIdx;
  String _curVehId;
  String *_id;
  String *_vin;
  String *_displayName;

  TESLA_CHARGE_INFO _chargeInfo;

  MongooseHttpClient _client;

  bool _isBusy() { return _activeRequest == TAR_NONE ? false : true; }
  void _cleanVehicles();
  int _hoursToSeconds(float hours);

 public:
  TeslaClient();
  ~TeslaClient();

  void setCredentials(String &accessToken, String &refreshToken, uint64_t created, uint64_t expires);

//  void requestAccessToken();
  void requestVehicles();
  void requestChargeState();

  int getVehicleCnt() { return _vehicleCnt; }
  void setVehicleId(String vehid);
  int getCurVehicleIdx() { return _curVehIdx; }
  String getVehicleId(int vehidx);
  String getVIN(int vehidx);
  String getVehicleDisplayName(int vehidx);
//  const char *getUser() { return _teslaUser.c_str(); }
//  const char *getPass() { return _teslaPass.c_str(); }
  void getChargeInfoJson(JsonDocument &sjson);
  const TESLA_CHARGE_INFO *getChargeInfo() { return &_chargeInfo; }

  void loop();
};

// global instance
extern TeslaClient teslaClient;

#endif // _TESLA_CLIENT_H_
