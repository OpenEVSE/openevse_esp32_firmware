#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_HTTP_UPATE)
#undef ENABLE_DEBUG
#endif

#include "http_update.h"
#include "lcd.h"
#include "debug.h"
#include "emonesp.h"
#include "web_server.h"
#include "ota_signing.h"
#include "ota_url_allow.h"
#include <MongooseHttpClient.h>
#include <Update.h>
#ifndef EPOXY_DUINO
#include <esp_heap_caps.h>
#endif

MongooseHttpClient client;
static int lastPercent = -1;
static size_t update_total_size = 0;
static size_t update_position = 0;

// --- Online-update heap budget ---------------------------------------------
// On the TFT build (~45 KB internal heap free) an online update only just fits:
// two TLS handshakes (~20 KB each, one after the other), a 16.9 KB TLS record
// buffer, and Update.begin()'s 4 KB flash-write buffer. The flag and the
// reservation below, together with the TLS buffer preallocation further down,
// make room for each piece before the download needs it.

// Update.begin() allocates its SPI_FLASH_SEC_SIZE write buffer only once the
// image starts to arrive, by which time the TLS connection owns most of the
// heap. Reserve a block of that size when the download connection is opened
// and free it immediately before Update.begin(), so the allocator has a block
// of exactly the right size to hand back.
#define HTTP_UPDATE_RESERVE_SIZE 4096
static void *updateReserve = nullptr;

static void update_reserve_take()
{
  if(nullptr == updateReserve) {
    updateReserve = malloc(HTTP_UPDATE_RESERVE_SIZE);
    if(nullptr == updateReserve) {
      DEBUG_PORT.println(F("OTA: could not reserve the update buffer"));
    }
  }
}

static void update_reserve_release()
{
  if(nullptr != updateReserve) {
    free(updateReserve);
    updateReserve = nullptr;
  }
}

// otaActive is set for the lifetime of an online update (start to failure; a
// success reboots). Services ask http_update_in_progress() and stand down while
// it is true. It reports false once nothing has happened for
// HTTP_UPDATE_IDLE_TIMEOUT_MS, so a download that dies without an error
// callback cannot keep MQTT down forever. If that same download later resumes,
// its next write refreshes the activity time and services stand down again.
// A local upload never sets otaActive, so its writes do not affect this.
#define HTTP_UPDATE_IDLE_TIMEOUT_MS (2 * 60 * 1000UL)
static bool otaActive = false;
static unsigned long otaLastActivity = 0;
static bool otaIdleReported = false;

static void ota_activity()
{
  otaLastActivity = millis();
  otaIdleReported = false;
}

bool http_update_in_progress()
{
  if(!otaActive) {
    return false;
  }
  if(millis() - otaLastActivity > HTTP_UPDATE_IDLE_TIMEOUT_MS) {
    if(!otaIdleReported) {
      otaIdleReported = true;
      // The reservation is only useful while the download is moving; give it
      // back so the resumed services have it.
      update_reserve_release();
      DEBUG_PORT.println(F("OTA: no progress for 2 minutes, releasing services"));
    }
    return false;
  }
  return true;
}

static void ota_finished()
{
  update_reserve_release();
  otaActive = false;
}

struct HttpUpdateRequestState
{
  bool startedUpdate = false;
  bool updateComplete = false;
  bool responseComplete = false;
  bool redirected = false;
  bool errorReported = false;
  int redirects = 0;
  String sourceUrl;
  String redirectUrl;
  std::function<void(size_t complete, size_t total)> progress;
  std::function<void(int)> success;
  std::function<void(int)> error;
};

// A GitHub release download redirects from github.com to its release-assets
// CDN. Do not start that second HTTPS request from the first request's
// onResponse callback: Mongoose has not freed the first TLS context yet and a
// second context can exhaust the ESP32 heap. Queue it until after the current
// Mongoose.poll() returns instead.
static HttpUpdateRequestState *pendingRedirect = nullptr;

// Mongoose 7's built-in TLS verifies the certificate chain in software on
// loopTask. GitHub's chain has a P-384 intermediate, and checking that
// signature takes longer than the 5 s task watchdog, so the unit reset in the
// middle of every GitHub download (#1359). Take loopTask off the watchdog
// from sending the request until the first response event, i.e. for the
// handshake only, and put it back as soon as data starts to flow.
static bool otaWdtSuspended = false;
static unsigned long otaWdtSuspendedAt = 0;

static void ota_wdt_suspend()
{
  if(!otaWdtSuspended) {
    disableLoopWDT();
    otaWdtSuspended = true;
    otaWdtSuspendedAt = millis();
  }
}

static void ota_wdt_resume()
{
  if(otaWdtSuspended) {
    enableLoopWDT();
    otaWdtSuspended = false;
    DEBUG_PORT.printf("OTA: TLS handshake took %lu ms\n", millis() - otaWdtSuspendedAt);
  }
}

// Mongoose's built-in TLS decrypts only whole records, so it buffers each
// encrypted record in c->rtls (it reads until 16 KB + 40 bytes are in hand).
// Left to itself it grows rtls by MG_IO_SIZE (512 B) at a time, and every step
// allocates the new size before freeing the old one. On a heap-tight build
// (openevse_wifi_tft_v1 has ~45 KB free) that walk fails a few KB in -- after
// the handshake has fragmented the heap -- with "mg_iobuf_resize 5632->6144 /
// OOM", and the download dies on its first 16 KB record (HTTP OTA failed: -4).
// Allocating the full record buffer once, before the handshake, while the
// large free block still exists, avoids both the growth and the copies.
// Size: a TLS 1.3 record is at most 5 + 16384 + 256 bytes; mg_iobuf_resize
// rounds up to the 512 B alignment, giving 16896.
#define HTTP_UPDATE_TLS_RECV_BUF (5 + 16 * 1024 + 256)

static void preallocTlsRecvBuffer(MongooseHttpClientRequest *request)
{
  // send() has just created the connection with mg_http_connect(); the request
  // holds it. Do not take the head of the manager's list instead: the first
  // connect also creates Mongoose's DNS client connection, which is linked
  // ahead of ours. Only touch it when fn_data confirms it is ours.
  struct mg_connection *nc = request->getConnection();
  if(nullptr == nc || nc->fn_data != (void *)request) {
    DEBUG_PORT.println(F("OTA: download connection not found, TLS buffer not preallocated"));
    return;
  }
  if(nc->rtls.size >= HTTP_UPDATE_TLS_RECV_BUF) {
    return;
  }
  if(!mg_iobuf_resize(&nc->rtls, HTTP_UPDATE_TLS_RECV_BUF)) {
    // Not fatal: Mongoose falls back to growing the buffer on demand.
#ifndef EPOXY_DUINO
    DEBUG_PORT.printf("OTA: could not preallocate %d B TLS buffer (largest free block %u B)\n",
      HTTP_UPDATE_TLS_RECV_BUF, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#else
    DEBUG_PORT.printf("OTA: could not preallocate %d B TLS buffer\n", HTTP_UPDATE_TLS_RECV_BUF);
#endif
  }
}

static bool isRedirectStatus(int status)
{
  return status == 301 || status == 302 || status == 303 ||
         status == 307 || status == 308;
}

bool http_update_url_allowed(const String &url)
{
  // Delegates to the pure, unit-tested host-allowlist check (ota_url_allow.cpp).
  return ota_url_host_allowed(url.c_str());
}

bool http_update_from_url(String url,
  std::function<void(size_t complete, size_t total)> progress,
  std::function<void(int)> success,
  std::function<void(int)> error,
  int redirects)
{
  DBUGF("Update from URL: %s", url.c_str());

  if(0 == redirects) {
    // Only one online update at a time. Refuse before touching any shared
    // state: a second start would otherwise reset the flag and, through its
    // own error path, free the running update's reservation. Redirect hops of
    // the running update arrive with redirects > 0 and are not affected.
    // Not reported through `error`: that would emit an "ota: failed" event to
    // the web UI while the first update is still running.
    if(http_update_in_progress()) {
      DEBUG_PORT.println(F("OTA: an update is already in progress"));
      return false;
    }

    // A new update: services stand down until it fails (a success reboots).
    // Every error path below, and on each redirect hop, goes through this
    // wrapper, which is handed on to the hops with the rest of the callbacks.
    otaActive = true;
    ota_activity();
    auto reportError = error;
    error = [reportError](int code) {
      ota_finished();
      reportError(code);
    };
  } else {
    ota_activity();
  }

  // Enforced on the initial fetch and on every redirect target (this function
  // is re-entered for 30x Location), so firmware can only come from a trusted
  // origin regardless of where the redirect chain leads.
  if(!http_update_url_allowed(url)) {
    DEBUG_PORT.printf("OTA URL not allowed: %s\n", url.c_str());
    error(HTTP_UPDATE_ERROR_URL_NOT_ALLOWED);
    return false;
  }

  if(redirects > HTTP_UPDATE_MAX_REDIRECTS) {
    DEBUG_PORT.printf("OTA too many redirects: %s\n", url.c_str());
    error(HTTP_UPDATE_ERROR_TOO_MANY_REDIRECTS);
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
    state->sourceUrl = url;
    state->redirects = redirects;
    state->progress = progress;
    state->success = success;
    state->error = error;

    request->setMethod(HTTP_GET);
    // Hand the image over chunk by chunk; the client would otherwise buffer the
    // whole download in RAM, which an ESP32 cannot do.
    request->setStreaming();

    DBUGF("Trying to fetch firmware from %s", url.c_str());

    request->onBody([request,state](MongooseHttpClientResponse *response)
    {
      DBUGF("Update onBody %d", response->respCode());
      ota_wdt_resume();
      if(state->updateComplete)
      {
        return;
      }

      if(response->respCode() == 200)
      {
        size_t total = response->contentLength();
        DBUGVAR(total);
#if defined(EPOXY_DUINO)
        // Host builds record the URL the update came from (see EpoxyUpdate)
        EpoxyUpdateSetSource(state->sourceUrl.c_str());
#endif
        if(Update.isRunning() || http_update_start(state->sourceUrl, total))
        {
          state->startedUpdate = true;
          uint8_t *data = (uint8_t *)response->body().c_str();
          size_t len = response->body().length();
          if(http_update_write(data, len))
          {
            state->progress(len, total);

            // With Mongoose's streaming client the final body chunk is
            // observable even when MG_EV_HTTP_REPLY is not delivered later.
            // Finalize as soon as every Content-Length byte is written.
            if(update_total_size > 0 && update_position == update_total_size)
            {
              DEBUG_PORT.printf("Update download complete: %zu bytes, finalizing\n", update_position);
              if(http_update_end(false))
              {
                state->updateComplete = true;
                state->success(HTTP_UPDATE_OK);
                restart_system();
              } else {
                state->errorReported = true;
                state->error(HTTP_UPDATE_ERROR_FAILED_TO_END_UPDATE);
              }
            }
            return;
          } else {
            state->errorReported = true;
            state->error(HTTP_UPDATE_ERROR_WRITE_FAILED);
          }
        } else {
          state->errorReported = true;
          state->error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
        }
      } else if(isRedirectStatus(response->respCode())) {
        // handle 3xx redirects (later)
        return;
      } else {
        state->errorReported = true;
        state->error(response->respCode());
      }
      request->abort();
    });

    request->onResponse([request,state](MongooseHttpClientResponse *response)
    {
      DBUGF("Update onResponse %d", response->respCode());
      ota_wdt_resume();
      if(isRedirectStatus(response->respCode()))
      {
        state->redirected = true;
        MongooseString location = response->headers("Location");
        DBUGVAR(location.toString());
        state->redirectUrl = location.toString();
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
            state->error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
          }
          return;
        }

        if(update_total_size > 0)
        {
          DEBUG_PORT.printf("Update incomplete: %zu/%zu bytes\n", update_position, update_total_size);
          Update.abort();
          state->errorReported = true;
          state->error(HTTP_UPDATE_ERROR_INCOMPLETE_DOWNLOAD);
          return;
        }

        // MG_EV_HTTP_REPLY means the complete HTTP response has been received.
        // Finalize here rather than waiting for a later socket-close callback.
        if(http_update_end(true))
        {
          state->updateComplete = true;
          state->success(HTTP_UPDATE_OK);
          restart_system();
        } else {
          state->errorReported = true;
          state->error(HTTP_UPDATE_ERROR_FAILED_TO_END_UPDATE);
        }
      }
    });

    request->onClose([state]()
    {
      DBUGLN("Update onClose");
      ota_wdt_resume();
      if(state->redirected)
      {
        if(pendingRedirect)
        {
          state->errorReported = true;
          state->error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
        }
        else
        {
          pendingRedirect = state;
          return;
        }
      }
      else if(state->startedUpdate && !state->updateComplete && !state->responseComplete)
      {
        if(Update.isRunning())
        {
          Update.abort();
        }
        if(!state->errorReported)
        {
          state->error(HTTP_UPDATE_ERROR_INCOMPLETE_DOWNLOAD);
        }
      }
      else if(!state->redirected && !state->responseComplete && !state->errorReported)
      {
        state->error(HTTP_UPDATE_ERROR_INCOMPLETE_DOWNLOAD);
      }
      delete state;
    });

    ota_wdt_suspend();
    // Only on the hop expected to carry the image. A github.com release URL
    // always answers with a 302 to its asset CDN; preallocating there leaves
    // too little heap for the redirect's ~5 KB of response headers (measured:
    // "mg_iobuf_resize 5120->5632 / OOM" on the TFT build). The first hop's
    // connection is freed before the redirect is followed, so the CDN hop
    // gets the memory back.
    bool expectRedirect = 0 == redirects &&
      (url.startsWith("https://github.com/") || url.startsWith("https://www.github.com/"));
    if(!request->send()) {
      // No connection was created, so no onClose will ever run to clean up
      // or report: do it here, through the (wrapped) error path so services
      // resume at once rather than after the idle timeout.
      ota_wdt_resume();
      delete state;
      delete request;
      error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
      return false;
    }
    if(url.startsWith("https://") && !expectRedirect) {
      preallocTlsRecvBuffer(request);
      update_reserve_take();
    }
    return true;
  }

  error(HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE);
  return false;
}

void http_update_loop()
{
  if(!pendingRedirect)
  {
    return;
  }

  // Clear the queue before starting the request. If it immediately fails, its
  // error callback is still valid; if it later redirects, the slot is free for
  // the next hop.
  HttpUpdateRequestState *redirect = pendingRedirect;
  pendingRedirect = nullptr;

  String url = redirect->redirectUrl;
  auto progress = redirect->progress;
  auto success = redirect->success;
  auto error = redirect->error;
  int redirects = redirect->redirects + 1;
  delete redirect;

  http_update_from_url(url, progress, success, error, redirects);
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
  // Hand back the online update's reserved block just before Update.begin()
  // allocates its write buffer of the same size (no-op for a local upload).
  update_reserve_release();
  if(Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN))
  {
    DEBUG_PORT.printf("Update Start: %s %zu\n", source.c_str(), total);

    lcd.display(F("Updating WiFi"), 0, 0, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    JsonDocument event;
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
  ota_activity();

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

        JsonDocument event;
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
    JsonDocument event;
    event["ota"] = "completed";
    web_server_event(event);
    yield();
    return true;
  } else {
    DEBUG_PORT.printf("Update failed: %d (%s)\n", Update.getError(), Update.errorString());
    JsonDocument event;
    event["ota"] = "failed";
    web_server_event(event);
    yield();
    lcd.display(F("Error"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
  }

  return false;
}
