#include <Arduino.h>
#include <MicroDebug.h>
#include <MongooseCore.h>
#include <MongooseSntpClient.h>

#include "sntp.h"
#include "net_manager.h"
#include "openevse.h"
#include "app_config.h"
#include "event.h"

static const char *time_host;
static MongooseSntpClient sntp;

static unsigned long next_time = 0;
static bool fetching_time = false;
static bool set_the_time = false;

#ifndef SNTP_POLL_TIME
// Check the time every 8 hours
#define SNTP_POLL_TIME 8 * 60 * 60 * 1000
//#define SNTP_POLL_TIME 10 * 1000
#endif

static double diff_time(timeval tv1, timeval tv2)
{
    double t1 = (double) tv1.tv_sec + (((double) tv1.tv_usec) / 1000000.0);
    double t2 = (double) tv2.tv_sec + (((double) tv2.tv_usec) / 1000000.0);

    return t1-t2;
}

void sntp_begin(const char *host)
{
  time_host = host;

  sntp.onError([](uint8_t err) {
    DBUGF("Got error %u", err);
    fetching_time = false;
    next_time = millis() + 10 * 1000;
  });

  sntp_check_now();
}

void sntp_check_now()
{
  next_time = millis();
}

void sntp_loop()
{
  if(config_sntp_enabled() &&
     net_is_connected() &&
     false == fetching_time &&
     millis() >= next_time)
  {
    fetching_time = true;

    DBUGF("Trying to get time from %s", time_host);
    sntp.getTime(time_host, [](struct timeval set_time) 
    {
      sntp_set_time(set_time, time_host);

      fetching_time = false;
      next_time = millis() + SNTP_POLL_TIME;
    });
  }

  if(set_the_time)
  {
    struct timeval utc_time;
    gettimeofday(&utc_time, NULL);

    if(utc_time.tv_usec >= 999500)
    {
      set_the_time = false;

      tm local_time, gm_time;
      localtime_r(&utc_time.tv_sec, &local_time);
      gmtime_r(&utc_time.tv_sec, &gm_time);
      DBUGF("Setting the time on the EVSE, Local: %s, UTC, %s", 
        sntp_format_time(local_time).c_str(),
        sntp_format_time(gm_time).c_str());
      OpenEVSE.setTime(local_time, [](int ret) 
      {
        DBUGF("EVSE time %sset", RAPI_RESPONSE_OK == ret ? "" : "not ");
        if(RAPI_RESPONSE_OK == ret) 
        {
          // Event the time change
          struct timeval esp_time;
          gettimeofday(&esp_time, NULL);

          struct tm timeinfo;
          localtime_r(&esp_time.tv_sec, &timeinfo);

          char time[64];
          char offset[8];
          strftime(time, sizeof(time), "%FT%TZ", &timeinfo);
          strftime(offset, sizeof(offset), "%z", &timeinfo);

          String event = F("{\"time\":\"");
          event += time;
          event += F(",\"offset\":\"");
          event += offset;
          event += F("\"}");
          event_send(event);
        }
      });
    }
  }
}

void sntp_set_time(struct timeval set_time, const char *source)
{
  timeval local_time;

  // Set the local time
  gettimeofday(&local_time, NULL);
  DBUGF("Local time: %s", sntp_format_time(local_time.tv_sec).c_str());
  DBUGF("Time from %s: %s", source, sntp_format_time(set_time.tv_sec).c_str());
  DBUGF("Diff %.2f", diff_time(set_time, local_time));
  settimeofday(&set_time, NULL);

  // Set the time on the OpenEVSE, set from the local time as this could take several ms
  OpenEVSE.getTime([](int ret, time_t evse_time)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      time_t local_time = time(NULL);
      DBUGF("Local time: %s", sntp_format_time(local_time).c_str());
      DBUGF("Time from EVSE: %s", sntp_format_time(evse_time).c_str());
      time_t diff = local_time - evse_time;      
      DBUGF("Diff %ld", diff);

      if(diff != 0)
      {
        // The EVSE can only be set to second accuracy, actually set the time in the loop on 0 ms
        DBUGF("Time change required");
        set_the_time = true;
      }
    }
  });
}

String sntp_format_time(time_t time)
{
  struct tm timeinfo;
  localtime_r(&time, &timeinfo);
  return sntp_format_time(timeinfo);
}

String sntp_format_time(tm &time)
{
  //See http://www.cplusplus.com/reference/ctime/strftime/
  char output[80];
  strftime(output, 80, "%d-%b-%y, %H:%M:%S", &time);
  return String(output);
}
