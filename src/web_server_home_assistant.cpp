#include "emonesp.h"
#include "web_server.h"
#include "app_config.h"
#include "home_assistant.h"
#include "debug.h"

#include <ArduinoJson.h>
#include <MongooseHttpServer.h>

// GET /ha/auth/start  -> 302 to HA authorize URL
// Protected by HTTP basic auth (same as all other endpoints).
void handleHomeAssistantAuthStart(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if (false == requestPreProcess(request, response, CONTENT_TYPE_HTML)) {
    return;
  }

  // request->host() returns MongooseString; .toString() gives String.
  String host = request->host().toString();

  // MongooseHttpServerRequest has no TLS/secure accessor; default to false.
  // LAN HTTP is the common case, and this only affects what redirect_uri we advertise.
  bool secure = false;

  String url = homeAssistant.beginAuthorize(host, secure);

  if (url.length() == 0) {
    response->setCode(400);
    response->setContentType(CONTENT_TYPE_JSON);
    response->print("{\"msg\":\"ha_url not configured\"}");
    request->send(response);
    return;
  }

  // Defense-in-depth: strip CR/LF so a crafted ha_url cannot inject response headers.
  url.replace("\r", "");
  url.replace("\n", "");

  // request->redirect() is a no-op stub in this build of ArduinoMongoose.
  // Use the manual redirect pattern (same as handleHttpsRedirect in web_server.cpp).
  response->setCode(302);
  response->addHeader(F("Location"), url);
  String s = F("<html><head><meta http-equiv=\"Refresh\" content=\"0; url=");
  s += url;
  s += F("\" /></head><body><a href=\"");
  s += url;
  s += F("\">Redirecting…</a></body></html>");
  response->print(s);
  request->send(response);
  // Ownership transferred to request->send(); do NOT delete response.
}

// GET /ha_callback?code=..&state=..  -> exchange code for tokens, then redirect to settings.
// No requestPreProcess/basic-auth: HA's browser redirect cannot carry HTTP credentials.
// Protected by the unguessable single-use `state` CSRF token.
void handleHomeAssistantCallback(MongooseHttpServerRequest *request)
{
  String code  = request->getParam("code");
  String state = request->getParam("state");

  String error;
  bool ok = homeAssistant.handleCallback(code, state, error);

  String dest = F("/#/settings/home-assistant?ha=");
  dest += ok ? F("connected") : F("error");

  // Manual redirect — same pattern as handleHttpsRedirect.
  MongooseHttpServerResponseStream *response = request->beginResponseStream();
  response->setContentType(CONTENT_TYPE_HTML);
  response->setCode(302);
  response->addHeader(F("Location"), dest);
  String s = F("<html><head><meta http-equiv=\"Refresh\" content=\"0; url=");
  s += dest;
  s += F("\" /></head><body><a href=\"");
  s += dest;
  s += F("\">Redirecting…</a></body></html>");
  response->print(s);
  request->send(response);
}

// GET /ha/status -> {enabled, connected, ha_url, expires_in}
void handleHomeAssistantStatus(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }
  const size_t capacity = JSON_OBJECT_SIZE(4) + 512;
  DynamicJsonDocument doc(capacity);
  homeAssistant.getStatus(doc);
  serializeJson(doc, *response);
  request->send(response);
}

// POST /ha/disconnect -> clear stored tokens and disconnect
void handleHomeAssistantDisconnect(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }
  if (HTTP_POST != request->method()) {
    response->setCode(405);
    response->setContentType(CONTENT_TYPE_JSON);
    response->print("{\"msg\":\"Method not allowed\"}");
    request->send(response);
    return;
  }
  homeAssistant.disconnect();
  response->print("{\"msg\":\"done\"}");
  request->send(response);
}
