#include <Arduino.h>
#include <MongooseString.h>
#include <MongooseHttpClient.h>

#include "emonesp.h"
#include "emoncms.h"
#include "config.h"
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

    DBUGLN(url);
    packets_sent++;

    client.get(url, [](MongooseHttpClientResponse *response)
    {
      MongooseString result = response->body();
      if (result == "ok") {
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
