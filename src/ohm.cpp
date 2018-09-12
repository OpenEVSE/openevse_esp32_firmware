#include "emonesp.h"
#include "input.h"
#include "wifi.h"
#include "config.h"
#include "RapiSender.h"

#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

#include <Arduino.h>

#define ACTIVE_TAG_START  "<active>"
#define ACTIVE_TAG_END    "</active>"

//Server strings for Ohm Connect
const char *ohm_host = "login.ohmconnect.com";
const char *ohm_url = "/verify-ohm-hour/";
const int ohm_httpsPort = 443;
const char *ohm_fingerprint =
  "0C 53 16 B1 DE 52 CD 3E 57 C5 6C A9 45 A2 DD 0A 04 1A AD C6";
String ohm_hour = "NotConnected";
int evse_sleep = 0;

extern RapiSender rapiSender;

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
    WiFiClientSecure client;
    if (!client.connect(ohm_host, ohm_httpsPort)) {
      DBUGLN(F("ERROR Ohm Connect - connection failed"));
      return;
    }
    if (client.verify(ohm_fingerprint, ohm_host))
    {
      client.print(String("GET ") + ohm_url + ohm + " HTTP/1.1\r\n" +
                   "Host: " + ohm_host + "\r\n" +
                   "User-Agent: OpenEVSE\r\n" + "Connection: close\r\n\r\n");

      String line = client.readString();
      DBUGVAR(line);

      int active_start = line.indexOf(ACTIVE_TAG_START);
      int active_end = line.indexOf(ACTIVE_TAG_END);

      if(active_start > 0 && active_end > 0)
      {
        active_start += sizeof(ACTIVE_TAG_START) - 1;

        String new_ohm_hour = line.substring(active_start, active_end);
        DBUGVAR(new_ohm_hour);

        if(new_ohm_hour != ohm_hour)
        {
          ohm_hour = new_ohm_hour;
          if(ohm_hour == "True")
          {
            DBUGLN(F("Ohm Hour"));
            if (evse_sleep == 0) {
              evse_sleep = 1;
              if(0 == rapiSender.sendCmd(F("$FS"))) {
                DBUGLN(F("Charging Started"));
              }
            }
          }
          else
          {
            DBUGLN(F("It is not an Ohm Hour"));
            if (evse_sleep == 1) {
              evse_sleep = 0;
              if(0 == rapiSender.sendCmd(F("$FE"))) {
                DBUGLN(F("Charging Stopped"));
              }
            }
          }
        }
      }
    } else {
      DBUGLN(F("ERROR Ohm Connect - Certificate Invalid"));
    }
  }

  Profile_End(ohm_loop, 5);
}
