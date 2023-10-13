#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

#include "emonesp.h"
#include "web_server.h"
#include "web_server_static.h"
#include "app_config.h"
#include "net_manager.h"
#include "embedded_files.h"

extern bool enableCors; // defined in web_server.cpp

#include "web_static/web_server_static_files.h"

#define WEB_SERVER_INDEX_PAGE "index.html"

// Pages
static const char _HOME_PAGE[] PROGMEM = "/" WEB_SERVER_INDEX_PAGE;
#define HOME_PAGE FPSTR(_HOME_PAGE)

class StaticFileResponse: public MongooseHttpServerResponse
{
  private:
    StaticFile *_content;

  public:
    StaticFileResponse(int code, StaticFile *file);
};

static bool web_static_get_file(MongooseHttpServerRequest *request, StaticFile **file)
{
  // Remove the found uri
  String path = request->uri();
  if(path == "/") {
    path = String(
      HOME_PAGE);
  }

  return embedded_get_file(path, web_server_static_files, ARRAY_LENGTH(web_server_static_files), file);
}

bool web_static_handle(MongooseHttpServerRequest *request)
{
  dumpRequest(request);

  // Are we authenticated
  if(!net.isWifiModeApOnly() && www_username!="" &&
     false == request->authenticate(www_username, www_password)) {
    request->requestAuthentication(esp_hostname);
    return false;
  }

  StaticFile *file = NULL;
  if (web_static_get_file(request, &file))
  {
    MongooseHttpServerResponseBasic *response = request->beginResponse();

    response->addHeader(F("Cache-Control"), F("public, max-age=30, must-revalidate"));

    MongooseString ifNoneMatch = request->headers("If-None-Match");
    if(ifNoneMatch.equals(file->etag)) {
      request->send(304);
      return true;
    }

    response->setCode(200);
    response->setContentType(file->type);
    response->setContentLength(file->length);

    if (enableCors) {
      response->addHeader(F("Access-Control-Allow-Origin"), F("*"));
    }
    if(file->compressed) {
      response->addHeader(F("Content-Encoding"), F("gzip"));
    }

    response->addHeader("Etag", file->etag);
    response->setContent((const uint8_t *)file->data, file->length);

    request->send(response);

    return true;
  }

  return false;
}
