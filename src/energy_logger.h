#ifndef _ENERGY_LOGGER_H
#define _ENERGY_LOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include <LittleFS.h>
#include "emonesp.h"

#define ENERGY_LOGGER_SAMPLE_INTERVAL  60000   // 60 seconds
#define ENERGY_LOGGER_HOURLY_SAMPLES   60      // 60 samples per hour
#define ENERGY_LOGGER_BUFFER_SIZE      180     // 3 hours of 1-per-minute samples
#define ENERGY_LOGGER_DIR              "/logs"
#define ENERGY_LOGGER_DAILY_DIR        "/logs/daily"
#define ENERGY_LOGGER_RAW_DIR          "/logs/raw"
#define ENERGY_LOGGER_RAW_SAVE_HOURS   3       // write a raw chunk file every 3 h
#define ENERGY_LOGGER_RAW_KEEP_HOURS   6       // keep 6 h = 2 files (~18 KB)
#define ENERGY_LOGGER_DAILY_MAX_QTRS   2       // keep 2 quarterly files = 6 months (~6 KB)
#define ENERGY_LOGGER_MAX_BYTES        20480   // hard cap for all /logs files (~20 KB)
#define ENERGY_LOGGER_MONTHLY_DIR      "/logs/monthly"
#define ENERGY_LOGGER_ANNUAL_FILE      "/logs/annual.json"

// Daily files:   /logs/daily/YYYY-QN.json   (quarterly, appended daily)
// Monthly files: /logs/monthly/YYYY.json    (yearly,    appended monthly, ≤12 entries)

// Forward declaration
class EvseManager;

struct RawSample
{
  time_t timestamp;
  double amps;
  double temperature;
  double energy_wh;
  int soc;            // vehicle state of charge (%), -1 when no valid reading
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
  time_t _last_chunk_time;   // tracks the raw-file boundary
  uint16_t _last_month;
  uint16_t _last_year;

  double _hour_peak_temp;
  double _hour_min_temp;
  double _hour_energy_wh;
  uint32_t _hour_sample_count;

  double _day_peak_temp;
  double _day_min_temp;
  double _day_energy_wh;

  void sample();
  void aggregate_hourly();
  void aggregate_daily();
  void aggregate_monthly_yearly();

  // Quarterly daily file helpers
  static int month_to_quarter(int month) { return (month - 1) / 3 + 1; }
  bool append_daily_to_quarter(const DailyMetrics &metrics);
  bool load_month_from_quarter(int year, int month,
                               double &peak_temp, double &min_temp,
                               double &total_energy_wh);

  bool save_monthly(const MonthlyMetrics &metrics);
  bool save_annual(const AnnualMetrics &metrics);

  void save_raw_chunk();
  void cleanup_old_raw_files();
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

  void getRawSamples(JsonDocument &doc, int max_samples = 0, time_t before = 0);
  // year=0 → current year, quarter=0 → current quarter (1-4)
  void getDailyMetrics(JsonDocument &doc, int year = 0, int quarter = 0);
  void getMonthlyMetrics(JsonDocument &doc, int year = 0);
  void getAnnualMetrics(JsonDocument &doc);
};

#endif // _ENERGY_LOGGER_H
