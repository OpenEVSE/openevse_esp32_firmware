#ifndef _OPENEVSE_TIME_H
#define _OPENEVSE_TIME_H

#include <MongooseSntpClient.h>
#include <MicroTasks.h>
#include <MicroTasksTask.h>
#include <ArduinoJson.h>
#ifndef EPOXY_DUINO
#include <lwip/ip_addr.h>
#endif

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

    // The NTP host is resolved asynchronously, because the synchronous resolvers
    // block for as long as the query takes and loop() runs under the task
    // watchdog. dnsFoundCallback() runs on the LwIP TCP/IP thread and hands the
    // answer to loop() through these fields.
    char   _dnsResult[46];                 // written by dnsFoundCallback() only
    volatile bool _dnsResultReady = false; // release/acquire flag for _dnsResult
    unsigned long _dnsDeadline = 0;        // 0 when no lookup is outstanding

    void startDnsLookup();
    void takeDnsResult();
#ifndef EPOXY_DUINO
    static void dnsFoundCallback(const char *name, const ip_addr_t *ipaddr, void *arg);
#endif

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
      // Discard any answer still in flight, so it cannot land on the new badge
      __atomic_store_n(&_dnsResultReady, false, __ATOMIC_RELAXED);
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

extern String time_format_time(time_t time, bool local_time = true);
extern String time_format_time(tm &time);

#endif // _OPENEVSE_TIME_H
