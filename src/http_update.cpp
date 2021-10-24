#include "http_update.h"
#include "lcd.h"
#include "debug.h"

#include <MongooseHttpClient.h>
#include <Update.h>

MongooseHttpClient client;
static int lastPercent = -1;
static size_t update_total_size = 0;
static size_t update_position = 0;

bool http_update_from_url(String url,
  std::function<void(size_t complete, size_t total)> progress,
  std::function<void(int)> success,
  std::function<void(int)> error)
{
  MongooseHttpClientRequest *request =
    client.beginRequest(url.c_str())
    ->setMethod(HTTP_GET)
//    ->onBody([progress, error](const uint8_t *data, size_t len) {
//      if(http_update_write(data, len)) {
//        progress(len, update_total_size);
//      } else {
//        error(HTTP_UPDATE_ERROR_WRITE_FAILED);
//      }
//    }
    ->onResponse([url,error](MongooseHttpClientResponse *response)
    {
      if(response->respCode() == 200)
      {
        size_t total = response->contentLength();
        if(!http_update_start(url, total)) {
          error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
        }
      } else {
        error(response->respCode());
      }
    })
    ->onClose([success, error](MongooseHttpClientResponse *response) {
      if(http_update_end()) {
        success(HTTP_UPDATE_OK);
      } else {
        error(HTTP_UPDATE_ERROR_FAILED_TO_END_UPDATE);
      }
    });
  client.send(request);

  return true;
}

bool http_update_start(String source, size_t total)
{
  update_total_size = total;
  if(Update.begin())
  {
    DEBUG_PORT.printf("Update Start: %s\n", source.c_str());

    lcd.display(F("Updating WiFi"), 0, 0, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);

    return true;
  }

  return false;
}

bool http_update_write(uint8_t *data, size_t len)
{
  DBUGF("Update Writing %u", update_position);
  if(Update.write(data, len) == len)
  {
    update_position += len;
    if(update_total_size > 0)
    {
      int percent = update_position / (update_total_size / 100);
      DBUGVAR(percent);
      DBUGVAR(lastPercent);
      if(percent != lastPercent)
      {
        String text = String(percent) + F("%");
        lcd.display(text, 0, 1, 10 * 1000, LCD_DISPLAY_NOW);
        DEBUG_PORT.printf("Update: %d%%\n", percent);
        lastPercent = percent;
      }
    }

    return true;
  }

  return false;
}

bool http_update_end()
{
  DBUGLN("Upload finished");
  if(Update.end(true))
  {
    DBUGF("Update Success: %uB", update_position);
    lcd.display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  } else {
    DBUGF("Update failed: %d", Update.getError());
    lcd.display(F("Error"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  }

  return false;
}
