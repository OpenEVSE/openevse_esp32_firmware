#include "emonesp.h"
#include "ota.h"
#include "wifi.h"

#include <ArduinoOTA.h>               // local OTA update from Arduino IDE
#include <FS.h>

#include "RapiSender.h"

extern RapiSender rapiSender;

void ota_setup()
{
  // Start local OTA update server
  ArduinoOTA.setHostname(esp_hostname);
  ArduinoOTA.begin();

  ArduinoOTA.onStart([]() {
    // Clean SPIFFS
    SPIFFS.end();

    rapiSender.sendCmd(F("$FP 0 0 OpenEVSE WiFi..."));
    rapiSender.sendCmd(F("$FP 0 1 ................"));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    String command = F("$FP 0 1 ");
    command += (progress / (total / 100));
    command += F("%");
    rapiSender.sendCmd(command);
  });

  ArduinoOTA.onEnd([]() {
    rapiSender.sendCmd(F("$FP 0 1 Complete"));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String command = F("$FP 0 1 Error[");
    command += error;
    command += F("]");
    rapiSender.sendCmd(command);
  });
}

void ota_loop()
{
  ArduinoOTA.handle();
}
