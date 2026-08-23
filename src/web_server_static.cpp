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
#include "http_etag.h"

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

  // The web UI shell (HTML/JS/CSS/assets) is served WITHOUT authentication, so
  // the single-page app can always load and present its own login page. It
  // carries no secrets: every piece of data and every action lives behind the
  // authenticated API and WebSocket (see isAuthenticated — Basic for machine
  // clients, session cookie for the browser).
  //
  // Gating the shell behind HTTP Basic here would make the browser pop its
  // native Basic dialog on the very first navigation, pre-empting the SPA login
  // page (the whole point of F1). So we intentionally do not challenge here.

  StaticFile *file = NULL;
  if (web_static_get_file(request, &file))
  {
    MongooseHttpServerResponseBasic *response = request->beginResponse();

    // Vite content-hashes everything under /assets/, so those URLs are
    // immutable by construction: a changed file gets a changed name. Telling
    // the browser so removes the revalidation round trip entirely. index.html
    // is NOT hashed -- it is what points at the current asset names -- so it
    // keeps a short lifetime or a new build would never be picked up.
    bool immutable = 0 == strncmp(file->filename, "/assets/", 8);
    response->addHeader(F("Cache-Control"),
                        immutable ? F("public, max-age=31536000, immutable")
                                  : F("public, max-age=30, must-revalidate"));

    MongooseString ifNoneMatch = request->headers("If-None-Match");
    if(http_etag_matches(ifNoneMatch.c_str(), ifNoneMatch.length(), file->etag)) {
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

    // Quoted, per RFC 7232. Sending a bare tag is what broke this: browsers
    // store it, hand it back quoted in If-None-Match, and the old exact-match
    // compare above never fired -- so every page load re-sent the whole bundle.
    char etag[HTTP_ETAG_QUOTED_MAX];
    if(http_etag_quote(file->etag, etag, sizeof(etag))) {
      response->addHeader("Etag", etag);
    }
    response->setContent((const uint8_t *)file->data, file->length);

    request->send(response);

    return true;
  }

  return false;
}
