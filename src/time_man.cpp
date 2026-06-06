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
  _fetchStartTime(0),
  _retryCount(0),
  _fetchingTime(false),
  _setTheTime(false),
  _lastSyncTime(0)
{
}

unsigned long TimeManager::retryDelay()
{
  // Exponential back-off: 10 s, 30 s, 90 s, 5 min, 30 min (cap)
  static const unsigned long delays[] = {
    10 * 1000UL,
    30 * 1000UL,
    90 * 1000UL,
     5 * 60 * 1000UL,
    30 * 60 * 1000UL,
  };
  uint8_t idx = _retryCount < sizeof(delays) / sizeof(delays[0])
                  ? _retryCount
                  : (uint8_t)(sizeof(delays) / sizeof(delays[0]) - 1);
  return delays[idx];
}

const char *TimeManager::getNtpStatus()
{
  if (!_sntpEnabled)      return "disabled";
  if (_fetchingTime)      return "connecting";
  if (_retryCount > 0)    return "retry";
  if (_lastSyncTime > 0)  return "synchronized";
  return "waiting";
}

int32_t TimeManager::getNextSyncMs()
{
  if (!_sntpEnabled || _timeHost == NULL) return -1;
  if (_fetchingTime)                      return 0;
  if (_nextCheckTime == 0)                return -1;
  int64_t diff = (int64_t)_nextCheckTime - (int64_t)millis();
  if (diff > INT32_MAX) return INT32_MAX;
  return (int32_t)(diff > 0 ? diff : 0);
}

void TimeManager::begin()
{
  MicroTask.startTask(this);
}

void TimeManager::setHost(const char *host)
{
  _timeHost = host;
  _fetchingTime = false;
  _retryCount   = 0;
  // Allow 2 s for the DNS resolver to initialise after WiFi connects before
  // firing the first request.  Update Now / mode-change use checkNow()
  // directly and bypass this delay to stay fully responsive.
  _nextCheckTime = millis() + 2000;
  MicroTask.wakeTask(this);
}

bool TimeManager::setTimeZone(String tz)
{
  const char *set_tz = tz.c_str();
  const char *split_pos = strchr(set_tz, '|');
  if(split_pos) {
    set_tz = split_pos + 1;
  }

  DBUGVAR(set_tz);

  setenv("TZ", set_tz, 1);
  tzset();

  DBUGVAR(tzname[0]);
  DBUGVAR(tzname[1]);

  if(set_tz[0] == '<') {
    set_tz++;
  }

  if(strncmp(set_tz, tzname[0], strlen(tzname[0])) != 0) {
    DBUGF("Timezone not set");
    return false;
  }

  DBUGLN("Timezone set");
// publish new time
 StaticJsonDocument<128> event;
 serialise(event);
 event_send(event);
  return true;
}

void TimeManager::setup()
{
  setTimeZone(time_zone);

  _sntp.onError([this](uint8_t err) {
    DBUGF("NTP error %u (attempt %u)", err, _retryCount + 1);
    _fetchingTime = false;
    unsigned long delay = retryDelay();
    _retryCount++;
    _nextCheckTime = millis() + delay;
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

      DBUGF("Setting the time on the EVSE, %s",
        time_format_time(utc_time.tv_sec).c_str());
      OpenEVSE.setTime(utc_time.tv_sec, [this](int ret)
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

  // Watchdog: if an in-flight request has gone silent (MG_EV_CLOSE fired
  // without MG_SNTP_REPLY or MG_SNTP_FAILED), unstick _fetchingTime.
  if(_fetchingTime)
  {
    unsigned long elapsed = millis() - _fetchStartTime;
    if(elapsed >= SNTP_FETCH_TIMEOUT)
    {
      DBUGF("NTP fetch timed out after %lums", elapsed);
      _fetchingTime = false;
      unsigned long delay = retryDelay();
      _retryCount++;
      _nextCheckTime = millis() + delay;
      // fall through to the scheduling block below
    }
    else
    {
      // Wake when the timeout expires so the watchdog above can fire
      return (unsigned long)(SNTP_FETCH_TIMEOUT - elapsed);
    }
  }

  DBUGVAR(_nextCheckTime);
  if(_sntpEnabled &&
    NULL != _timeHost &&
    _nextCheckTime > 0)
  {
    int64_t delay = (int64_t)_nextCheckTime - (int64_t)millis();

    if(net.isConnected() &&
      false == _fetchingTime &&
      delay <= 0)
    {
      _fetchingTime = true;
      _fetchStartTime = millis();
      _nextCheckTime = 0;

      DBUGF("Trying to get time from %s", _timeHost);
      bool started = _sntp.getTime(_timeHost, [this](struct timeval newTime)
      {
        setTime(newTime, _timeHost);

        _fetchingTime  = false;
        _retryCount    = 0;
        _lastSyncTime  = newTime.tv_sec;   // use NTP ts directly
        _nextCheckTime = millis() + TIME_POLL_TIME;
        MicroTask.wakeTask(this);
      });

      if(started)
      {
        // Return the watchdog deadline so the task self-wakes if the
        // callback never fires (stale _nc, DNS failure, dropped packet)
        ret = SNTP_FETCH_TIMEOUT;
      }
      else
      {
        // getTime() returned false — either the stale-_nc library patch
        // hasn't been applied yet and _nc is non-NULL, or mg_sntp_get_time()
        // itself failed transiently (no memory, DNS not yet ready, etc.).
        // Don't increment _retryCount: no network request was actually sent
        // so this isn't a server-side failure.  Retry in 2 s so the UI
        // stays on "waiting"/"synchronized" rather than jumping to "retry".
        DBUGLN("NTP: getTime() could not start, retrying in 2s");
        _fetchingTime = false;
        _nextCheckTime = millis() + 2000;
        ret = 2000;
      }
    } else {
      ret = delay > 0 ? (unsigned long)delay : 0;
    }
  }

  return ret;
}

void TimeManager::setTime(struct timeval setTime, const char *source)
{
  timeval local_time;
  timezone tz_utc = {0,0};

  // Set the local time
  gettimeofday(&local_time, NULL);
  DBUGF("Local time: %s", time_format_time(local_time.tv_sec).c_str());
  DBUGF("Time from %s: %s", source, time_format_time(setTime.tv_sec).c_str());
  DBUGF("Diff %.2f", diffTime(setTime, local_time));
  settimeofday(&setTime, &tz_utc);

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
  StaticJsonDocument<128> doc;
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

void TimeManager::setSntpEnabled(bool enabled)
{
  if(enabled != _sntpEnabled)
  {
    _sntpEnabled = enabled;
    if(enabled) {
      checkNow();   // fresh start; checkNow() clears retry state
    }
  }
}

void time_set_time(struct timeval setTime, const char *source) {
  timeManager.setTime(setTime, source);
}

String time_format_time(time_t time, bool local_time)
{
  struct tm timeinfo;
  if(local_time) {
    localtime_r(&time, &timeinfo);
  } else {
    gmtime_r(&time, &timeinfo);
  }
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
  char local_time[64];

  struct timeval time_now;
  gettimeofday(&time_now, NULL);

  struct tm timeinfo;
  gmtime_r(&time_now.tv_sec, &timeinfo);
  strftime(time, sizeof(time), "%FT%TZ", &timeinfo);
  localtime_r(&time_now.tv_sec, &timeinfo);
  strftime(local_time, sizeof(local_time), "%FT%T%z", &timeinfo);
  strftime(offset, sizeof(offset), "%z", &timeinfo);

  doc["time"] = time;
  doc["local_time"] = local_time;
  doc["offset"] = offset;
  doc["uptime"] = uptimeMillis() / 1000;
}
