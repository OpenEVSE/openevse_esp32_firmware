#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_TIME)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <MongooseCore.h>
#ifdef EPOXY_DUINO
#include <netdb.h>
#else
#include <lwip/netdb.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#endif

#include "debug.h"
#include "time_man.h"
#include "input.h"
#include "net_manager.h"
#include "openevse.h"
#include "app_config.h"
#include "event.h"

#ifndef TIME_POLL_TIME
// Check the time every 8 hours
#define TIME_POLL_TIME 8 * 60 * 60 * 1000
//#define TIME_POLL_TIME 10 * 1000
#endif

// How often to look for an asynchronous DNS answer while one is outstanding.
// Only used while a lookup is in flight, so it does not affect the idle rate.
#define DNS_TAKE_POLL_TIME 250

// Give up waiting for a DNS callback after this long. LwIP always calls back,
// found or not, so this only stops a lost callback pinning loop() to the poll
// rate for the rest of the uptime.
#define DNS_LOOKUP_TIMEOUT (30 * 1000UL)

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
  _lastSyncTime(0),
  _syncRequested(false)
{
  _resolvedIp[0] = '\0';
  _dnsResult[0]  = '\0';
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

#ifndef EPOXY_DUINO
// Runs on the LwIP TCP/IP thread, not on loopTask. Formats the answer and then
// publishes the flag with a release store, so a loopTask that sees the flag set
// is guaranteed to see the whole buffer.
void TimeManager::dnsFoundCallback(const char *name, const ip_addr_t *ipaddr, void *arg)
{
  TimeManager *self = static_cast<TimeManager *>(arg);
  if(nullptr != ipaddr) {
    ipaddr_ntoa_r(ipaddr, self->_dnsResult, sizeof(self->_dnsResult));
  } else {
    // A null address means the query completed and the name does not resolve.
    strncpy(self->_dnsResult, "failed", sizeof(self->_dnsResult) - 1);
    self->_dnsResult[sizeof(self->_dnsResult) - 1] = '\0';
  }
  __atomic_store_n(&self->_dnsResultReady, true, __ATOMIC_RELEASE);
}
#endif

// Starts resolving _timeHost for the status display. An answer already in
// LwIP's cache lands in _resolvedIp before this returns; anything else arrives
// through dnsFoundCallback() and is collected by takeDnsResult(). Never blocks.
void TimeManager::startDnsLookup()
{
  if(nullptr == _timeHost) {
    return;
  }

#ifdef EPOXY_DUINO
  // Host build: no LwIP resolver and no watchdog to trip, so resolve inline.
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  if(getaddrinfo(_timeHost, nullptr, &hints, &res) == 0 && res) {
    void *addr = res->ai_family == AF_INET
      ? (void *)&((struct sockaddr_in  *)res->ai_addr)->sin_addr
      : (void *)&((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
    inet_ntop(res->ai_family, addr, _resolvedIp, sizeof(_resolvedIp) - 1);
    freeaddrinfo(res);
  } else {
    strncpy(_resolvedIp, "failed", sizeof(_resolvedIp) - 1);
  }
  _resolvedIp[sizeof(_resolvedIp) - 1] = '\0';
#else
  ip_addr_t addr;

  // Discard any answer still in flight from an earlier attempt
  __atomic_store_n(&_dnsResultReady, false, __ATOMIC_RELAXED);
  _dnsDeadline = 0;

  // dns_gethostbyname() is LwIP raw API, so it is only safe to call while
  // holding the TCP/IP core lock. LwIP copies the name, so it need not outlive
  // this call.
  LOCK_TCPIP_CORE();
  err_t err = dns_gethostbyname(_timeHost, &addr, dnsFoundCallback, this);
  UNLOCK_TCPIP_CORE();

  if(ERR_OK == err) {
    // Already in LwIP's cache: answered here, and no callback will follow.
    ipaddr_ntoa_r(&addr, _resolvedIp, sizeof(_resolvedIp));
    DBUGF("NTP: DNS cache hit, %s is %s", _timeHost, _resolvedIp);
  } else if(ERR_INPROGRESS == err) {
    // dnsFoundCallback() will hand the answer to takeDnsResult()
    _dnsDeadline = millis() + DNS_LOOKUP_TIMEOUT;
    if(0 == _dnsDeadline) {
      _dnsDeadline = 1;     // 0 is the "nothing outstanding" sentinel
    }
  } else {
    // Could not even start the query (no DNS server set, or the table is full)
    strncpy(_resolvedIp, "failed", sizeof(_resolvedIp) - 1);
    _resolvedIp[sizeof(_resolvedIp) - 1] = '\0';
  }
#endif
}

// Picks up an answer left by dnsFoundCallback(), if one has arrived.
void TimeManager::takeDnsResult()
{
#ifndef EPOXY_DUINO
  if(__atomic_load_n(&_dnsResultReady, __ATOMIC_ACQUIRE)) {
    __atomic_store_n(&_dnsResultReady, false, __ATOMIC_RELAXED);
    _dnsDeadline = 0;
    strncpy(_resolvedIp, _dnsResult, sizeof(_resolvedIp) - 1);
    _resolvedIp[sizeof(_resolvedIp) - 1] = '\0';
    DBUGF("NTP: DNS answer for %s is %s", _timeHost ? _timeHost : "", _resolvedIp);
  } else if(0 != _dnsDeadline && (long)(millis() - _dnsDeadline) >= 0) {
    // LwIP always calls back, found or not, so this should not happen. Stop
    // polling for it rather than holding loop() at the poll rate forever.
    DBUGLN("NTP: DNS callback never arrived");
    _dnsDeadline = 0;
  }
#endif
}

const char *TimeManager::getNtpStatus()
{
  if (!_sntpEnabled)                    return "disabled";
  if (_fetchingTime || _syncRequested)  return "connecting";
  if (_retryCount > 0)                  return "retry";
  if (_lastSyncTime > 0)                return "synchronized";
  return "waiting";
}

int32_t TimeManager::getNextSyncMs()
{
  if (!_sntpEnabled || _timeHost == NULL) return -1;
  if (_fetchingTime || _syncRequested)    return 0;   // pending/in-flight
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
  // Discard any answer in flight for the previous host
  __atomic_store_n(&_dnsResultReady, false, __ATOMIC_RELAXED);
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
    // Distinguish DNS failure from other NTP errors (firewall, bad server, etc.)
    // Only show "DNS failed" badge when DNS resolution itself fails.
    //
    // Resolve asynchronously. This handler runs because the name did not
    // resolve, so a synchronous lookup of that same name would take the full
    // retry path — A and AAAA, DNS_MAX_RETRIES against every configured
    // server — and outlast the task watchdog on loopTask.
    if(_timeHost) {
      startDnsLookup();
    } else {
      strncpy(_resolvedIp, "failed", sizeof(_resolvedIp) - 1);
      _resolvedIp[sizeof(_resolvedIp) - 1] = '\0';
    }
    unsigned long delay = retryDelay();
    _retryCount++;
    _nextCheckTime = millis() + delay;
    MicroTask.wakeTask(this);
  });
}

unsigned long TimeManager::loop(MicroTasks::WakeReason reason)
{
  // An asynchronous DNS answer may have landed since the last pass
  takeDnsResult();

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
      evse.getOpenEVSE().setTime(utc_time.tv_sec, [this](int ret)
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
      // Early DNS probe: populate _resolvedIp while the SNTP reply is still
      // pending so the UI shows DNS status without waiting for the full cycle.
      // Mongoose usually resolved the hostname before this fires, so the query
      // is answered from LwIP's cache — but when Mongoose's own resolve failed
      // the cache is empty, and a synchronous lookup would then block loopTask
      // past the task watchdog. Start it asynchronously instead.
      if(_resolvedIp[0] == '\0' && _timeHost) {
        startDnsLookup();
      }
      // Wake when the full watchdog deadline expires, or sooner to collect an
      // outstanding DNS answer
      unsigned long remaining = (unsigned long)(SNTP_FETCH_TIMEOUT - elapsed);
      return (0 != _dnsDeadline && remaining > DNS_TAKE_POLL_TIME)
        ? DNS_TAKE_POLL_TIME : remaining;
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
      _syncRequested = false;   // fetch is actually starting now
      _fetchingTime  = true;
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
        // Resolve hostname → IP for status display. The sync just succeeded so
        // the answer is in LwIP's cache and comes back from startDnsLookup()
        // immediately; going through the same path keeps this off the blocking
        // resolvers even if the cache entry has since expired.
        if(_timeHost) {
          startDnsLookup();
        }
        MicroTask.wakeTask(this);
      });

      if(started)
      {
        // Wake in 1 s for an early DNS probe while the SNTP request is
        // in-flight.  By then Mongoose will have resolved the hostname and
        // sent the UDP packet, so the lookup is answered from LwIP's cache
        // and we can show the DNS badge before the sync completes.
        ret = 1000;
      }
      else
      {
        // getTime() returned false — the MongooseSntpClient stale-_nc bug:
        // after the first successful sync MG_EV_CLOSE never fires for UDP,
        // so _nc stays non-NULL and every subsequent getTime() call returns
        // false without sending any traffic.  No DNS attempt was made, so
        // _resolvedIp is left untouched (already cleared by checkNow()) — the
        // UI will show no DNS badge.  Increment _retryCount so status shows
        // "retry" rather than falsely maintaining "synchronized".
        DBUGLN("NTP: getTime() could not start (stale connection?), treating as failure");
        _fetchingTime = false;
        unsigned long delay = retryDelay();
        _retryCount++;
        _nextCheckTime = millis() + delay;
        ret = delay;
      }
    } else {
      ret = delay > 0 ? (unsigned long)delay : 0;
    }
  }

  // While a DNS answer is outstanding, come back for it rather than sleeping
  // out a retry back-off that can be minutes long
  if(0 != _dnsDeadline && ret > DNS_TAKE_POLL_TIME) {
    ret = DNS_TAKE_POLL_TIME;
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
  evse.getOpenEVSE().getTime([this](int ret, time_t evse_time)
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
