#include <Arduino.h>
#include <MicroDebug.h>
#include <MongooseCore.h>
#include <MongooseSntpClient.h>

#include "sntp.h"
#include "net_manager.h"
#include "openevse.h"

static const char *time_host;
static MongooseSntpClient sntp;

static unsigned long next_time = 0;
static bool fetching_time = false;

static bool set_the_time = false;

// The OpenEVSE's clock is only accurate to 1 second
#ifndef SNTP_EVSE_THRESHOLD
#define SNTP_EVSE_THRESHOLD 1.100
#endif

#ifndef SNTP_POLL_TIME
// Check the time every 8 hours
// #define SNTP_POLL_TIME 8 * 60 * 60 * 1000
#define SNTP_POLL_TIME 10 * 1000
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

  next_time = millis();
}


void sntp_loop()
{
  if(time_host &&
     net_is_connected() &&
     false == fetching_time &&
     millis() >= next_time)
  {
    fetching_time = true;

    DBUGF("Trying to get time from %s", time_host);
    sntp.getTime(time_host, [](struct timeval server_time)
    {
      struct timeval local_time;

      // Set the local time
      gettimeofday(&local_time, NULL);
      DBUGF("Local time: %s", ctime(&local_time.tv_sec));
      DBUGF("Time from %s: %s", time_host, ctime(&server_time.tv_sec));
      DBUGF("Diff %.2f\n", diff_time(server_time, local_time));
      settimeofday(&server_time, NULL);

      fetching_time = false;
      next_time = millis() + SNTP_POLL_TIME;

      // Set the time on the OpenEVSE, set from the local time as this could take several ms
      OpenEVSE.getTime([](int ret, time_t evse_time)
      {
        if(RAPI_RESPONSE_OK == ret)
        {
          struct timeval local_time;
          gettimeofday(&local_time, NULL);

          DBUGF("Local time: %s", ctime(&local_time.tv_sec));
          DBUGF("Time from EVSE: %s", ctime(&evse_time));
          time_t diff = local_time.tv_sec - evse_time;
          DBUGF("Diff %ld", diff);

          if(diff != 0)
          {
            // The EVSE can only be set to second accuracy, actually set the time in the loop on 0 ms
            DBUGF("Time change required");
            set_the_time = true;
          }
        }
      });
    });
  }

  if(set_the_time)
  {
    set_the_time = false;

    struct timeval local_time;
    gettimeofday(&local_time, NULL);

    if(local_time.tv_usec > 5000)
    {
      DBUGF("Setting the time on the EVSE, %s", ctime(&local_time.tv_sec));
      OpenEVSE.setTime(local_time.tv_sec, [](int ret) {
        DBUGF("EVSE time %sset", RAPI_RESPONSE_OK == ret ? "" : "not ");
      });
    }
  }
}

