#include "emonesp.h"
#include "ota.h"

#include <WiFiUdp.h>
#include <ArduinoOTA.h> // local OTA update from Arduino IDE
#include <FS.h>

#include "lcd.h"
#include "app_config.h"

static int lastPercent = -1;

void ota_setup()
{
  // Start local OTA update server
  ArduinoOTA.setHostname(esp_hostname.c_str());
  ArduinoOTA.begin();

  ArduinoOTA.onStart([]() {
    // Clean SPIFFS
    //SPIFFS.end();
    DBUGF("Starting ArduinoOTA update");
    lcd.display(F("Updating WiFi"), 0, 0, 10, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = progress / (total / 100);
    if (percent != lastPercent)
    {
      DBUGF("ArduinoOTA progress %d%%", percent);
      String text = String(percent) + F("%");
      lcd.display(text, 0, 1, 10 * 1000, LCD_DISPLAY_NOW);
      lastPercent = percent;
      feedLoopWDT();
    }
  });

  ArduinoOTA.onEnd([]() {
    DBUGF("ArduinoOTA finished");
    lcd.display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    DBUGF("ArduinoOTA error %d", error);
    String text = F("Error[");
    text += error;
    text += F("]");
    lcd.display(text, 0, 1, 5 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  });
}

void ota_loop()
{
  ArduinoOTA.handle();
}
