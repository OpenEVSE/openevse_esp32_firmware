#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_HTTP_UPATE)
#undef ENABLE_DEBUG
#endif

#include "http_update.h"
#include "lcd.h"
#include "debug.h"
#include "emonesp.h"
#include "web_server.h"
#include "ota_signing.h"
#include <MongooseHttpClient.h>
#include <Update.h>

MongooseHttpClient client;
static int lastPercent = -1;
static size_t update_total_size = 0;
static size_t update_position = 0;

struct HttpUpdateRequestState
{
  bool startedUpdate = false;
  bool updateComplete = false;
  bool responseComplete = false;
  bool redirected = false;
  bool errorReported = false;
};

// Case-insensitive check that `host` equals `suffix` or ends with ".suffix".
static bool host_matches(const String &host, const char *suffix)
{
  String s = host;
  s.toLowerCase();
  String suf = suffix;
  if(s == suf) {
    return true;
  }
  String dotted = "." + suf;
  return s.length() > dotted.length() &&
         s.endsWith(dotted);
}

bool http_update_url_allowed(const String &url)
{
  // Require HTTPS: prevents a plaintext-MITM'd fetch and enforces a trusted
  // transport to the release host.
  if(!url.startsWith("https://")) {
    return false;
  }

  // Extract the host: everything between "https://" and the next '/', '?' or ':'.
  int start = 8; // strlen("https://")
  int end = url.length();
  for(int i = start; i < url.length(); i++) {
    char c = url[i];
    if(c == '/' || c == '?' || c == ':') {
      end = i;
      break;
    }
  }
  // Strip any userinfo ("user@host") to avoid "github.com@evil.example" tricks.
  String authority = url.substring(start, end);
  int at = authority.lastIndexOf('@');
  String host = (at >= 0) ? authority.substring(at + 1) : authority;
  if(host.length() == 0) {
    return false;
  }

  // Allowlist: OpenEVSE release downloads live on github.com and are served
  // (after redirect) from the *.githubusercontent.com CDN. Edit here to permit
  // other trusted firmware hosts.
  return host_matches(host, "github.com") ||
         host_matches(host, "githubusercontent.com");
}

bool http_update_from_url(String url,
  std::function<void(size_t complete, size_t total)> progress,
  std::function<void(int)> success,
  std::function<void(int)> error)
{
  DBUGF("Update from URL: %s", url.c_str());

  // Enforced on the initial fetch and on every redirect target (this function
  // is re-entered for 30x Location), so firmware can only come from a trusted
  // origin regardless of where the redirect chain leads.
  if(!http_update_url_allowed(url)) {
    DEBUG_PORT.printf("OTA URL not allowed: %s\n", url.c_str());
    error(HTTP_UPDATE_ERROR_URL_NOT_ALLOWED);
    return false;
  }

  MongooseHttpClientRequest *request = client.beginRequest(url.c_str());
  if(request)
  {
    HttpUpdateRequestState *state = new HttpUpdateRequestState();
    if(!state)
    {
      delete request;
      error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
      return false;
    }

    request->setMethod(HTTP_GET);

    DBUGF("Trying to fetch firmware from %s", url.c_str());

    request->onBody([url,progress,success,error,request,state](MongooseHttpClientResponse *response)
    {
      DBUGF("Update onBody %d", response->respCode());
      if(state->updateComplete)
      {
        return;
      }

      if(response->respCode() == 200)
      {
        size_t total = response->contentLength();
        DBUGVAR(total);
        if(Update.isRunning() || http_update_start(url, total))
        {
          state->startedUpdate = true;
          uint8_t *data = (uint8_t *)response->body().c_str();
          size_t len = response->body().length();
          if(http_update_write(data, len))
          {
            progress(len, total);

            // With Mongoose's streaming client the final body chunk is
            // observable even when MG_EV_HTTP_REPLY is not delivered later.
            // Finalize as soon as every Content-Length byte is written.
            if(update_total_size > 0 && update_position == update_total_size)
            {
              DEBUG_PORT.printf("Update download complete: %zu bytes, finalizing\n", update_position);
              if(http_update_end(false))
              {
                state->updateComplete = true;
                success(HTTP_UPDATE_OK);
                restart_system();
              } else {
                state->errorReported = true;
                error(HTTP_UPDATE_ERROR_FAILED_TO_END_UPDATE);
              }
            }
            return;
          } else {
            state->errorReported = true;
            error(HTTP_UPDATE_ERROR_WRITE_FAILED);
          }
        } else {
          state->errorReported = true;
          error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
        }
      } else if (300 <= response->respCode() && response->respCode() < 400) {
        // handle 3xx redirects (later)
        return;
      } else {
        state->errorReported = true;
        error(response->respCode());
      }
      request->abort();
    });

    request->onResponse([progress, error, success, request,state](MongooseHttpClientResponse *response)
    {
      DBUGF("Update onResponse %d", response->respCode());
      if(301 == response->respCode() ||
         302 == response->respCode())
      {
        state->redirected = true;
        MongooseString location = response->headers("Location");
        DBUGVAR(location.toString());
        http_update_from_url(location.toString(), progress, success, error);
      } else if(200 == response->respCode()) {
        state->responseComplete = true;

        if(state->updateComplete)
        {
          return;
        }

        if(!state->startedUpdate || !Update.isRunning())
        {
          if(!state->errorReported)
          {
            state->errorReported = true;
            error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
          }
          return;
        }

        if(update_total_size > 0)
        {
          DEBUG_PORT.printf("Update incomplete: %zu/%zu bytes\n", update_position, update_total_size);
          Update.abort();
          state->errorReported = true;
          error(HTTP_UPDATE_ERROR_INCOMPLETE_DOWNLOAD);
          return;
        }

        // MG_EV_HTTP_REPLY means the complete HTTP response has been received.
        // Finalize here rather than waiting for a later socket-close callback.
        if(http_update_end(true))
        {
          state->updateComplete = true;
          success(HTTP_UPDATE_OK);
          restart_system();
        } else {
          state->errorReported = true;
          error(HTTP_UPDATE_ERROR_FAILED_TO_END_UPDATE);
        }
      }
    });

    request->onClose([error,state]()
    {
      DBUGLN("Update onClose");
      if(state->startedUpdate && !state->updateComplete && !state->responseComplete)
      {
        if(Update.isRunning())
        {
          Update.abort();
        }
        if(!state->errorReported)
        {
          error(HTTP_UPDATE_ERROR_INCOMPLETE_DOWNLOAD);
        }
      }
      else if(!state->redirected && !state->responseComplete && !state->errorReported)
      {
        error(HTTP_UPDATE_ERROR_INCOMPLETE_DOWNLOAD);
      }
      delete state;
    });

    client.send(request);
    return true;
  }

  error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
  return false;
}

bool http_update_start(String source, size_t total)
{
  update_position = 0;
  update_total_size = total;
  lastPercent = -1;

  // Signed-OTA builds must know the exact image size up front: the verifier
  // hashes (total - 512) bytes and treats the trailing 512 as the signature,
  // so an unknown Content-Length would hash the signature into the firmware and
  // always fail. Reject rather than silently mis-verify.
  if(ota_signing_required() && total == 0)
  {
    DEBUG_PORT.println(F("Signed OTA requires a known Content-Length; refusing update"));
    return false;
  }

  // Install the image-signature verifier before begin() (no-op unless this is a
  // signed-OTA build). If it is required but cannot be installed, fail closed.
  if(!ota_install_signature())
  {
    DEBUG_PORT.println(F("Failed to install OTA signature verifier; refusing update"));
    return false;
  }

  // Pass the expected size so the library can (a) reject an oversized binary
  // before erasing the target partition, and (b) validate completeness at end().
  // Fall back to UPDATE_SIZE_UNKNOWN only when Content-Length is absent.
  if(Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN))
  {
    DEBUG_PORT.printf("Update Start: %s %zu\n", source.c_str(), total);

    lcd.display(F("Updating WiFi"), 0, 0, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    StaticJsonDocument<128> event;
    event["ota"] = "started";
    web_server_event(event);
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
      int percent = (int)((update_position * 100) / update_total_size);
      percent = min(percent, 100);
      DBUGVAR(percent);
      DBUGVAR(lastPercent);
      if(percent != lastPercent)
      {
        String text = String(percent) + F("%");
        lcd.display(text, 0, 1, 10 * 1000, LCD_DISPLAY_NOW);

        DEBUG_PORT.printf("Update: %d%%\n", percent);

        StaticJsonDocument<128> event;
        event["ota_progress"] = percent;
        web_server_event(event);
        yield();
        lastPercent = percent;
      }
    }

    return true;
  }

  return false;
}

bool http_update_end(bool evenIfRemaining)
{
  DBUGLN("Upload finished");
  if(Update.end(evenIfRemaining))
  {
    DBUGF("Update Success: %u", update_position);
    lcd.display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    StaticJsonDocument<128> event;
    event["ota"] = "completed";
    web_server_event(event);
    yield();
    return true;
  } else {
    DEBUG_PORT.printf("Update failed: %d (%s)\n", Update.getError(), Update.errorString());
    StaticJsonDocument<128> event;
    event["ota"] = "failed";
    web_server_event(event);
    yield();
    lcd.display(F("Error"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  }

  return false;
}
