#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

typedef const __FlashStringHelper *fstr_t;

#include "emonesp.h"
#include "web_server.h"
#include "app_config.h"
#include "event.h"
#include "time_man.h"

extern bool isPositive(MongooseHttpServerRequest *request, const char *param);
extern bool web_server_config_deserialise(DynamicJsonDocument &doc, bool factory);

void handleTimePost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  bool sntp_enabled = false;
  String time;
  String time_zone;
  bool is_json = false;

  MongooseString type = request->headers("Content-Type");
  if(type.equalsIgnoreCase("application/x-www-form-urlencoded"))
  {
    response->setContentType(CONTENT_TYPE_TEXT);

    String time_zone = request->getParam("time_zone");
    if(!time_zone.length())
    {
      time_zone = request->getParam("tz");
      if(!time_zone.length()) {
        time_zone = "UTC0";
      }
    }

    sntp_enabled = isPositive(request, "ntp");
    time = request->getParam("time");
  }
  else if(type.equalsIgnoreCase("application/json"))
  {
    is_json = true;
    response->setContentType(CONTENT_TYPE_JSON);

    DynamicJsonDocument doc(1024);
    MongooseString body = request->body();
    DeserializationError error = deserializeJson(doc, body.c_str(), body.length());
    if(!error)
    {
      sntp_enabled = doc["sntp_enabled"];
      time = doc["time"].as<String>();
      time_zone = doc["time_zone"].as<String>();
    } else {
      response->setCode(400);
      response->print("{\"msg\":\"Could not parse JSON\"}");
      return;
    }
  }

  DBUGF("sntp_enabled: %d", sntp_enabled);
  DBUGF("time: %s", time.c_str());
  DBUGF("time_zone: %s", time_zone.c_str());

  DynamicJsonDocument config(1024);
  config["sntp_enabled"] = sntp_enabled;
  config["time_zone"] = time_zone;
  web_server_config_deserialise(config, false);

  if(false == sntp_enabled)
  {
    struct tm tm;

    int yr, mnth, d, h, m, s;
    if(time.endsWith("Z") && 6 == sscanf( time.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &yr, &mnth, &d, &h, &m, &s))
    {
      tm.tm_year = yr - 1900;
      tm.tm_mon = mnth - 1;
      tm.tm_mday = d;
      tm.tm_hour = h;
      tm.tm_min = m;
      tm.tm_sec = s;

      struct timeval set_time = {0,0};
      set_time.tv_sec = mktime(&tm);

      time_set_time(set_time, "manual");
    }
    else
    {
      response->setCode(400);
      response->print(is_json ? "{\"msg\":\"Could not parse time\"}" : "could not parse time");
      return;
    }
  }

  response->setCode(200);
  response->print(is_json ? "{\"msg\":\"done\"}" : "set");
}

void handleTimeGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  StaticJsonDocument<128> doc;
  timeManager.serialise(doc);
  response->setCode(200);
  serializeJson(doc, *response);
}

void handleTime(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {
    handleTimeGet(request, response);
  } else if(HTTP_POST == request->method()) {
    handleTimePost(request, response);
  } else if(HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

