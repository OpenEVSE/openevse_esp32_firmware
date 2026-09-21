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

// Consecutive failures against the DHCP-supplied server before falling back to
// the configured host for the rest of the poll cycle
#ifndef SNTP_DHCP_FALLBACK_AFTER
#define SNTP_DHCP_FALLBACK_AFTER 2
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

    // NTP server learnt from DHCP (option 42), preferred over _timeHost while
    // sntp_dhcp is on. If it stops answering we fall back to the configured
    // host until the next scheduled poll, which tries DHCP again.
    char   _dhcpHost[16];           // dotted IPv4, "" when DHCP offered none
    bool   _dhcpEnabled;            // config sntp_dhcp
    bool   _dhcpFailedOver;         // DHCP server unresponsive this cycle
    const char *_activeHost;        // host the in-flight / last fetch targeted

    unsigned long retryDelay();     // exponential back-off based on _retryCount
    const char *pickHost();         // DHCP server if usable, else _timeHost
    void fetchFailed();             // shared failure path: back off or fall back
    bool resolveActiveHost();       // populate _resolvedIp from _activeHost; false if unresolved

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
    void setDhcpServer(const char *ip);   // NULL or "" clears
    void setDhcpEnabled(bool enabled);
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
      _dhcpFailedOver = false;        // a fresh start tries the DHCP server first again
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
    const char *getDhcpServer()    { return _dhcpHost; }
    // Host the next/current fetch goes to, and whether that is the DHCP one
    const char *getActiveHost()    { return pickHost(); }
    bool        isUsingDhcpServer() { return pickHost() == _dhcpHost; }

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
