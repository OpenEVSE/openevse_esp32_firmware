#include "emonesp.h"
#include "ota.h"
#include "wifi.h"

#include <ArduinoOTA.h>               // local OTA update from Arduino IDE
#include <FS.h>

#include "lcd.h"

static int lastPercent = -1;

void ota_setup()
{
  // Start local OTA update server
  ArduinoOTA.setHostname(esp_hostname);
  ArduinoOTA.begin();

  ArduinoOTA.onStart([]() {
    // Clean SPIFFS
    SPIFFS.end();

    lcd_display(F("Updating WiFi"), 0, 0, 0, LCD_CLEAR_LINE);
    lcd_display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE);
    lcd_loop();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = progress / (total / 100);
    if(percent != lastPercent) {
      String text = String(percent) + F("%");
      lcd_display(text, 0, 1, 10 * 1000, LCD_DISPLAY_NOW);
      lastPercent = percent;
    }
  });

  ArduinoOTA.onEnd([]() {
    lcd_display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String text = F("Error[");
    text += error;
    text += F("]");
    lcd_display(text, 0, 1, 5 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  });
}

void ota_loop()
{
  ArduinoOTA.handle();
}
