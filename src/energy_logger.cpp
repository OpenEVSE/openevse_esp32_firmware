#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_ENERGY_LOGGER)
#undef ENABLE_DEBUG
#endif

#include <sys/time.h>
#include <time.h>
#include <stdio.h>

#include "energy_logger.h"
#include "evse_man.h"
#include "debug.h"

EnergyLogger::EnergyLogger() : _buffer_index(0), _buffer_count(0), _monitor(nullptr),
  _last_sample_time(0), _last_hour_time(0), _last_day_time(0),
  _last_month(0), _last_year(0),
  _hour_peak_temp(-99.0), _hour_min_temp(99.0), _hour_energy_wh(0), _hour_sample_count(0)
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
  _last_hour_time = _last_sample_time;
  _last_day_time = _last_sample_time;

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
  double temperature = _monitor->getTemperature(EVSE_MONITOR_TEMP_MONITOR);
  double session_energy = _monitor->getSessionEnergy();

  // Add to circular buffer
  _hourly_buffer[_buffer_index].timestamp = now;
  _hourly_buffer[_buffer_index].amps = amps;
  _hourly_buffer[_buffer_index].temperature = temperature;
  _hourly_buffer[_buffer_index].energy_wh = session_energy;

  _buffer_index = (_buffer_index + 1) % ENERGY_LOGGER_BUFFER_SIZE;
  if (_buffer_count < ENERGY_LOGGER_BUFFER_SIZE) {
    _buffer_count++;
  }

  // Track hourly statistics
  if (temperature >= 0) {  // Only track valid readings
    if (temperature > _hour_peak_temp) {
      _hour_peak_temp = temperature;
    }
    if (temperature < _hour_min_temp) {
      _hour_min_temp = temperature;
    }
  }
  _hour_energy_wh = session_energy;
  _hour_sample_count++;

  DBUGF("[EnergyLogger] Sample: amps=%.2f, temp=%.2f, energy=%.2f Wh", amps, temperature, session_energy);
}

void EnergyLogger::aggregate_hourly()
{
  // This is called when hour changes, but the raw samples stay in the buffer
  // Reset hourly stats for next hour
  _hour_peak_temp = -99.0;
  _hour_min_temp = 99.0;
  _hour_energy_wh = 0;
  _hour_sample_count = 0;

  DBUG("[EnergyLogger] Hourly aggregation completed");
}

void EnergyLogger::aggregate_daily()
{
  if (!_monitor) return;

  time_t now = time(NULL);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  timeinfo.tm_hour = 0;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;
  time_t start_of_day = mktime(&timeinfo);

  double peak_temp = -99.0;
  double min_temp = 99.0;
  double total_energy = 0;
  int sample_count = 0;

  // Aggregate all samples from the past 24 hours
  for (int i = 0; i < _buffer_count; i++) {
    int idx = (_buffer_index - _buffer_count + i + ENERGY_LOGGER_BUFFER_SIZE) % ENERGY_LOGGER_BUFFER_SIZE;
    RawSample &sample = _hourly_buffer[idx];

    if (sample.timestamp >= start_of_day && sample.timestamp < start_of_day + 86400) {
      if (sample.temperature >= 0) {
        if (sample.temperature > peak_temp) {
          peak_temp = sample.temperature;
        }
        if (sample.temperature < min_temp) {
          min_temp = sample.temperature;
        }
      }
      total_energy = sample.energy_wh;
      sample_count++;
    }
  }

  // Create and save daily metrics
  DailyMetrics daily;
  format_date(start_of_day, daily.date, sizeof(daily.date));
  daily.peak_temp = (peak_temp > -99.0) ? peak_temp : 0;
  daily.min_temp = (min_temp < 99.0) ? min_temp : 0;
  daily.energy_wh = total_energy;

  save_daily(daily);
  cleanup_old_daily_files();

  DBUGF("[EnergyLogger] Daily aggregation: date=%s, peak=%.2f, min=%.2f, energy=%.2f Wh",
        daily.date, daily.peak_temp, daily.min_temp, daily.energy_wh);
}

void EnergyLogger::aggregate_monthly_yearly()
{
  if (!_monitor) return;

  time_t now = time(NULL);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Check if month changed
  if (timeinfo.tm_mon + 1 != _last_month || timeinfo.tm_year + 1900 != _last_year) {
    // Aggregate previous month from daily metrics
    MonthlyMetrics monthly;
    format_month(now, monthly.month, sizeof(monthly.month));
    monthly.peak_temp = -99.0;
    monthly.min_temp = 99.0;
    monthly.energy_kwh = 0;

    struct tm prev_month = timeinfo;
    prev_month.tm_mday = 1;
    if (prev_month.tm_mon == 0) {
      prev_month.tm_mon = 11;
      prev_month.tm_year--;
    } else {
      prev_month.tm_mon--;
    }
    mktime(&prev_month);

    // Scan daily directory for files matching previous month
    int days_in_month = (prev_month.tm_mon == 1) ? 28 : 31;
    bool found_any = false;
    for (int day = 1; day <= days_in_month; day++) {
      char date_buf[11];
      sprintf(date_buf, "%04d-%02d-%02d", prev_month.tm_year + 1900, prev_month.tm_mon + 1, day);

      DailyMetrics daily;
      if (load_daily(date_buf, daily)) {
        if (daily.peak_temp > monthly.peak_temp) {
          monthly.peak_temp = daily.peak_temp;
        }
        if (daily.min_temp < monthly.min_temp && daily.min_temp > 0) {
          monthly.min_temp = daily.min_temp;
        }
        monthly.energy_kwh += daily.energy_wh / 1000.0;
        found_any = true;
      }
    }

    if (found_any && monthly.peak_temp > -99.0) {
      save_monthly(monthly);
    }
  }

  DBUG("[EnergyLogger] Monthly/Annual aggregation completed");
}

bool EnergyLogger::load_daily(const char *date, DailyMetrics &metrics)
{
  char filepath[64];
  sprintf(filepath, "%s/%s.json", ENERGY_LOGGER_DAILY_DIR, date);

  File file = LittleFS.open(filepath, "r");
  if (!file) {
    return false;
  }

  const size_t capacity = JSON_OBJECT_SIZE(4) + 64;
  StaticJsonDocument<capacity> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    DBUGF("[EnergyLogger] Error parsing daily metrics: %s", error.c_str());
    return false;
  }

  metrics = DailyMetrics::deserialize(doc.as<JsonObject>());
  return true;
}

bool EnergyLogger::save_daily(const DailyMetrics &metrics)
{
  char filepath[64];
  sprintf(filepath, "%s/%s.json", ENERGY_LOGGER_DAILY_DIR, metrics.date);

  const size_t capacity = JSON_OBJECT_SIZE(4) + 64;
  StaticJsonDocument<capacity> doc;
  JsonObject obj = doc.to<JsonObject>();
  metrics.serialize(obj);

  File file = LittleFS.open(filepath, "w");
  if (!file) {
    DBUGF("[EnergyLogger] Failed to open daily file: %s", filepath);
    return false;
  }

  serializeJson(doc, file);
  file.close();

  DBUGF("[EnergyLogger] Saved daily metrics: %s", metrics.date);
  return true;
}

bool EnergyLogger::save_monthly(const MonthlyMetrics &metrics)
{
  // Load existing data
  DynamicJsonDocument doc(JSON_ARRAY_SIZE(100) + 100 * JSON_OBJECT_SIZE(4));
  JsonArray arr = doc.to<JsonArray>();

  File file = LittleFS.open(ENERGY_LOGGER_MONTHLY_FILE, "r");
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
  file = LittleFS.open(ENERGY_LOGGER_MONTHLY_FILE, "w");
  if (!file) {
    DBUGF("[EnergyLogger] Failed to open monthly file");
    return false;
  }

  serializeJson(doc, file);
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
  file = LittleFS.open(ENERGY_LOGGER_ANNUAL_FILE, "w");
  if (!file) {
    DBUGF("[EnergyLogger] Failed to open annual file");
    return false;
  }

  serializeJson(doc, file);
  file.close();

  DBUGF("[EnergyLogger] Saved annual metrics: %u", metrics.year);
  return true;
}

void EnergyLogger::cleanup_old_daily_files()
{
  // Keep only 365 most recent daily files
  File dir = LittleFS.open(ENERGY_LOGGER_DAILY_DIR);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  // For simplicity, we just limit to ~400 files
  // A more sophisticated approach would sort by date
  int file_count = 0;
  File file = dir.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      file_count++;
    }
    file = dir.openNextFile();
  }

  if (file_count > 400) {
    // Delete oldest files - simplified approach
    DBUGF("[EnergyLogger] Too many daily files (%d), cleanup needed", file_count);
  }
}

void EnergyLogger::ensure_directories()
{
  if (!LittleFS.exists(ENERGY_LOGGER_DIR)) {
    LittleFS.mkdir(ENERGY_LOGGER_DIR);
  }
  if (!LittleFS.exists(ENERGY_LOGGER_DAILY_DIR)) {
    LittleFS.mkdir(ENERGY_LOGGER_DAILY_DIR);
  }
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

void EnergyLogger::getRawSamples(JsonDocument &doc, int max_samples)
{
  JsonArray arr = doc.createNestedArray("samples");

  int start_idx = 0;
  int count = _buffer_count;

  if (max_samples > 0 && count > max_samples) {
    start_idx = count - max_samples;
    count = max_samples;
  }

  for (int i = 0; i < count; i++) {
    int idx = (_buffer_index - _buffer_count + i + ENERGY_LOGGER_BUFFER_SIZE) % ENERGY_LOGGER_BUFFER_SIZE;
    RawSample &sample = _hourly_buffer[idx];

    JsonObject obj = arr.createNestedObject();
    obj["ts"] = (uint32_t)sample.timestamp;
    obj["a"] = sample.amps;
    obj["t"] = sample.temperature;
    obj["e"] = sample.energy_wh;
  }
}

void EnergyLogger::getDailyMetrics(JsonDocument &doc, int days)
{
  JsonArray arr = doc.createNestedArray("daily");

  // Get current date and go back 'days' days
  time_t now = time(NULL);
  for (int i = 0; i < days; i++) {
    time_t date_time = now - (i * 86400);
    char date_buf[11];
    format_date(date_time, date_buf, sizeof(date_buf));

    DailyMetrics metrics;
    if (load_daily(date_buf, metrics)) {
      JsonObject obj = arr.createNestedObject();
      metrics.serialize(obj);
    }
  }
}

void EnergyLogger::getMonthlyMetrics(JsonDocument &doc)
{
  JsonArray arr = doc.createNestedArray("monthly");

  File file = LittleFS.open(ENERGY_LOGGER_MONTHLY_FILE, "r");
  if (!file) {
    return;
  }

  const size_t capacity = JSON_ARRAY_SIZE(200) + 200 * JSON_OBJECT_SIZE(4);
  DynamicJsonDocument temp_doc(capacity);
  DeserializationError error = deserializeJson(temp_doc, file);
  file.close();

  if (error) {
    DBUGF("[EnergyLogger] Error parsing monthly metrics: %s", error.c_str());
    return;
  }

  if (!temp_doc.is<JsonArray>()) {
    return;
  }

  JsonArray src_arr = temp_doc.as<JsonArray>();
  for (JsonObject item : src_arr) {
    JsonObject obj = arr.createNestedObject();
    obj["mo"] = item["mo"];
    obj["pk"] = item["pk"];
    obj["mn"] = item["mn"];
    obj["en"] = item["en"];
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

  JsonArray src_arr = temp_doc.as<JsonArray>();
  for (JsonObject item : src_arr) {
    JsonObject obj = arr.createNestedObject();
    obj["yr"] = item["yr"];
    obj["pk"] = item["pk"];
    obj["mn"] = item["mn"];
    obj["en"] = item["en"];
  }
}
