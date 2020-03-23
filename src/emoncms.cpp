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

boolean emoncms_connected = false;

unsigned long packets_sent = 0;
unsigned long packets_success = 0;

const char *post_path = "/input/post?";

static MongooseHttpClient client;

void emoncms_publish(String data)
{
  Profile_Start(emoncms_publish);

  if (emoncms_apikey != 0)
  {
    String url = emoncms_server + post_path;
    url += "fulljson=";
    MongooseString encodedJson = mg_url_encode(MongooseString(data));
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

      StaticJsonDocument<32> doc;
      if(DeserializationError::Code::Ok == deserializeJson(doc, result.c_str(), result.length()))
      {
        bool success = doc["success"]; // true
        if(success) {
          packets_success++;
          emoncms_connected = true;
        }
      } else if (result == "ok") {
        packets_success++;
        emoncms_connected = true;
      } else {
        emoncms_connected = false;
        DEBUG.print("Emoncms error: ");
        DEBUG.printf("%.*s\n", result.length(), (const char *)result);
      }
    });
  }

  Profile_End(emoncms_publish, 10);
}
