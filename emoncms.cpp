#include "emonesp.h"
#include "emoncms.h"
#include "config.h"
#include "http.h"
#include "input.h"

#include <Arduino.h>

//EMONCMS SERVER strings

boolean emoncms_connected = false;

unsigned long packets_sent = 0;
unsigned long packets_success = 0;


void emoncms_publish(String url){
  if(emoncms_apikey != 0) {  
    DEBUG.println(url); delay(10);
    packets_sent++;
    // Send data to Emoncms server
    String result="";
    if (emoncms_fingerprint!=0){
      // HTTPS on port 443 if HTTPS fingerprint is present
      DEBUG.println("HTTPS Enabled"); delay(10);
      result = get_https(emoncms_fingerprint.c_str(), emoncms_server.c_str(), url, 443);
    } else {
      // Plain HTTP if other emoncms server e.g EmonPi
      DEBUG.println("Plain old HTTP"); delay(10);
      result = get_http(emoncms_server.c_str(), url);
    }
    if (result == "ok"){
      packets_success++;
      emoncms_connected = true;
    } else {
      emoncms_connected=false;
      DEBUG.print("Emoncms error: ");
      DEBUG.println(result);
    }
  }
}
