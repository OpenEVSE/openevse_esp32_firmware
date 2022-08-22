#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_HTTP_UPATE)
#undef ENABLE_DEBUG
#endif

#include "http_update.h"
#include "lcd.h"
#include "debug.h"
#include "emonesp.h"

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
  MongooseHttpClientRequest *request = client.beginRequest(url.c_str());
  if(request)
  {
    request->setMethod(HTTP_GET);

    request->onBody([url,progress,error,request](MongooseHttpClientResponse *response)
    {
      if(response->respCode() == 200)
      {
        size_t total = response->contentLength();
        DBUGVAR(total);
        if(Update.isRunning() || http_update_start(url, total))
        {
          uint8_t *data = (uint8_t *)response->body().c_str();
          size_t len = response->body().length();
          if(http_update_write(data, len))
          {
            progress(len, total);
            return;
          } else {
            error(HTTP_UPDATE_ERROR_WRITE_FAILED);
          }
        } else {
          error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
        }
      } else {
        error(response->respCode());
      }
      request->abort();
    });

    request->onClose([success, error]()
    {
      if(http_update_end())
      {
        success(HTTP_UPDATE_OK);
        restart_system();
      } else {
        error(HTTP_UPDATE_ERROR_FAILED_TO_END_UPDATE);
      }
    });
    client.send(request);

    return true;
  }

  return false;
}

bool http_update_start(String source, size_t total)
{
  update_position = 0;
  update_total_size = total;
  if(Update.begin())
  {
    DEBUG_PORT.printf("Update Start: %s %zu\n", source.c_str(), total);

    lcd.display(F("Updating WiFi"), 0, 0, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);

    return true;
  }

  return false;
}

bool http_update_write(uint8_t *data, size_t len)
{
  // Issue #187 "HTTP update fails on ESP32 ethernet gateway" was caused by the
  // loop watchdog timing out, presumably because updating wasn't bottlenecked
  // by network and execution stayed in the Mongoose poll() method.
  feedLoopWDT();

  DBUGF("Update Writing %u, %u", update_position, len);
  size_t written = Update.write(data, len);
  DBUGVAR(written);
  if(written == len)
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
    DBUGF("Update Success: %u", update_position);
    lcd.display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    return true;
  } else {
    DBUGF("Update failed: %d", Update.getError());
    lcd.display(F("Error"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  }

  return false;
}
