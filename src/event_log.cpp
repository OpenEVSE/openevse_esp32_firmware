#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_EVENT_LOG)
#undef ENABLE_DEBUG
#endif

#include <LITTLEFS.h>
#include <ArduinoJson.h>

#include "debug.h"
#include "emonesp.h"
#include "event_log.h"

EventLog::EventLog() :
  _min_log_index(0),
  _max_log_index(0)
{
}

EventLog::~EventLog()
{
}

String EventLog::filenameFromIndex(uint32_t index)
{
  String filename = EVENTLOG_BASE_DIRECTORY;
  filename += "/" + String(index);
  return filename;
}

uint32_t EventLog::indexFromFilename(String &path)
{
  DBUGVAR(path);

  int lastSeparator = path.lastIndexOf('/');
  String name = lastSeparator >= 0 ? path.substring(lastSeparator + 1) : path;
  DBUGVAR(name);

  return atol(name.c_str());
}

// Scan our base directory for existing log files and workout the min/max index files
void EventLog::begin()
{
  File eventLog = LittleFS.open(EVENTLOG_BASE_DIRECTORY);
  if(eventLog && eventLog.isDirectory())
  {
    _min_log_index = UINT32_MAX;
    _max_log_index = 0;

    File file = eventLog.openNextFile();
    while(file)
    {
      if(!file.isDirectory())
      {
        String name = file.name();
        long chunk = indexFromFilename(name);
        DBUGVAR(chunk);
        if(chunk > _max_log_index) {
          _max_log_index = chunk;
          DBUGVAR(_max_log_index);
        }
        if(chunk < _min_log_index) {
          _min_log_index = chunk;
          DBUGVAR(_min_log_index);
        }
      }

      file = eventLog.openNextFile();
    }

    if(UINT32_MAX == _min_log_index) {
      _min_log_index = 0;
    }
  }
  else
  {
    LittleFS.mkdir(EVENTLOG_BASE_DIRECTORY);
  }
}

void EventLog::log(EventType type, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode)
{
  String eventFilename = filenameFromIndex(_max_log_index);
  File eventFile = LittleFS.open(eventFilename, FILE_APPEND);
  if(eventFile && eventFile.size() > EVENTLOG_ROTATE_SIZE)
  {
    DBUGLN("Rotating log file");
    eventFile.close();

    _max_log_index ++;
    eventFilename = filenameFromIndex(_max_log_index);
    eventFile = LittleFS.open(eventFilename, FILE_APPEND);

    // _max_log_index is inclusive, so we need to increment it here
    while((_max_log_index + 1) - _min_log_index > EVENTLOG_MAX_ROTATE_COUNT) {
      LittleFS.remove(filenameFromIndex(_min_log_index));
      _min_log_index++;
    }
  }

  if(eventFile)
  {
    StaticJsonDocument<256> line;

    time_t now = time(NULL);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char output[80];
    strftime(output, 80, "%FT%TZ", &timeinfo);

    line["t"] = output;
    line["ty"] = type.toInt();
    line["ms"] = managerState.toString();
    line["es"] = evseState;
    line["ef"] = evseFlags;
    line["p"] = pilot;
    line["e"] = energy;
    line["el"] = elapsed;
    line["tp"] = temperature;
    line["tm"] = temperatureMax;
    line["dm"] = divertMode;

    serializeJson(line, eventFile);
    eventFile.println("");

    #ifdef ENABLE_DEBUG
    serializeJson(line, DEBUG_PORT);
    DBUGLN("");
    #endif

    eventFile.close();
  }
}

void EventLog::enumerate(uint32_t index, std::function<void(String time, EventType type, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode)> callback)
{
  String filename = filenameFromIndex(index);
  File eventFile = LittleFS.open(filename);
  if(eventFile)
  {
    while(eventFile.available())
    {
      String line = eventFile.readStringUntil('\n');
      if(line.length() > 0)
      {
        StaticJsonDocument<256> json;
        DeserializationError error = deserializeJson(json, line);
        if(error)
        {
          DBUGF("Error parsing line: %s", error.c_str());
          break;
        }

        String time = json["t"];
        EventType type = EventType::Information;
        type.fromInt(json["ty"]);
        EvseState managerState = EvseState::None;
        managerState.fromString(json["ms"]);
        uint8_t evseState = json["es"];
        uint32_t evseFlags = json["ef"];
        uint32_t pilot = json["p"];
        double energy = json["e"];
        uint32_t elapsed = json["el"];
        double temperature = json["tp"];
        double temperatureMax = json["tm"];
        uint8_t divertMode = json["dm"];

        callback(time, type, managerState, evseState, evseFlags, pilot, energy, elapsed, temperature, temperatureMax, divertMode);
      }
    }
    eventFile.close();
  }
}
