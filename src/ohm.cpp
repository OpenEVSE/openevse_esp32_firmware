#include <Arduino.h>
#include <MongooseString.h>
#include <MongooseHttpClient.h>

#include "emonesp.h"
#include "input.h"
#include "app_config.h"
#include "RapiSender.h"

#define ACTIVE_TAG_START  "<active>"
#define ACTIVE_TAG_END    "</active>"

#ifndef OHM_URL
#define OHM_URL "https://login.ohmconnect.com/verify-ohm-hour/";
#endif

String ohm_hour = "NotConnected";
int evse_sleep = 0;

extern RapiSender rapiSender;

static MongooseHttpClient client;

// -------------------------------------------------------------------
// Ohm Connect "Ohm Hour"
//
// Call every once every 60 seconds if connected to the WiFi and
// Ohm Key is set
// -------------------------------------------------------------------

void ohm_loop()
{
  Profile_Start(ohm_loop);

  if (ohm != 0)
  {
    String ohm_url = OHM_URL;
    ohm_url += ohm;
    DBUGVAR(ohm_url);
    client.get(ohm_url, [](MongooseHttpClientResponse *response)
    {
      String result = response->body().toString();
      DBUGVAR(result);

      int active_start = result.indexOf(ACTIVE_TAG_START);
      int active_end = result.indexOf(ACTIVE_TAG_END);

      if(active_start > 0 && active_end > 0)
      {
        active_start += sizeof(ACTIVE_TAG_START) - 1;

        String new_ohm_hour = result.substring(active_start, active_end);
        DBUGVAR(new_ohm_hour);

        if(new_ohm_hour != ohm_hour)
        {
          ohm_hour = new_ohm_hour;
          if(ohm_hour == "True")
          {
            DBUGLN(F("Ohm Hour"));
            if (evse_sleep == 0)
            {
              evse_sleep = 1;
              rapiSender.sendCmd(F("$FS"), [](int ret)
              {
                if(RAPI_RESPONSE_OK == ret) {
                  DBUGLN(F("Charge Stopped"));
                }
              });
            }
          }
          else
          {
            DBUGLN(F("It is not an Ohm Hour"));
            if (evse_sleep == 1)
            {
              evse_sleep = 0;
              rapiSender.sendCmd(F("$FE"), [](int ret)
              {
                if(RAPI_RESPONSE_OK == ret) {
                  DBUGLN(F("Charging enabled"));
                }
              });
            }
          }
        }
      }
    });
  }

  Profile_End(ohm_loop, 5);
}
