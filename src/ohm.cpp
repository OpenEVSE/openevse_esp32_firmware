#include "emonesp.h"
#include "input.h"
#include "wifi.h"
#include "config.h"

#include <WiFiClientSecure.h>        
#include <ESP8266HTTPClient.h>

#include <Arduino.h>

//Server strings for Ohm Connect 
const char* ohm_host = "login.ohmconnect.com";
const char* ohm_url = "/verify-ohm-hour/";
const int ohm_httpsPort = 443;
const char* ohm_fingerprint = "0C 53 16 B1 DE 52 CD 3E 57 C5 6C A9 45 A2 DD 0A 04 1A AD C6";
String ohm_hour = "NotConnected";
int evse_sleep = 0;



// -------------------------------------------------------------------
// Ohm Connect "Ohm Hour"
//
// Call every once every 60 seconds if connected to the WiFi and 
// Ohm Key is set
// -------------------------------------------------------------------

void ohm_loop(){

if (ohm != 0){
  WiFiClientSecure client;
  if (!client.connect(ohm_host, ohm_httpsPort)) {
    DEBUG.println("ERROR Ohm Connect - connection failed");
    return;
  } 
  if (client.verify(ohm_fingerprint, ohm_host)) {
    client.print(String("GET ") + ohm_url + ohm + " HTTP/1.1\r\n" +
               "Host: " + ohm_host + "\r\n" +
               "User-Agent: OpenEVSE\r\n" +
               "Connection: close\r\n\r\n");
    String line = client.readString();
      if(line.indexOf("False") > 0) {
        DEBUG.println("It is not an Ohm Hour");
        ohm_hour = "False";
        if (evse_sleep == 1){
          evse_sleep = 0;
          Serial.println("$FE*AF");
        }
      }
      if(line.indexOf("True") > 0) {
        DEBUG.println("Ohm Hour");
        ohm_hour = "True";
        if (evse_sleep == 0){
          evse_sleep = 1;
          Serial.println("$FS*BD");
        }
      }
      DEBUG.println(line);
    } 
    else {
      DEBUG.println("ERROR Ohm Connect - Certificate Invalid");
    }
  }
}




