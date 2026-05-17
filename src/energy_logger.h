#ifndef _ENERGY_LOGGER_H
#define _ENERGY_LOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include <LittleFS.h>
#include "emonesp.h"

#define ENERGY_LOGGER_SAMPLE_INTERVAL 60000  // 60 seconds
#define ENERGY_LOGGER_HOURLY_SAMPLES 60      // 60 samples per hour
#define ENERGY_LOGGER_BUFFER_SIZE 168        // 7 days * 24 hours = 168 hours

#define ENERGY_LOGGER_DIR "/logs"
#define ENERGY_LOGGER_DAILY_DIR "/logs/daily"
#define ENERGY_LOGGER_MONTHLY_FILE "/logs/monthly.json"
#define ENERGY_LOGGER_ANNUAL_FILE "/logs/annual.json"

// Forward declaration
class EvseManager;

struct RawSample
{
  time_t timestamp;
  double amps;
  double temperature;
  double energy_wh;
};

struct DailyMetrics
{
  char date[11];      // "YYYY-MM-DD"
  double peak_temp;
  double min_temp;
  double energy_wh;

  void serialize(JsonObject &obj) const;
  static DailyMetrics deserialize(const JsonObject &obj);
};

struct MonthlyMetrics
{
  char month[8];      // "YYYY-MM"
  double peak_temp;
  double min_temp;
  double energy_kwh;

  void serialize(JsonObject &obj) const;
  static MonthlyMetrics deserialize(const JsonObject &obj);
};

struct AnnualMetrics
{
  uint16_t year;
  double peak_temp;
  double min_temp;
  double energy_kwh;

  void serialize(JsonObject &obj) const;
  static AnnualMetrics deserialize(const JsonObject &obj);
};

class EnergyLogger : public MicroTasks::Task
{
private:
  RawSample _hourly_buffer[ENERGY_LOGGER_BUFFER_SIZE];
  int _buffer_index;
  int _buffer_count;

  EvseManager *_monitor;

  time_t _last_sample_time;
  time_t _last_hour_time;
  time_t _last_day_time;
  uint16_t _last_month;
  uint16_t _last_year;

  double _hour_peak_temp;
  double _hour_min_temp;
  double _hour_energy_wh;
  uint32_t _hour_sample_count;

  void sample();
  void aggregate_hourly();
  void aggregate_daily();
  void aggregate_monthly_yearly();

  bool load_daily(const char *date, DailyMetrics &metrics);
  bool save_daily(const DailyMetrics &metrics);

  bool save_monthly(const MonthlyMetrics &metrics);

  bool save_annual(const AnnualMetrics &metrics);

  void cleanup_old_daily_files();
  void ensure_directories();

  char *format_date(time_t t, char *buf, size_t len);
  char *format_month(time_t t, char *buf, size_t len);
  uint16_t get_year(time_t t);

protected:
  void setup() override;
  unsigned long loop(MicroTasks::WakeReason reason) override;

public:
  EnergyLogger();
  ~EnergyLogger();

  void begin(EvseManager *monitor);
  void end();

  void getRawSamples(JsonDocument &doc, int max_samples = 0);
  void getDailyMetrics(JsonDocument &doc, int days = 365);
  void getMonthlyMetrics(JsonDocument &doc);
  void getAnnualMetrics(JsonDocument &doc);
};

#endif // _ENERGY_LOGGER_H
