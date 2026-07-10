#ifndef _OPENEVSE_TIME_H
#define _OPENEVSE_TIME_H

#include <MongooseSntpClient.h>
#include <MicroTasks.h>
#include <MicroTasksTask.h>
#include <ArduinoJson.h>

// How long to wait for an in-flight SNTP reply before treating it as lost
#ifndef SNTP_FETCH_TIMEOUT
#define SNTP_FETCH_TIMEOUT (30 * 1000UL)
#endif

class TimeManager : public MicroTasks::Task
{
  private:
    const char *_timeHost;
    MongooseSntpClient _sntp;
    unsigned long _nextCheckTime;
    unsigned long _fetchStartTime;  // when current fetch was started
    uint8_t       _retryCount;      // consecutive failures; reset on success
    bool _fetchingTime;
    bool _setTheTime;
    bool _sntpEnabled;
    time_t _lastSyncTime;           // Unix timestamp of last successful sync
    char   _resolvedIp[46];         // last resolved IP, "failed", or ""
    bool   _syncRequested;          // set by checkNow(); shows "connecting" before fetch starts

    unsigned long retryDelay();     // exponential back-off based on _retryCount

    class TimeChange : public MicroTasks::Event
    {
      public:
        void Trigger() {
          Event::Trigger(false);
        }
    } _timeChange;

    double diffTime(timeval tv1, timeval tv2);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    TimeManager();

    void begin();

    void setHost(const char *host);
    void setTime(struct timeval setTime, const char *source);
    bool setTimeZone(String tz);

    bool isSntpEnabled() {
      return _sntpEnabled;
    }
    void setSntpEnabled(bool enabled);

    // Force an immediate sync attempt, clearing any stuck/backoff state
    void checkNow() {
      _fetchingTime  = false;
      _retryCount    = 0;
      _syncRequested = true;          // show "connecting" immediately in the UI
      _resolvedIp[0] = '\0';          // drop stale DNS badge
      _nextCheckTime = millis();
      MicroTask.wakeTask(this);
    }

    // NTP status accessors (used by GET /time)
    const char *getNtpStatus();
    time_t      getLastSyncTime()  { return _lastSyncTime; }
    int32_t     getNextSyncMs();
    const char *getResolvedIp()    { return _resolvedIp; }

    // Register for events
    void onTimeChange(MicroTasks::EventListener *listner) {
      _timeChange.Register(listner);
    }

    void serialise(JsonDocument &doc);
};

extern TimeManager timeManager;

extern void time_set_time(struct timeval set_time, const char *source);

// Convert the raw time_t returned by OpenEVSE.getTime() to the actual UTC epoch.
//
// The EVSE stores time as UTC calendar fields (year/month/day/hour/min/sec).
// However the library's getTime() reconstructs a time_t by calling mktime(),
// which treats those fields as *local* time, introducing a timezone offset error.
// This function undoes that offset so callers always work with true UTC.
//
// The compensation is computed using only evse_time itself as the reference,
// so it is self-consistent even on first boot before the WiFi clock is set.
extern time_t evse_time_to_utc(time_t evse_time);

extern String time_format_time(time_t time, bool local_time = true);
extern String time_format_time(tm &time);

#endif // _OPENEVSE_TIME_H
