#include "emonesp.h"
#include "ota.h"
#include "wifi.h"

#include <ArduinoOTA.h>               // local OTA update from Arduino IDE
#include <FS.h>

void ota_setup()
{
  // Start local OTA update server
  ArduinoOTA.setHostname(esp_hostname);
  ArduinoOTA.begin();

  ArduinoOTA.onStart([]() {
    // Clean SPIFFS
    SPIFFS.end();

    Serial.println("$FP 0 0 OpenEVSE WiFi...");
    delay(100);
    Serial.println("$FP 0 1 ................");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("$FP 0 1 %u%%\rzn", (progress / (total / 100)));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("$FP 0 1 Complete");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("$FP 0 1 Error[%u]\r\n", error);
  });
}

void ota_loop()
{
  ArduinoOTA.handle();
}
