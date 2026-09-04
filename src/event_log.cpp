#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_EVENT_LOG)
#undef ENABLE_DEBUG
#endif

#include <LittleFS.h>
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

void EventLog::log(EventType type, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint8_t pilotState, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode, uint8_t shaper)
{
  time_t now = time(NULL);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  // Check if we have a reasonable time, don't want to be logging events from 1970
  if(timeinfo.tm_year < (2021 - 1900)) {
    return;
  }

  EventLogEntryKey key = {
    type.toInt(),
    (uint8_t)managerState,
    evseState,
    eventLogSignificantFlags(evseFlags),
    pilot,
    divertMode,
    shaper
  };

  // The type is part of the key, so the first entry of any new state - a fault
  // above all - is always written. Only an exact repeat of what a reader has
  // already been told is dropped, and warnings are not exempt: a fault that
  // persists while the pilot flickers would otherwise flood the log with copies
  // of itself and evict the very context needed to interpret it.
  if(_repeat.isRepeat(key, now)) {
    DBUGLN("EventLog: entry repeats the previous one, not logging");
    return;
  }

  // Why this entry is here. Zero only survives the check above once the repeat
  // interval has lapsed, which is the periodic sample of an unchanging state.
  uint16_t changed = _repeat.changedFrom(key);
  if(0 == changed) {
    changed = EVENTLOG_CHANGE_PERIODIC;
  }

  // Guard against filling LittleFS — keep at least 8 KB free to prevent filesystem corruption.
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < 8192) {
    DBUGLN("EventLog: Low SPIFFS space, skipping entry");
    return;
  }

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
    StaticJsonDocument<320> line;
    char output[80];
    strftime(output, 80, "%FT%TZ", &timeinfo);

    line["t"] = output;
    line["ty"] = type.toInt();
    line["ms"] = managerState.toString();
    line["es"] = evseState;
    line["ef"] = evseFlags;
    line["ps"] = pilotState;
    line["ch"] = changed;
    line["p"] = pilot;
    line["e"] = energy;
    line["el"] = elapsed;
    line["tp"] = temperature;
    line["tm"] = temperatureMax;
    line["dm"] = divertMode;
    line["sh"] = shaper;

    serializeJson(line, eventFile);
    eventFile.println("");

    #ifdef ENABLE_DEBUG
    serializeJson(line, DEBUG_PORT);
    DBUGLN("");
    #endif

    eventFile.close();

    _repeat.recordWritten(key, now);
  }
}

void EventLog::enumerate(uint32_t index, std::function<void(String time, EventType type, const String &logEntry, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint8_t pilotState, uint16_t changed, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode, uint8_t shaper)> callback)
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
        StaticJsonDocument<320> json;
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
        // Entries written before "ps" existed have no pilot state to report.
        uint8_t pilotState = json["ps"] | EVENTLOG_PILOT_STATE_UNKNOWN;
        // Entries written before "ch" existed cannot say why they are there.
        uint16_t changed = json["ch"] | 0;
        uint32_t pilot = json["p"];
        double energy = json["e"];
        uint32_t elapsed = json["el"];
        double temperature = json["tp"];
        double temperatureMax = json["tm"];
        uint8_t divertMode = json["dm"];
        uint8_t shaper = json["sh"];

        callback(time, type, line, managerState, evseState, evseFlags, pilotState, changed, pilot, energy, elapsed, temperature, temperatureMax, divertMode, shaper);
      }
    }
    eventFile.close();
  }
}
