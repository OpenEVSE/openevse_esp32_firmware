#include "emonesp.h"
#include "http.h"

#include <WiFiClientSecure.h>   // Secure https GET request
#include <ESP8266HTTPClient.h>

WiFiClientSecure client;        // Create class for HTTPS TCP connections get_https()
HTTPClient http;                // Create class for HTTP TCP connections get_http()

// -------------------------------------------------------------------
// HTTPS SECURE GET Request
// url: N/A
// -------------------------------------------------------------------

String
get_https(const char *fingerprint, const char *host, String url,
          int httpsPort) {
  // Use WiFiClient class to create TCP connections
  if (!client.connect(host, httpsPort)) {
    DEBUG.print(host + httpsPort);      //debug
    return ("Connection error");
  }
  if (client.verify(fingerprint, host)) {
    client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host +
                 "\r\n" + "Connection: close\r\n\r\n");
    // Handle wait for reply and timeout
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        client.stop();
        return ("Client Timeout");
      }
    }
    // Handle message receive
    while (client.available()) {
      String line = client.readStringUntil('\r');
      DEBUG.println(line);      //debug
      if (line.startsWith("HTTP/1.1 200 OK")) {
        return ("ok");
      }
    }
  } else {
    return ("HTTPS fingerprint no match");
  }
  return ("error " + String(host));
}

// -------------------------------------------------------------------
// HTTP GET Request
// url: N/A
// -------------------------------------------------------------------
String
get_http(const char *host, String url) {
  http.begin(String("http://") + host + String(url));
  int httpCode = http.GET();
  if ((httpCode > 0) && (httpCode == HTTP_CODE_OK)) {
    String payload = http.getString();
    DEBUG.println(payload);
    http.end();
    return (payload);
  } else {
    http.end();
    return ("server error: " + String(httpCode));
  }
}                               // end http_get
