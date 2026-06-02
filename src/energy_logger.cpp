#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_ENERGY_LOGGER)
#undef ENABLE_DEBUG
#endif

#include <sys/time.h>
#include <time.h>
#include <stdio.h>

#include "energy_logger.h"
#include "evse_man.h"
#include "debug.h"

// Returns true when LittleFS has at least `needed` bytes free plus an 8 KB safety margin.
// Opening a file for write truncates it immediately, so we must check BEFORE open.
static bool fs_has_space(size_t needed) {
  size_t free_bytes = LittleFS.totalBytes() - LittleFS.usedBytes();
  return free_bytes >= needed + 8192;
}

// Returns total bytes currently used by all files under a directory (non-recursive).
static size_t dir_used_bytes(const char *path) {
  size_t total = 0;
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) total += entry.size();
    entry = dir.openNextFile();
  }
  return total;
}

// Returns total bytes currently used by all energy logger files.
static size_t energy_log_used_bytes() {
  size_t total = 0;
  total += dir_used_bytes(ENERGY_LOGGER_RAW_DIR);
  total += dir_used_bytes(ENERGY_LOGGER_DAILY_DIR);
  total += dir_used_bytes(ENERGY_LOGGER_MONTHLY_DIR);
  File f = LittleFS.open(ENERGY_LOGGER_ANNUAL_FILE, "r");
  if (f) { total += f.size(); f.close(); }
  return total;
}

// Returns 40 when LittleFS is 1 MB or larger, 1 otherwise.
// This scales the energy logger limits proportionally to the available partition.
static uint32_t el_scale() {
  return LittleFS.totalBytes() >= (1024u * 1024u) ? 40u : 1u;
}

// Returns true if adding `needed` bytes stays within the energy logger budget.
static bool energy_log_has_budget(size_t needed) {
  return energy_log_used_bytes() + needed <= (size_t)ENERGY_LOGGER_MAX_BYTES * el_scale();
}

EnergyLogger::EnergyLogger() : _buffer_index(0), _buffer_count(0), _monitor(nullptr),
  _last_sample_time(0), _last_hour_time(0), _last_day_time(0), _last_chunk_time(0),
  _last_month(0), _last_year(0),
  _hour_peak_temp(-99.0), _hour_min_temp(99.0), _hour_energy_wh(0), _hour_sample_count(0),
  _day_peak_temp(-99.0), _day_min_temp(99.0), _day_energy_wh(0)
{
  memset(_hourly_buffer, 0, sizeof(_hourly_buffer));
}

EnergyLogger::~EnergyLogger()
{
  end();
}

void EnergyLogger::begin(EvseManager *monitor)
{
  _monitor = monitor;
  ensure_directories();
  MicroTask.startTask(this);
}

void EnergyLogger::end()
{
  if (_monitor) {
    aggregate_hourly();
    aggregate_daily();
    aggregate_monthly_yearly();
    _monitor = nullptr;
  }
}

void EnergyLogger::setup()
{
  _last_sample_time = time(NULL);
  _last_hour_time   = _last_sample_time;
  _last_day_time    = _last_sample_time;
  _last_chunk_time  = _last_sample_time;

  struct tm timeinfo;
  localtime_r(&_last_sample_time, &timeinfo);
  _last_month = timeinfo.tm_mon + 1;
  _last_year = timeinfo.tm_year + 1900;
}

unsigned long EnergyLogger::loop(MicroTasks::WakeReason reason)
{
  if (!_monitor) {
    return ENERGY_LOGGER_SAMPLE_INTERVAL;
  }

  time_t now = time(NULL);

  // Sampling every 60 seconds
  sample();

  // Check for hourly aggregation
  struct tm now_tm, last_tm;
  localtime_r(&now, &now_tm);
  localtime_r(&_last_hour_time, &last_tm);

  if (now_tm.tm_hour != last_tm.tm_hour || now_tm.tm_mday != last_tm.tm_mday) {
    aggregate_hourly();
    _last_hour_time = now;
  }

  // Check for 6-hour raw chunk save (boundaries at 00:00, 06:00, 12:00, 18:00)
  struct tm last_chunk_tm;
  localtime_r(&_last_chunk_time, &last_chunk_tm);
  if ((now_tm.tm_hour / ENERGY_LOGGER_RAW_SAVE_HOURS) != (last_chunk_tm.tm_hour / ENERGY_LOGGER_RAW_SAVE_HOURS) ||
      now_tm.tm_mday != last_chunk_tm.tm_mday ||
      now_tm.tm_mon  != last_chunk_tm.tm_mon) {
    save_raw_chunk();
    _last_chunk_time = now;
  }

  // Check for daily aggregation (midnight)
  if (now_tm.tm_mday != last_tm.tm_mday) {
    aggregate_daily();
    _last_day_time = now;
  }

  // Check for monthly/annual aggregation
  if (now_tm.tm_mon != last_tm.tm_mon || now_tm.tm_year != last_tm.tm_year) {
    aggregate_monthly_yearly();
    _last_month = now_tm.tm_mon + 1;
    _last_year = now_tm.tm_year + 1900;
  }

  return ENERGY_LOGGER_SAMPLE_INTERVAL;
}

void EnergyLogger::sample()
{
  if (!_monitor) return;

  time_t now = time(NULL);
  double amps = _monitor->getAmps();
  // Store 0.0 as the "no valid reading" sentinel; a real 0 °C reading is
  // indistinguishable but practically impossible for an EVSE environment.
  double temperature = _monitor->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)
                         ? _monitor->getTemperature(EVSE_MONITOR_TEMP_MONITOR)
                         : 0.0;
  double session_energy = _monitor->getSessionEnergy();
  // Vehicle SoC: -1 is the "no valid reading" sentinel (0% is a legitimate
  // reading, so unlike temperature we cannot reuse 0 as the sentinel).
  int soc = _monitor->isVehicleStateOfChargeValid()
              ? _monitor->getVehicleStateOfCharge()
              : -1;

  // Add to circular buffer
  _hourly_buffer[_buffer_index].timestamp = now;
  _hourly_buffer[_buffer_index].amps = amps;
  _hourly_buffer[_buffer_index].temperature = temperature;
  _hourly_buffer[_buffer_index].energy_wh = session_energy;
  _hourly_buffer[_buffer_index].soc = soc;

  _buffer_index = (_buffer_index + 1) % ENERGY_LOGGER_BUFFER_SIZE;
  if (_buffer_count < ENERGY_LOGGER_BUFFER_SIZE) {
    _buffer_count++;
  }

  // Track hourly and daily statistics (daily runs independently of the 3-hour
  // circular buffer so yesterday's values survive to midnight aggregation)
  if (temperature > 0) {  // 0 is the invalid/no-sensor sentinel
    if (temperature > _hour_peak_temp) _hour_peak_temp = temperature;
    if (temperature < _hour_min_temp)  _hour_min_temp  = temperature;
    if (temperature > _day_peak_temp)  _day_peak_temp  = temperature;
    if (temperature < _day_min_temp)   _day_min_temp   = temperature;
  }
  _hour_energy_wh = session_energy;
  _day_energy_wh  = session_energy;
  _hour_sample_count++;

  DBUGF("[EnergyLogger] Sample: amps=%.2f, temp=%.2f, energy=%.2f Wh, soc=%d", amps, temperature, session_energy, soc);
}

void EnergyLogger::aggregate_hourly()
{
  _hour_peak_temp = -99.0;
  _hour_min_temp = 99.0;
  _hour_energy_wh = 0;
  _hour_sample_count = 0;

  DBUG("[EnergyLogger] Hourly aggregation completed");
}

void EnergyLogger::save_raw_chunk()
{
  if (_buffer_count == 0) return;

  // _last_chunk_time is within the 6-hour block that just completed.
  // Round its hour down to the block-start (0, 6, 12, or 18).
  struct tm chunk_tm;
  localtime_r(&_last_chunk_time, &chunk_tm);
  chunk_tm.tm_hour = (chunk_tm.tm_hour / ENERGY_LOGGER_RAW_SAVE_HOURS) * ENERGY_LOGGER_RAW_SAVE_HOURS;
  chunk_tm.tm_min  = 0;
  chunk_tm.tm_sec  = 0;
  time_t chunk_start = mktime(&chunk_tm);
  time_t chunk_end   = chunk_start + ENERGY_LOGGER_RAW_SAVE_HOURS * 3600;

  char filepath[64];
  snprintf(filepath, sizeof(filepath), "%s/%04d-%02d-%02d-%02d.json",
    ENERGY_LOGGER_RAW_DIR,
    chunk_tm.tm_year + 1900,
    chunk_tm.tm_mon + 1,
    chunk_tm.tm_mday,
    chunk_tm.tm_hour);

  // Count matching samples to size the JSON document exactly
  int count = 0;
  for (int i = 0; i < _buffer_count; i++) {
    int idx = (_buffer_index - _buffer_count + i + ENERGY_LOGGER_BUFFER_SIZE) % ENERGY_LOGGER_BUFFER_SIZE;
    if (_hourly_buffer[idx].timestamp >= chunk_start && _hourly_buffer[idx].timestamp < chunk_end)
      count++;
  }

  if (count == 0) return;

  const size_t cap = JSON_ARRAY_SIZE(count + 1) + (count + 1) * JSON_OBJECT_SIZE(5) + 64;
  DynamicJsonDocument doc(cap);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < _buffer_count; i++) {
    int idx = (_buffer_index - _buffer_count + i + ENERGY_LOGGER_BUFFER_SIZE) % ENERGY_LOGGER_BUFFER_SIZE;
    RawSample &s = _hourly_buffer[idx];
    if (s.timestamp >= chunk_start && s.timestamp < chunk_end) {
      JsonObject obj = arr.createNestedObject();
      obj["ts"] = (uint32_t)s.timestamp;
      obj["a"]  = s.amps;
      obj["t"]  = s.temperature;
      obj["e"]  = s.energy_wh;
      obj["s"]  = s.soc;
    }
  }

  size_t needed = measureJson(doc);

  cleanup_old_raw_files();

  if (!energy_log_has_budget(needed) || !fs_has_space(needed)) {
    DBUGF("[EnergyLogger] Insufficient space for raw chunk (%u B needed), skipping", needed);
    return;
  }
  File file = LittleFS.open(filepath, "w");
  if (!file) {
    DBUGF("[EnergyLogger] Failed to save raw chunk: %s", filepath);
    return;
  }
  if (serializeJson(doc, file) == 0) {
    DBUGF("[EnergyLogger] Write failed for raw chunk: %s", filepath);
    file.close();
    LittleFS.remove(filepath);
    return;
  }
  file.close();

  DBUGF("[EnergyLogger] Saved %d raw samples to %s", count, filepath);
}

void EnergyLogger::cleanup_old_raw_files()
{
  time_t cutoff = time(NULL) - (time_t)ENERGY_LOGGER_RAW_KEEP_HOURS * el_scale() * 3600;
  struct tm cutoff_tm;
  localtime_r(&cutoff, &cutoff_tm);
  // Align to the block boundary so we never clip a file that is still in window
  cutoff_tm.tm_hour = (cutoff_tm.tm_hour / ENERGY_LOGGER_RAW_SAVE_HOURS) * ENERGY_LOGGER_RAW_SAVE_HOURS;
  cutoff_tm.tm_min  = 0;
  cutoff_tm.tm_sec  = 0;
  mktime(&cutoff_tm);

  // File names are YYYY-MM-DD-HH.json — lexicographic order equals chronological order
  char cutoff_name[24];
  snprintf(cutoff_name, sizeof(cutoff_name), "%04d-%02d-%02d-%02d.json",
    cutoff_tm.tm_year + 1900,
    cutoff_tm.tm_mon + 1,
    cutoff_tm.tm_mday,
    cutoff_tm.tm_hour);

  // Delete files one at a time; re-open the directory after each removal
  bool deleted;
  do {
    deleted = false;
    File dir = LittleFS.open(ENERGY_LOGGER_RAW_DIR);
    if (!dir || !dir.isDirectory()) return;

    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        const char *full = entry.name();
        const char *slash = strrchr(full, '/');
        const char *name  = slash ? slash + 1 : full;
        if (strcmp(name, cutoff_name) < 0) {
          char filepath[64];
          snprintf(filepath, sizeof(filepath), "%s/%s", ENERGY_LOGGER_RAW_DIR, name);
          entry.close();
          LittleFS.remove(filepath);
          deleted = true;
          break;
        }
      }
      entry = dir.openNextFile();
    }
  } while (deleted);
}

void EnergyLogger::cleanup_old_daily_files()
{
  // Files are named YYYY-QN.json — lexicographic order equals chronological order.
  // Delete the oldest file whenever the count exceeds ENERGY_LOGGER_DAILY_MAX_QTRS.
  bool deleted;
  do {
    deleted = false;
    int count = 0;
    char oldest_name[24] = {0};

    File dir = LittleFS.open(ENERGY_LOGGER_DAILY_DIR);
    if (!dir || !dir.isDirectory()) return;

    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        const char *full = entry.name();
        const char *slash = strrchr(full, '/');
        const char *name  = slash ? slash + 1 : full;
        count++;
        if (oldest_name[0] == '\0' || strcmp(name, oldest_name) < 0)
          strlcpy(oldest_name, name, sizeof(oldest_name));
      }
      entry = dir.openNextFile();
    }

    if (count > (int)(ENERGY_LOGGER_DAILY_MAX_QTRS * el_scale()) && oldest_name[0] != '\0') {
      char filepath[64];
      snprintf(filepath, sizeof(filepath), "%s/%s", ENERGY_LOGGER_DAILY_DIR, oldest_name);
      LittleFS.remove(filepath);
      DBUGF("[EnergyLogger] Removed old quarterly file: %s", filepath);
      deleted = true;
    }
  } while (deleted);
}

void EnergyLogger::aggregate_daily()
{
  if (!_monitor) return;

  // Compute the date that just ended (yesterday at the moment midnight fires)
  time_t now = time(NULL);
  time_t yesterday = now - 86400;
  struct tm timeinfo;
  localtime_r(&yesterday, &timeinfo);
  timeinfo.tm_hour = 0;
  timeinfo.tm_min  = 0;
  timeinfo.tm_sec  = 0;
  time_t start_of_yesterday = mktime(&timeinfo);

  DailyMetrics daily;
  format_date(start_of_yesterday, daily.date, sizeof(daily.date));

  // Use the running daily accumulators — they track the full 24 h independently
  // of the 3-hour circular buffer, so no samples are lost to buffer rotation.
  daily.peak_temp = (_day_peak_temp > -99.0) ? _day_peak_temp : 0;
  daily.min_temp  = (_day_min_temp  <  99.0) ? _day_min_temp  : 0;
  daily.energy_wh = _day_energy_wh;

  // Reset accumulators for the new day
  _day_peak_temp = -99.0;
  _day_min_temp  =  99.0;
  _day_energy_wh =   0.0;

  append_daily_to_quarter(daily);

  DBUGF("[EnergyLogger] Daily aggregation: date=%s, peak=%.2f, min=%.2f, energy=%.2f Wh",
        daily.date, daily.peak_temp, daily.min_temp, daily.energy_wh);
}

// ── Quarterly daily helpers ───────────────────────────────────────────────────

// Capacity for one quarterly file: up to 92 days, each with a 10-char date string
#define QUARTER_JSON_CAP \
  (JSON_ARRAY_SIZE(93) + 93 * JSON_OBJECT_SIZE(4) + 93 * 16)

bool EnergyLogger::append_daily_to_quarter(const DailyMetrics &metrics)
{
  // Parse year + month from metrics.date ("YYYY-MM-DD")
  int year = 0, month = 0, day = 0;
  sscanf(metrics.date, "%d-%d-%d", &year, &month, &day);
  int quarter = month_to_quarter(month);

  char filepath[64];
  snprintf(filepath, sizeof(filepath), "%s/%04d-Q%d.json",
           ENERGY_LOGGER_DAILY_DIR, year, quarter);

  DynamicJsonDocument doc(QUARTER_JSON_CAP);
  JsonArray arr = doc.to<JsonArray>();

  File file = LittleFS.open(filepath, "r");
  if (file) {
    deserializeJson(doc, file);
    file.close();
    if (!doc.is<JsonArray>()) arr = doc.to<JsonArray>();
  }

  JsonObject obj = arr.createNestedObject();
  obj["dt"] = (char *)metrics.date;   // char* forces ArduinoJson to copy the string
  obj["pk"] = metrics.peak_temp;
  obj["mn"] = metrics.min_temp;
  obj["en"] = metrics.energy_wh;

  size_t needed = measureJson(doc);

  cleanup_old_daily_files();

  if (!energy_log_has_budget(needed) || !fs_has_space(needed)) {
    DBUGF("[EnergyLogger] Insufficient space for quarterly file (%u B needed), skipping", needed);
    return false;
  }
  file = LittleFS.open(filepath, "w");
  if (!file) {
    DBUGF("[EnergyLogger] Failed to write quarterly file: %s", filepath);
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    DBUGF("[EnergyLogger] Write failed for quarterly file: %s", filepath);
    file.close();
    LittleFS.remove(filepath);
    return false;
  }
  file.close();

  DBUGF("[EnergyLogger] Appended daily entry to %s", filepath);
  return true;
}

bool EnergyLogger::load_month_from_quarter(int year, int month,
                                           double &peak_temp, double &min_temp,
                                           double &total_energy_wh)
{
  int quarter = month_to_quarter(month);
  char filepath[64];
  snprintf(filepath, sizeof(filepath), "%s/%04d-Q%d.json",
           ENERGY_LOGGER_DAILY_DIR, year, quarter);

  File file = LittleFS.open(filepath, "r");
  if (!file) return false;

  DynamicJsonDocument doc(QUARTER_JSON_CAP);
  deserializeJson(doc, file);
  file.close();
  if (!doc.is<JsonArray>()) return false;

  char month_prefix[8];
  snprintf(month_prefix, sizeof(month_prefix), "%04d-%02d", year, month);

  bool found = false;
  for (JsonObject item : doc.as<JsonArray>()) {
    const char *dt = item["dt"] | "";
    if (strncmp(dt, month_prefix, 7) == 0) {
      double pk = item["pk"] | 0.0;
      double mn = item["mn"] | 0.0;
      double en = item["en"] | 0.0;
      if (pk > peak_temp) peak_temp = pk;
      if (mn > 0 && mn < min_temp) min_temp = mn;
      total_energy_wh += en;
      found = true;
    }
  }
  return found;
}

// ── Monthly/yearly aggregation ────────────────────────────────────────────────

void EnergyLogger::aggregate_monthly_yearly()
{
  if (!_monitor) return;

  time_t now = time(NULL);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  if (timeinfo.tm_mon + 1 != _last_month || timeinfo.tm_year + 1900 != _last_year) {
    struct tm prev = timeinfo;
    prev.tm_mday = 1;
    if (prev.tm_mon == 0) { prev.tm_mon = 11; prev.tm_year--; }
    else                  { prev.tm_mon--;                    }
    mktime(&prev);

    int prev_year  = prev.tm_year + 1900;
    int prev_month = prev.tm_mon  + 1;

    double peak = -99.0, minT = 99.0, energy_wh = 0.0;
    bool found = load_month_from_quarter(prev_year, prev_month, peak, minT, energy_wh);

    if (found && peak > -99.0) {
      MonthlyMetrics monthly;
      snprintf(monthly.month, sizeof(monthly.month), "%04d-%02d", prev_year, prev_month);
      monthly.peak_temp  = peak;
      monthly.min_temp   = (minT < 99.0) ? minT : 0;
      monthly.energy_kwh = energy_wh / 1000.0;
      save_monthly(monthly);
    }
  }

  DBUG("[EnergyLogger] Monthly/Annual aggregation completed");
}

// ── Monthly file: /logs/monthly/YYYY.json (≤12 entries) ──────────────────────

#define MONTHLY_JSON_CAP \
  (JSON_ARRAY_SIZE(13) + 13 * JSON_OBJECT_SIZE(4) + 13 * 12)

bool EnergyLogger::save_monthly(const MonthlyMetrics &metrics)
{
  int year = 0, month = 0;
  sscanf(metrics.month, "%d-%d", &year, &month);

  char filepath[64];
  snprintf(filepath, sizeof(filepath), "%s/%04d.json",
           ENERGY_LOGGER_MONTHLY_DIR, year);

  DynamicJsonDocument doc(MONTHLY_JSON_CAP);
  JsonArray arr = doc.to<JsonArray>();

  File file = LittleFS.open(filepath, "r");
  if (file) {
    deserializeJson(doc, file);
    file.close();
    if (!doc.is<JsonArray>()) arr = doc.to<JsonArray>();
  }

  JsonObject obj = arr.createNestedObject();
  obj["mo"] = (char *)metrics.month;
  obj["pk"] = metrics.peak_temp;
  obj["mn"] = metrics.min_temp;
  obj["en"] = metrics.energy_kwh;

  size_t needed = measureJson(doc);
  if (!energy_log_has_budget(needed) || !fs_has_space(needed)) {
    DBUGF("[EnergyLogger] Insufficient space for monthly file (%u B needed), skipping", needed);
    return false;
  }
  file = LittleFS.open(filepath, "w");
  if (!file) {
    DBUGF("[EnergyLogger] Failed to write monthly file: %s", filepath);
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    DBUGF("[EnergyLogger] Write failed for monthly file: %s", filepath);
    file.close();
    LittleFS.remove(filepath);
    return false;
  }
  file.close();

  DBUGF("[EnergyLogger] Saved monthly metrics: %s", metrics.month);
  return true;
}

bool EnergyLogger::save_annual(const AnnualMetrics &metrics)
{
  // Load existing data
  DynamicJsonDocument doc(JSON_ARRAY_SIZE(100) + 100 * JSON_OBJECT_SIZE(4));
  JsonArray arr = doc.to<JsonArray>();

  File file = LittleFS.open(ENERGY_LOGGER_ANNUAL_FILE, "r");
  if (file) {
    deserializeJson(doc, file);
    file.close();
    if (!doc.is<JsonArray>()) {
      arr = doc.to<JsonArray>();
    }
  }

  // Append new metrics
  JsonObject obj = arr.createNestedObject();
  metrics.serialize(obj);

  // Save
  size_t needed = measureJson(doc);
  if (!energy_log_has_budget(needed) || !fs_has_space(needed)) {
    DBUGF("[EnergyLogger] Insufficient space for annual file (%u B needed), skipping", needed);
    return false;
  }
  file = LittleFS.open(ENERGY_LOGGER_ANNUAL_FILE, "w");
  if (!file) {
    DBUGF("[EnergyLogger] Failed to open annual file");
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    DBUGF("[EnergyLogger] Write failed for annual file");
    file.close();
    LittleFS.remove(ENERGY_LOGGER_ANNUAL_FILE);
    return false;
  }
  file.close();

  DBUGF("[EnergyLogger] Saved annual metrics: %u", metrics.year);
  return true;
}

void EnergyLogger::ensure_directories()
{
  if (!LittleFS.exists(ENERGY_LOGGER_DIR))         LittleFS.mkdir(ENERGY_LOGGER_DIR);
  if (!LittleFS.exists(ENERGY_LOGGER_DAILY_DIR))   LittleFS.mkdir(ENERGY_LOGGER_DAILY_DIR);
  if (!LittleFS.exists(ENERGY_LOGGER_RAW_DIR))     LittleFS.mkdir(ENERGY_LOGGER_RAW_DIR);
  if (!LittleFS.exists(ENERGY_LOGGER_MONTHLY_DIR)) LittleFS.mkdir(ENERGY_LOGGER_MONTHLY_DIR);
}

char *EnergyLogger::format_date(time_t t, char *buf, size_t len)
{
  struct tm timeinfo;
  localtime_r(&t, &timeinfo);
  snprintf(buf, len, "%04d-%02d-%02d",
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday);
  return buf;
}

char *EnergyLogger::format_month(time_t t, char *buf, size_t len)
{
  struct tm timeinfo;
  localtime_r(&t, &timeinfo);
  snprintf(buf, len, "%04d-%02d",
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1);
  return buf;
}

uint16_t EnergyLogger::get_year(time_t t)
{
  struct tm timeinfo;
  localtime_r(&t, &timeinfo);
  return timeinfo.tm_year + 1900;
}

void DailyMetrics::serialize(JsonObject &obj) const
{
  obj["dt"] = date;
  obj["pk"] = peak_temp;
  obj["mn"] = min_temp;
  obj["en"] = energy_wh;
}

DailyMetrics DailyMetrics::deserialize(const JsonObject &obj)
{
  DailyMetrics m;
  strlcpy(m.date, obj["dt"] | "", sizeof(m.date));
  m.peak_temp = obj["pk"] | 0.0;
  m.min_temp = obj["mn"] | 0.0;
  m.energy_wh = obj["en"] | 0.0;
  return m;
}

void MonthlyMetrics::serialize(JsonObject &obj) const
{
  obj["mo"] = month;
  obj["pk"] = peak_temp;
  obj["mn"] = min_temp;
  obj["en"] = energy_kwh;
}

MonthlyMetrics MonthlyMetrics::deserialize(const JsonObject &obj)
{
  MonthlyMetrics m;
  strlcpy(m.month, obj["mo"] | "", sizeof(m.month));
  m.peak_temp = obj["pk"] | 0.0;
  m.min_temp = obj["mn"] | 0.0;
  m.energy_kwh = obj["en"] | 0.0;
  return m;
}

void AnnualMetrics::serialize(JsonObject &obj) const
{
  obj["yr"] = year;
  obj["pk"] = peak_temp;
  obj["mn"] = min_temp;
  obj["en"] = energy_kwh;
}

AnnualMetrics AnnualMetrics::deserialize(const JsonObject &obj)
{
  AnnualMetrics m;
  m.year = obj["yr"] | 0;
  m.peak_temp = obj["pk"] | 0.0;
  m.min_temp = obj["mn"] | 0.0;
  m.energy_kwh = obj["en"] | 0.0;
  return m;
}

void EnergyLogger::getRawSamples(JsonDocument &doc, int max_samples, time_t before)
{
  JsonArray arr = doc.createNestedArray("samples");

  if (before > 0) {
    // Step back a full save-interval from 'before' to land in the previous
    // block.  Using 'before - 1' mapped to the SAME block as the current
    // buffer when the buffer started at a non-boundary time, and the filter
    // 'ts >= before' then discarded every sample in that file.
    time_t target = before - (time_t)ENERGY_LOGGER_RAW_SAVE_HOURS * 3600;
    struct tm file_tm;
    localtime_r(&target, &file_tm);
    file_tm.tm_hour = (file_tm.tm_hour / ENERGY_LOGGER_RAW_SAVE_HOURS) * ENERGY_LOGGER_RAW_SAVE_HOURS;
    file_tm.tm_min  = 0;
    file_tm.tm_sec  = 0;
    time_t chunk_start = mktime(&file_tm);
    time_t chunk_end   = chunk_start + (time_t)ENERGY_LOGGER_RAW_SAVE_HOURS * 3600;

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%04d-%02d-%02d-%02d.json",
      ENERGY_LOGGER_RAW_DIR,
      file_tm.tm_year + 1900,
      file_tm.tm_mon + 1,
      file_tm.tm_mday,
      file_tm.tm_hour);

    File file = LittleFS.open(filepath, "r");
    if (file) {
      DynamicJsonDocument file_doc(JSON_ARRAY_SIZE(ENERGY_LOGGER_BUFFER_SIZE + 1) + (ENERGY_LOGGER_BUFFER_SIZE + 1) * JSON_OBJECT_SIZE(5) + 128);
      DeserializationError err = deserializeJson(file_doc, file);
      file.close();

      if (!err && file_doc.is<JsonArray>()) {
        int count = 0;
        for (JsonObject item : file_doc.as<JsonArray>()) {
          // Return all samples in the chunk — no 'before' filter needed
          // because we are already pointing at the preceding block.
          if (max_samples > 0 && count >= max_samples) break;
          JsonObject obj = arr.createNestedObject();
          obj["ts"] = item["ts"];
          obj["a"]  = item["a"];
          obj["t"]  = item["t"];
          obj["e"]  = item["e"];
          obj["s"]  = item["s"] | -1;   // default for chunks written before SoC logging
          count++;
        }
      }
    } else {
      // File not yet written — search the in-memory buffer for the window
      for (int i = 0; i < _buffer_count; i++) {
        int idx = (_buffer_index - _buffer_count + i + ENERGY_LOGGER_BUFFER_SIZE) % ENERGY_LOGGER_BUFFER_SIZE;
        RawSample &s = _hourly_buffer[idx];
        if (s.timestamp < chunk_start || s.timestamp >= chunk_end) continue;
        if (max_samples > 0 && (int)arr.size() >= max_samples) break;
        JsonObject obj = arr.createNestedObject();
        obj["ts"] = (uint32_t)s.timestamp;
        obj["a"]  = s.amps;
        obj["t"]  = s.temperature;
        obj["e"]  = s.energy_wh;
        obj["s"]  = s.soc;
      }
    }
    return;
  }

  // Default: return most-recent in-memory samples
  int offset = 0;
  int count  = _buffer_count;

  if (max_samples > 0 && count > max_samples) {
    offset = count - max_samples;
    count  = max_samples;
  }

  for (int i = offset; i < offset + count; i++) {
    int idx = (_buffer_index - _buffer_count + i + ENERGY_LOGGER_BUFFER_SIZE) % ENERGY_LOGGER_BUFFER_SIZE;
    RawSample &sample = _hourly_buffer[idx];

    JsonObject obj = arr.createNestedObject();
    obj["ts"] = (uint32_t)sample.timestamp;
    obj["a"]  = sample.amps;
    obj["t"]  = sample.temperature;
    obj["e"]  = sample.energy_wh;
    obj["s"]  = sample.soc;
  }
}

void EnergyLogger::getDailyMetrics(JsonDocument &doc, int year, int quarter)
{
  // Default to current year / quarter
  if (year == 0 || quarter == 0) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (year    == 0) year    = tm_now.tm_year + 1900;
    if (quarter == 0) quarter = month_to_quarter(tm_now.tm_mon + 1);
  }

  JsonArray arr = doc.createNestedArray("daily");

  char filepath[64];
  snprintf(filepath, sizeof(filepath), "%s/%04d-Q%d.json",
           ENERGY_LOGGER_DAILY_DIR, year, quarter);

  File file = LittleFS.open(filepath, "r");
  if (!file) return;

  DynamicJsonDocument file_doc(QUARTER_JSON_CAP);
  DeserializationError err = deserializeJson(file_doc, file);
  file.close();
  if (err || !file_doc.is<JsonArray>()) return;

  for (JsonObject item : file_doc.as<JsonArray>()) {
    JsonObject obj = arr.createNestedObject();
    const char *dt = item["dt"] | "";
    obj["dt"] = (char *)dt;   // force copy into doc's pool
    obj["pk"] = item["pk"] | 0.0;
    obj["mn"] = item["mn"] | 0.0;
    obj["en"] = item["en"] | 0.0;
  }
}

void EnergyLogger::getMonthlyMetrics(JsonDocument &doc, int year)
{
  if (year == 0) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    year = tm_now.tm_year + 1900;
  }

  char filepath[64];
  snprintf(filepath, sizeof(filepath), "%s/%04d.json",
           ENERGY_LOGGER_MONTHLY_DIR, year);

  JsonArray arr = doc.createNestedArray("monthly");

  File file = LittleFS.open(filepath, "r");
  if (!file) return;

  DynamicJsonDocument temp_doc(MONTHLY_JSON_CAP);
  DeserializationError err = deserializeJson(temp_doc, file);
  file.close();
  if (err || !temp_doc.is<JsonArray>()) return;

  for (JsonObject item : temp_doc.as<JsonArray>()) {
    JsonObject obj = arr.createNestedObject();
    const char *mo = item["mo"] | "";
    obj["mo"] = (char *)mo;
    obj["pk"] = item["pk"] | 0.0;
    obj["mn"] = item["mn"] | 0.0;
    obj["en"] = item["en"] | 0.0;
  }
}

void EnergyLogger::getAnnualMetrics(JsonDocument &doc)
{
  JsonArray arr = doc.createNestedArray("annual");

  File file = LittleFS.open(ENERGY_LOGGER_ANNUAL_FILE, "r");
  if (!file) {
    return;
  }

  const size_t capacity = JSON_ARRAY_SIZE(100) + 100 * JSON_OBJECT_SIZE(4);
  DynamicJsonDocument temp_doc(capacity);
  DeserializationError error = deserializeJson(temp_doc, file);
  file.close();

  if (error) {
    DBUGF("[EnergyLogger] Error parsing annual metrics: %s", error.c_str());
    return;
  }

  if (!temp_doc.is<JsonArray>()) {
    return;
  }

  for (JsonObject item : temp_doc.as<JsonArray>()) {
    JsonObject obj = arr.createNestedObject();
    // yr is a numeric field — no string-ownership issue, but copy explicitly
    // for consistency and to avoid any future regressions.
    obj["yr"] = item["yr"] | 0;
    obj["pk"] = item["pk"] | 0.0;
    obj["mn"] = item["mn"] | 0.0;
    obj["en"] = item["en"] | 0.0;
  }
}
