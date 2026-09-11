#ifdef ENABLE_DEBUG
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "web_server.h"
#include "notifications.h"

// -------------------------------------------------------------------
// Returns the advisory list, muted entries included - the GUI needs to
// render them as muted rather than have them vanish.
// url: /notifications
// -------------------------------------------------------------------
void handleNotifications(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  const size_t capacity = JSON_ARRAY_SIZE(NOTIFICATION_MAX) +
                          NOTIFICATION_MAX * JSON_OBJECT_SIZE(7) +
                          JSON_OBJECT_SIZE(3) + 512;
  DynamicJsonDocument doc(capacity);
  notifications.serialize(doc);
  response->setCode(200);
  serializeJson(doc, *response);
  request->send(response);
}

// -------------------------------------------------------------------
// Acknowledge one advisory. A sticky advisory is muted rather than
// cleared - it stays in the list and keeps its settings-page marker.
// url: /notifications/ack?id=safety.ground_check
// -------------------------------------------------------------------
void handleNotificationAck(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }
  if(!actuatorMethodAllowed(request, response)) {
    return;
  }

  String id = request->getParam("id");
  if(0 == id.length()) {
    response->setCode(400);
    response->print("id required");
    request->send(response);
    return;
  }

  if(!notifications.ack(id.c_str())) {
    response->setCode(404);
    response->print("no such active notification");
    request->send(response);
    return;
  }

  response->setCode(200);
  response->print("acknowledged");
  request->send(response);
  DBUGF("Notification acked: %s", id.c_str());
}
