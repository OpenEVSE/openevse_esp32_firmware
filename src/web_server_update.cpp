#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <Update.h>

#include "emonesp.h"
#include "web_server.h"
#include "lcd.h"
#include "http_update.h"
#include "ota_signing.h"


static MongooseHttpServerResponseStream *upgradeResponse = NULL;
// Update.isFinished() is true while idle (0 bytes of 0), so remember the
// multipart request that actually completed instead.
static MongooseHttpServerRequest *completedUpdateRequest = NULL;

void handleUpdateFileUpload(MongooseHttpServerRequest *request)
{
  if(NULL != upgradeResponse) {
    request->send(500, CONTENT_TYPE_TEXT, "Error: Upgrade in progress");
    return;
  }

  if(false == requestPreProcess(request, upgradeResponse, CONTENT_TYPE_TEXT)) {
    return;
  }

  // A multipart Content-Length counts the boundaries too, so it is not the image
  // size: a signed build would hash the signature trailer into the image and
  // always fail. Signed builds take the raw body instead, where Content-Length
  // is exact. Rejected here, before any part is read.
  if(ota_signing_required() &&
     request->headers("Content-Type").toString().indexOf("boundary=") >= 0) {
    upgradeResponse->setCode(400);
    upgradeResponse->print(F("Error: signed firmware must be POSTed as a raw body, not multipart"));
    request->send(upgradeResponse);
    upgradeResponse = NULL;
    return;
  }

  completedUpdateRequest = NULL;

  // TODO: Add support for returning 100: Continue
}

// POST /update with a JSON body ({"url": ...}) asks the device to download the
// image itself. On Mongoose 7 every POST to an endpoint with an upload handler
// is streamed through handleUpdateUpload(), JSON included, and request->body()
// is never filled in -- so the JSON is collected there and handed to
// startUrlUpdate() once it has all arrived. Without this the JSON was written
// to flash as if it were the firmware image and failed at once (#1359).
#define URL_UPDATE_MAX_BODY 1024
static MongooseHttpServerRequest *urlUpdateRequest = NULL;
static MongooseHttpServerResponseStream *urlUpdateResponse = NULL;
static String urlUpdateBody;

static bool isJsonRequest(MongooseHttpServerRequest *request)
{
  MongooseString contentType = request->headers("Content-Type");
  return contentType.length() >= 16 &&
    0 == strncasecmp(contentType.c_str(), "application/json", 16);
}

static void startUrlUpdate(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, const String &body)
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if(DeserializationError::Code::Ok == error)
  {
    String url = doc["url"];
    if(http_update_from_url(url,
      [](size_t complete, size_t total) {},
      [](int) { },
      [](int errorCode) {
        DEBUG_PORT.printf("HTTP OTA failed: %d\n", errorCode);
        JsonDocument event;
        event["ota"] = "failed";
        event["ota_error"] = errorCode;
        web_server_event(event);
        yield();
      }))
    {
      response->setCode(200);
      response->print(F("{\"msg\":\"started\"}"));
    } else {
      response->setCode(500);
      response->print(F("{\"msg\":\"error\"}"));
    }
  }
  else
  {
    response->setCode(400);
    response->printf("{\"msg\":\"%s\"}", error.c_str());
  }

  request->send(response);
}

void handleUpdateFileFetch(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if(request->isUpload())
  {
    // The body has not arrived yet; handleUpdateUpload() collects it.
    if(NULL != urlUpdateRequest) {
      response->setCode(409);
      response->print(F("{\"msg\":\"busy\"}"));
      request->send(response);
      return;
    }
    urlUpdateRequest = request;
    urlUpdateResponse = response;
    urlUpdateBody = "";
    return;
  }

  startUrlUpdate(request, response, request->body().toString());
}

static size_t handleUrlUpdateBody(MongooseHttpServerRequest *request, int ev, uint8_t *data, size_t len)
{
  if(NULL == urlUpdateResponse) {
    return len;
  }

  if(len > 0)
  {
    if(urlUpdateBody.length() + len > URL_UPDATE_MAX_BODY)
    {
      urlUpdateResponse->setCode(413);
      urlUpdateResponse->print(F("{\"msg\":\"too large\"}"));
      request->send(urlUpdateResponse);
      urlUpdateResponse = NULL;
      return len;
    }
    urlUpdateBody.concat((const char *)data, len);
  }

  if(MG_EV_HTTP_PART_END == ev)
  {
    MongooseHttpServerResponseStream *response = urlUpdateResponse;
    urlUpdateResponse = NULL;
    startUrlUpdate(request, response, urlUpdateBody);
    urlUpdateBody = "";
  }

  return len;
}

void handleUpdateRequest(MongooseHttpServerRequest *request)
{
  if(HTTP_GET == request->method())
  {
    // This used to serve a bare multipart upload form. It had no way to work
    // for a browser authenticated by the session cookie -- the CSRF guard in
    // requestPreProcess() requires the X-Requested-With header on non-GET
    // requests, and a plain HTML form cannot set one. Uploading is the web
    // app's job; machine clients POST here directly.
    request->send(405, CONTENT_TYPE_TEXT, "POST firmware to this endpoint");
  }
  else if(HTTP_POST == request->method())
  {
    if(request->isUpload() && !isJsonRequest(request)) {
      handleUpdateFileUpload(request);
    } else {
      handleUpdateFileFetch(request);
    }
  }
}

static void handleUpdateError(MongooseHttpServerRequest *request)
{
  upgradeResponse->setCode(500);
  upgradeResponse->printf("Error: %d", Update.getError());
  request->send(upgradeResponse);
  upgradeResponse = NULL;

  // Anoyingly this uses Stream rather than Print...
#ifdef ENABLE_DEBUG
  Update.printError(DEBUG_PORT);
#endif
}

size_t handleUpdateUpload(MongooseHttpServerRequest *request, int ev, MongooseString filename, uint64_t index, uint8_t *data, size_t len)
{
  if(request == urlUpdateRequest) {
    return handleUrlUpdateBody(request, ev, data, len);
  }

  if(MG_EV_HTTP_PART_BEGIN == ev)
  {
//    dumpRequest(request);

    if(!http_update_start(filename, request->contentLength())) {
      handleUpdateError(request);
    }
  }

  // Only PART_DATA carries payload; PART_BEGIN and PART_END arrive with len 0.
  // A zero-length Update.write() is not a no-op: once the upload has delivered
  // exactly the size passed to Update.begin(), remaining() is 0, so the
  // `_bufferLen == remaining()` test in Updater.cpp fires on an empty buffer and
  // flushes it. _writeBuffer() then erases at the unaligned _progress, which
  // esp_partition_erase_range() rejects, aborting the update with
  // UPDATE_ERROR_ERASE after every byte has already been written.
  //
  // This only bites when the declared length matches the image exactly, which is
  // the raw-body upload path. A multipart Content-Length includes the boundary
  // overhead, so remaining() never reaches 0 and the bug stays hidden.
  if(len > 0 && !Update.hasError())
  {
    if(!http_update_write(data, len)) {
      handleUpdateError(request);
    }
  }

  if(MG_EV_HTTP_PART_END == ev)
  {
    if(http_update_end()) {
      completedUpdateRequest = request;
      upgradeResponse->setCode(200);
      upgradeResponse->print("OK");
      request->send(upgradeResponse);
      upgradeResponse = NULL;
    } else {
      handleUpdateError(request);
    }
  }

  return len;
}

void handleUpdateClose(MongooseHttpServerRequest *request)
{
  DBUGLN("Update close");

  if(request == urlUpdateRequest) {
    if(urlUpdateResponse) {
      delete urlUpdateResponse;
      urlUpdateResponse = NULL;
    }
    urlUpdateRequest = NULL;
    urlUpdateBody = "";
    return;
  }

  if(upgradeResponse) {
    delete upgradeResponse;
    upgradeResponse = NULL;
  }

  if(request == completedUpdateRequest) {
    completedUpdateRequest = NULL;
    restart_system();
  }
}
