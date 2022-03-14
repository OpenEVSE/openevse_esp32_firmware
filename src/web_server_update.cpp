#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <Update.h>

#include "emonesp.h"
#include "web_server.h"
#include "lcd.h"
#include "http_update.h"


// -------------------------------------------------------------------
// Update firmware
// url: /update
// -------------------------------------------------------------------
static void handleUpdateGet(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_HTML)) {
    return;
  }

  response->setCode(200);
  response->print(
    F("<html><form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware'> "
        "<input type='submit' value='Update'>"
      "</form></html>"));
  request->send(response);
}

static MongooseHttpServerResponseStream *upgradeResponse = NULL;

void handleUpdateFileUpload(MongooseHttpServerRequest *request)
{
  if(NULL != upgradeResponse) {
    request->send(500, CONTENT_TYPE_TEXT, "Error: Upgrade in progress");
    return;
  }

  if(false == requestPreProcess(request, upgradeResponse, CONTENT_TYPE_TEXT)) {
    return;
  }

  // TODO: Add support for returning 100: Continue
}

void handleUpdateFileFetch(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  String body = request->body().toString();
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, body);
  if(DeserializationError::Code::Ok == error)
  {
    String url = doc["url"];
    if(http_update_from_url(url,
      [](size_t complete, size_t total) {},
      [](int) { },
      [](int) { }))
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
    response->printf("{\"msg\":\"%s\"", error.c_str());
  }

  request->send(response);
}

void handleUpdateRequest(MongooseHttpServerRequest *request)
{
  if(HTTP_GET == request->method())
  {
    handleUpdateGet(request);
  }
  else if(HTTP_POST == request->method())
  {
    if(request->isUpload()) {
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
  if(MG_EV_HTTP_PART_BEGIN == ev)
  {
//    dumpRequest(request);

    if(!http_update_start(filename, request->contentLength())) {
      handleUpdateError(request);
    }
  }

  if(!Update.hasError())
  {
    if(!http_update_write(data, len)) {
      handleUpdateError(request);
    }
  }

  if(MG_EV_HTTP_PART_END == ev)
  {
    if(http_update_end()) {
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

  if(upgradeResponse) {
    delete upgradeResponse;
    upgradeResponse = NULL;
  }

  if(Update.isFinished() && !Update.hasError()) {
    restart_system();
  }
}
