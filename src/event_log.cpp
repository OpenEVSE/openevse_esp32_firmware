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

uint32_t EventLog::indexFromFilename(String &filename)
{
  return atol(filename.c_str());
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
        if(chunk > _max_log_index) {
          _max_log_index = chunk;
        }
        if(chunk < _min_log_index) {
          _min_log_index = chunk;
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

void EventLog::log(EventType type, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double tempurature, double tempuratureMax, uint8_t divertMode)
{
  String eventFilename = filenameFromIndex(_max_log_index);

  File eventFile = LittleFS.open(eventFilename, FILE_APPEND);
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
    line["tp"] = tempurature;
    line["tm"] = tempuratureMax;
    line["dm"] = divertMode;

    serializeJson(line, eventFile);
    eventFile.println("");

    #ifdef ENABLE_DEBUG
    serializeJson(line, DEBUG_PORT);
    DBUGLN("");
    #endif

    if(eventFile.size() > EVENTLOG_ROTATE_SIZE)
    {
      _max_log_index ++;
      if(_max_log_index - _min_log_index > EVENTLOG_MAX_ROTATE_COUNT) {
        LittleFS.remove(filenameFromIndex(_min_log_index));
        _min_log_index++;
      }
    }

    eventFile.close();
  }
}
