#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

typedef const __FlashStringHelper *fstr_t;

#include "emonesp.h"
#include "web_server.h"
#include "evse_man.h"
#include "input.h"

// -------------------------------------------------------------------
//
// url: /claims
// -------------------------------------------------------------------
void
handleEvseClaimsGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint32_t client)
{
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument doc(capacity);

  bool success = (EvseClient_NULL == client) ?
    evse.serializeClaims(doc) :
    evse.serializeClaim(doc, client);

  if(success) {
    response->setCode(200);
    serializeJson(doc, *response);
  } else {
    response->setCode(404);
    response->print("{\"msg\":\"Not found\"}");
  }
}

void
handleEvseClaimsPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint32_t client)
{
  String body = request->body().toString();

  if(EvseClient_NULL != client)
  {
    EvseProperties properties;
    if(properties.deserialize(body))
    {
      int priority = EvseManager_Priority_API;
      if(evse.claim(client, priority, properties)) {
        response->setCode(200);
        response->print("{\"msg\":\"done\"}");
      } else {
        response->setCode(400);
        response->print("{\"msg\":\"Could not make claim\"}");
      }
    } else {
      response->setCode(400);
      response->print("{\"msg\":\"Could not parse JSON\"}");
    }
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }
}

void
handleEvseClaimsDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint32_t client)
{
  if(EvseClient_NULL != client)
  {
    if(evse.release(client)) {
      response->setCode(200);
      response->print("{\"msg\":\"done\"}");
    } else {
      response->setCode(404);
      response->print("{\"msg\":\"Not found\"}");
    }
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }
}

#define EVSE_CLAIM_PATH_LEN (sizeof("/claims/") - 1)

void
handleEvseClaims(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  uint32_t client = EvseClient_NULL;

  String path = request->uri();
  if(path.length() > EVSE_CLAIM_PATH_LEN) {
    String clientStr = path.substring(EVSE_CLAIM_PATH_LEN);
    DBUGVAR(clientStr);
    client = clientStr.toInt();
  }

  DBUGVAR(client, HEX);

  if(HTTP_GET == request->method()) {
    handleEvseClaimsGet(request, response, client);
  } else if(HTTP_POST == request->method()) {
    handleEvseClaimsPost(request, response, client);
  } else if(HTTP_DELETE == request->method()) {
    handleEvseClaimsDelete(request, response, client);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}
