#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include "web_server.h"
#include "rfid_user.h"
#include "input.h"

typedef const __FlashStringHelper *fstr_t;

// -------------------------------------------------------------------
// Handle RFID user management
// GET /rfid/users - Get all RFID user mappings
// POST /rfid/users - Set user name for RFID tag (body: {"rfid": "xxx", "name": "yyy"})
// DELETE /rfid/users?rfid=xxx - Remove user name for RFID tag
// -------------------------------------------------------------------
void handleRfidUsers(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method())
  {
    DynamicJsonDocument doc(2048);
    RfidUser::load(doc);
    
    response->setCode(200);
    serializeJson(doc, *response);
  }
  else if(HTTP_POST == request->method())
  {
    String body = request->body().toString();
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);
    
    if(error) {
      response->setCode(400);
      response->print("{\"msg\":\"Invalid JSON\"}");
    } else {
      String rfid = doc["rfid"] | "";
      String name = doc["name"] | "";
      
      if(rfid.length() == 0) {
        response->setCode(400);
        response->print("{\"msg\":\"RFID tag is required\"}");
      } else {
        if(RfidUser::setUserName(rfid, name)) {
          response->setCode(200);
          response->print("{\"msg\":\"User name saved\"}");
        } else {
          response->setCode(500);
          response->print("{\"msg\":\"Failed to save user name\"}");
        }
      }
    }
  }
  else if(HTTP_DELETE == request->method())
  {
    String rfid = request->getParam("rfid");
    
    if(rfid.length() == 0) {
      response->setCode(400);
      response->print("{\"msg\":\"RFID tag parameter is required\"}");
    } else {
      if(RfidUser::removeUserName(rfid)) {
        response->setCode(200);
        response->print("{\"msg\":\"User name removed\"}");
      } else {
        response->setCode(500);
        response->print("{\"msg\":\"Failed to remove user name\"}");
      }
    }
  }
  else
  {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}

// -------------------------------------------------------------------
// Export event logs as CSV
// GET /logs/export - Export all logs as CSV
// -------------------------------------------------------------------
void handleLogsExport(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method())
  {
    response->setCode(200);
    response->setContentType("text/csv");
    response->addHeader("Content-Disposition", "attachment; filename=\"session_history.csv\"");
    
    // CSV header
    response->print("Time,Type,State,Energy (kWh),Elapsed (min),RFID Tag,User Name,Temperature (C)\r\n");
    
    // Iterate through all log files
    for(uint32_t i = eventLog.getMinIndex(); i <= eventLog.getMaxIndex(); i++)
    {
      eventLog.enumerate(i, [response](String time, EventType type, const String &logEntry, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode, uint8_t shaper, const String &rfidTag)
      {
        // Convert values
        double energyKwh = energy / 1000.0;
        double elapsedMin = elapsed / 60.0;
        String userName = RfidUser::getUserName(rfidTag);
        
        // Escape CSV fields if they contain commas, quotes, or newlines
        auto escapeCSV = [](const String &field) -> String {
          if(field.indexOf(',') >= 0 || field.indexOf('"') >= 0 || field.indexOf('\n') >= 0) {
            String escaped = field;
            escaped.replace("\"", "\"\"");
            return "\"" + escaped + "\"";
          }
          return field;
        };
        
        // Build CSV line
        response->print(escapeCSV(time));
        response->print(",");
        response->print(escapeCSV(type.toString()));
        response->print(",");
        response->print(escapeCSV(managerState.toString()));
        response->print(",");
        response->print(String(energyKwh, 3));
        response->print(",");
        response->print(String(elapsedMin, 1));
        response->print(",");
        response->print(escapeCSV(rfidTag));
        response->print(",");
        response->print(escapeCSV(userName));
        response->print(",");
        response->print(String(temperature, 1));
        response->print("\r\n");
      });
    }
  }
  else
  {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}
