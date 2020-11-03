#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

#include "emonesp.h"
#include "web_server.h"
#include "web_server_static.h"
#include "app_config.h"
#include "net_manager.h"

extern bool enableCors; // defined in web_server.cpp

// Static files
struct StaticFile
{
  const char *filename;
  const char *data;
  size_t length;
  const char *type;
};

#include "web_static/web_server_static_files.h"

#define ARRAY_LENGTH(x) (sizeof(x)/sizeof((x)[0]))

#define IS_ALIGNED(x)   (0 == ((uint32_t)(x) & 0x3))

// Pages
static const char _HOME_PAGE[] PROGMEM = "/home.html";
#define HOME_PAGE FPSTR(_HOME_PAGE)

static const char _WIFI_PAGE[] PROGMEM = "/wifi_portal.html";
#define WIFI_PAGE FPSTR(_WIFI_PAGE)

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
    path = String(net_wifi_mode_is_ap_only() ? WIFI_PAGE : HOME_PAGE);
  }

  DBUGF("Looking for %s", path.c_str());

  for(int i = 0; i < ARRAY_LENGTH(staticFiles); i++) {
    if(path == staticFiles[i].filename)
    {
      DBUGF("Found %s %d@%p", staticFiles[i].filename, staticFiles[i].length, staticFiles[i].data);

      if(file) {
        *file = &staticFiles[i];
      }
      return true;
    }
  }

  return false;
}

bool web_static_handle(MongooseHttpServerRequest *request)
{
  dumpRequest(request);

  // Are we authenticated
  if(!net_wifi_mode_is_ap_only() && www_username!="" &&
     false == request->authenticate(www_username, www_password)) {
    request->requestAuthentication(esp_hostname);
    return false;
  }

  StaticFile *file = NULL;
  if (web_static_get_file(request, &file))
  {
    MongooseHttpServerResponseBasic *response = request->beginResponse();
    response->setCode(200);
    response->setContentType(file->type);
    response->setContentLength(file->length);

    if (enableCors)
    {
      response->addHeader(F("Access-Control-Allow-Origin"), F("*"));
    }

    response->setContent((const uint8_t *)file->data, file->length);

    request->send(response);

    return true;
  }

  return false;
}
