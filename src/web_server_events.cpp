#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

typedef const __FlashStringHelper *fstr_t;

#include "emonesp.h"
#include "web_server.h"
#include "evse_man.h"
#include "input.h"
#include "current_shaper.h"

// /events
#define LOG_BASE_LEN 6

// -------------------------------------------------------------------
// Download event file.
// -------------------------------------------------------------------

void handleEventLogs(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method())
  {
    String path = request->uri();
    if(path.length() > LOG_BASE_LEN)
    {
      uint32_t block = EvseClient_NULL;
      String blockStr = path.substring(LOG_BASE_LEN);
      DBUGVAR(blockStr);
      block = blockStr.toInt();
      DBUGVAR(block);

      if(eventLog.getMinIndex() <= block && block <= eventLog.getMaxIndex())
      {
        response->setCode(200);
        int count = 0;

        response->print("[");

        eventLog.enumerate(block, [&count, response](String time, EventType type, const String &logEntry, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode, uint8_t shaper)
        {
          StaticJsonDocument<1024> event;

          if(count++ > 0) {
            response->print(",");
          }

          event["time"] = time;
          event["type"] = type.toString();
          event["managerState"] = managerState.toString();
          event["evseState"] = evseState;
          event["evseFlags"] = evseFlags;
          event["pilot"] = pilot;
          event["energy"] = energy;
          event["elapsed"] = elapsed;
          event["temperature"] = temperature;
          event["temperatureMax"] = temperatureMax;
          event["divertMode"] = divertMode;
          event["shaper"] = shaper == true?1:0;
          serializeJson(event, *response);
        });

        response->print("]");

      } else {
        response->setCode(404);
        response->print("{\"msg\":\"Block out of range\"}");
      }
    }
    else
    {
      StaticJsonDocument<1024> doc;
      doc["min"] = eventLog.getMinIndex();
      doc["max"] = eventLog.getMaxIndex();

      response->setCode(200);
      serializeJson(doc, *response);
    }

  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);

}

