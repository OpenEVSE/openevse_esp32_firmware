#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_TIME)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <MongooseCore.h>

#include "debug.h"
#include "time_man.h"
#include "net_manager.h"
#include "openevse.h"
#include "app_config.h"
#include "event.h"

#ifndef TIME_POLL_TIME
// Check the time every 8 hours
#define TIME_POLL_TIME 8 * 60 * 60 * 1000
//#define TIME_POLL_TIME 10 * 1000
#endif

TimeManager timeManager;

TimeManager::TimeManager() :
  MicroTasks::Task(),
  _timeHost(NULL),
  _sntp(),
  _nextCheckTime(0),
  _fetchingTime(false),
  _setTheTime(false)
{
}

void TimeManager::begin()
{
  MicroTask.startTask(this);
}

void TimeManager::setHost(const char *host)
{
  _timeHost = host;
  checkNow();
}

void TimeManager::setup()
{
  config_set_timezone(time_zone);

  _sntp.onError([this](uint8_t err) {
    DBUGF("Got error %u", err);
    _fetchingTime = false;
    _nextCheckTime = millis() + 10 * 1000;
    MicroTask.wakeTask(this);
  });
}

unsigned long TimeManager::loop(MicroTasks::WakeReason reason)
{
#ifdef ENABLE_DEBUG
  DBUG("Time manager woke: ");
  DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
         WakeReason_Event == reason ? "WakeReason_Event" :
         WakeReason_Message == reason ? "WakeReason_Message" :
         WakeReason_Manual == reason ? "WakeReason_Manual" :
         "UNKNOWN");

  if(!_setTheTime)
  {
    timeval utc_time;
    gettimeofday(&utc_time, NULL);
    tm local_time, gm_time;
    localtime_r(&utc_time.tv_sec, &local_time);
    gmtime_r(&utc_time.tv_sec, &gm_time);
    const char *tz = getenv("TZ");
    DBUGF("Time now, Local: %s, UTC: %s, %s",
      time_format_time(local_time).c_str(),
      time_format_time(gm_time).c_str(),
      tz ? tz : "TZ not set");
  }

#endif

  unsigned long ret = MicroTask.Infinate;

  DBUGVAR(_setTheTime);
  if(_setTheTime)
  {
    struct timeval utc_time;
    gettimeofday(&utc_time, NULL);

    DBUGVAR(utc_time.tv_usec);
    if(utc_time.tv_usec >= 999000)
    {
      _setTheTime = false;

      tm local_time, gm_time;
      localtime_r(&utc_time.tv_sec, &local_time);
      gmtime_r(&utc_time.tv_sec, &gm_time);
      DBUGF("Setting the time on the EVSE, Local: %s, UTC, %s",
        time_format_time(local_time).c_str(),
        time_format_time(gm_time).c_str());
      OpenEVSE.setTime(gm_time, [this](int ret)
      {
        DBUGF("EVSE time %sset", RAPI_RESPONSE_OK == ret ? "" : "not ");
      });
    }
    else
    {
      unsigned long msec = utc_time.tv_usec / 1000;
      DBUGVAR(msec);
      unsigned long delay = msec < 998 ? 998 - msec : 0;
      DBUGVAR(delay);
      return delay;
    }
  }

  DBUGVAR(_nextCheckTime);
  if(config_sntp_enabled() &&
    NULL != _timeHost &&
    _nextCheckTime > 0)
  {
    int64_t delay = (int64_t)_nextCheckTime - (int64_t)millis();

    if(net.isConnected() &&
      false == _fetchingTime &&
      delay <= 0)
    {
      _fetchingTime = true;
      _nextCheckTime = 0;

      DBUGF("Trying to get time from %s", _timeHost);
      _sntp.getTime(_timeHost, [this](struct timeval newTime)
      {
        setTime(newTime, _timeHost);

        _fetchingTime = false;
        _nextCheckTime = millis() + TIME_POLL_TIME;
        MicroTask.wakeTask(this);
      });
    } else {
      ret = delay > 0 ? (unsigned long)delay : 0;
    }
  }

  return ret;
}

void TimeManager::setTime(struct timeval setTime, const char *source)
{
  timeval local_time;

  // Set the local time
  gettimeofday(&local_time, NULL);
  DBUGF("Local time: %s", time_format_time(local_time.tv_sec).c_str());
  DBUGF("Time from %s: %s", source, time_format_time(setTime.tv_sec).c_str());
  DBUGF("Diff %.2f", diffTime(setTime, local_time));
  settimeofday(&setTime, NULL);

  // Set the time on the OpenEVSE, set from the local time as this could take several ms
  OpenEVSE.getTime([this](int ret, time_t evse_time)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      time_t local_time = time(NULL);
      DBUGF("Local time: %s", time_format_time(local_time).c_str());
      DBUGF("Time from EVSE: %s", time_format_time(evse_time).c_str());
      time_t diff = local_time - evse_time;
      DBUGF("Diff %ld", diff);

      if(diff != 0)
      {
        // The EVSE can only be set to second accuracy, actually set the time in the loop on 0 ms
        DBUGF("Time change required");
        _setTheTime = true;
        MicroTask.wakeTask(this);
      }
    } else {
      DBUGF("Failed to get the EVSE time: %d", ret);
    }
  });

  // Event the time change
  StaticJsonDocument<64> doc;
  serialise(doc);
  event_send(doc);

  _timeChange.Trigger();
}

double TimeManager::diffTime(timeval tv1, timeval tv2)
{
    double t1 = (double) tv1.tv_sec + (((double) tv1.tv_usec) / 1000000.0);
    double t2 = (double) tv2.tv_sec + (((double) tv2.tv_usec) / 1000000.0);

    return t1-t2;
}

void time_check_now() {
  timeManager.checkNow();
}

void time_set_time(struct timeval setTime, const char *source) {
  timeManager.setTime(setTime, source);
}

String time_format_time(time_t time)
{
  struct tm timeinfo;
  localtime_r(&time, &timeinfo);
  return time_format_time(timeinfo);
}

String time_format_time(tm &time)
{
  //See http://www.cplusplus.com/reference/ctime/strftime/
  char output[80];
  strftime(output, 80, "%d-%b-%y, %H:%M:%S", &time);
  return String(output);
}

void TimeManager::serialise(JsonDocument &doc)
{
  // get the current time
  char time[64];
  char offset[8];

  struct timeval local_time;
  gettimeofday(&local_time, NULL);

  struct tm * timeinfo = gmtime(&local_time.tv_sec);
  strftime(time, sizeof(time), "%FT%TZ", timeinfo);
  strftime(offset, sizeof(offset), "%z", timeinfo);

  doc["time"] = time;
  doc["offset"] = offset;
}