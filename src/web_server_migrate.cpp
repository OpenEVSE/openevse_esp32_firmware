#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

#include "emonesp.h"
#include "web_server.h"
#include "flash_migrate.h"

// -------------------------------------------------------------------
// Start the in-place 16MB flash repartition.
// url: /migrate/expand16mb   (POST)
// Optional JSON body: { "url": "<manifest url>" } to override the default
// GitHub release manifest. Returns immediately; progress/state is pushed over
// the /ws websocket as {"migrate":...,"migrate_progress":...}.
// -------------------------------------------------------------------
void handleMigrateExpand16mb(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if(HTTP_POST != request->method()) {
    response->setCode(405);
    response->print(F("{\"msg\":\"method not allowed\"}"));
    request->send(response);
    return;
  }

  if(flash_migrate_in_progress()) {
    response->setCode(409);
    response->print(F("{\"msg\":\"in progress\"}"));
    request->send(response);
    return;
  }

  if(!flash_migrate_can_expand_16mb()) {
    response->setCode(412);
    response->print(F("{\"msg\":\"not eligible\"}"));
    request->send(response);
    return;
  }

  String manifest_url = "";
  String body = request->body().toString();
  if(body.length() > 0)
  {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, body);
    if(DeserializationError::Code::Ok == error) {
      manifest_url = (const char *)(doc["url"] | "");
    }
  }

  if(flash_migrate_start_16mb(manifest_url)) {
    response->setCode(200);
    response->print(F("{\"msg\":\"started\"}"));
  } else {
    response->setCode(500);
    response->print(F("{\"msg\":\"error\"}"));
  }

  request->send(response);
}
