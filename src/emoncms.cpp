#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_EMONCMS)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MongooseString.h>
#include <MongooseHttpClient.h>

#include "emonesp.h"
#include "emoncms.h"
#include "app_config.h"
#include "input.h"
#include "event.h"

boolean emoncms_connected = false;
boolean emoncms_updated = false;

unsigned long packets_sent = 0;
unsigned long packets_success = 0;

const char *post_path = "/input/post?";

static MongooseHttpClient client;

static void emoncms_result(bool success, String message)
{
  StaticJsonDocument<128> event;
  
  emoncms_connected = success;
  event["emoncms_connected"] = (int)emoncms_connected;
  event["emoncms_message"] = message.substring(0, 64);
  event_send(event);
}

void emoncms_publish(JsonDocument &data)
{
  Profile_Start(emoncms_publish);

  if (config_emoncms_enabled() && emoncms_apikey != 0)
  {
    String url = emoncms_server + post_path;
    String json;
    serializeJson(data, json);
    url += "fulljson=";
    MongooseString encodedJson = mg_url_encode(MongooseString(json));
    url += (const char *)encodedJson;
    mg_strfree(encodedJson);
    url += "&node=";
    url += emoncms_node;
    url += "&apikey=";
    url += emoncms_apikey;

    DBUGVAR(url);
    packets_sent++;

    client.get(url, [](MongooseHttpClientResponse *response)
    {
      MongooseString result = response->body();
      DBUGF("result = %.*s", result.length(), result.c_str());

      const size_t capacity = JSON_OBJECT_SIZE(2) + result.length();
      DynamicJsonDocument doc(capacity);
      if(DeserializationError::Code::Ok == deserializeJson(doc, result.c_str(), result.length()))
      {
        DBUGLN("Got JSON");
        bool success = doc["success"]; // true
        if(success) {
          packets_success++;
        }
        emoncms_result(success, doc["message"]);
      } else if (result == "ok") {
        packets_success++;
        emoncms_result(true, result);
      } else {
        DEBUG.print("Emoncms error: ");
        DEBUG.printf("%.*s\n", result.length(), (const char *)result);
        emoncms_result(false, result.toString());
      }
    }, [](MongooseHttpClientResponse *response)
    {
      DBUGF("onClose %p", response);
      if(NULL == response) {
        emoncms_result(false, String("Failed to connect"));
      }
    });
  } else {
    if(false != emoncms_connected) {
      emoncms_result(false, String("Disabled"));
    }
  }

  Profile_End(emoncms_publish, 10);
}
