#include <iostream>
#include <sys/time.h>
#include <string>

#include "StdioSerial.h"
#include "RapiSender.h"
#include "openevse.h"
#include "divert.h"
#include "event.h"
#include "event_log.h"
#include "manual.h"

#include "parser.hpp"
#include "cxxopts.hpp"

#include <MicroTasks.h>
#include <EpoxyFS.h>

#include <epoxy_test/ArduinoTest.h>

using namespace aria::csv;

EventLog eventLog;
EvseManager evse(RAPI_PORT, eventLog);
DivertTask divert(evse);
ManualOverride manual(evse);

long pilot = 32;                      // OpenEVSE Pilot Setting
long state = OPENEVSE_STATE_CONNECTED; // OpenEVSE State
double voltage = 240; // Voltage from OpenEVSE or MQTT

extern double smoothed_available_current;

int date_col = 0;
int grid_ie_col = -1;
int solar_col = 1;
int voltage_col = 1;

time_t simulated_time = 0;
time_t last_time = 0;

bool kw = false;

time_t parse_date(const char *dateStr)
{
  int y = 2020, M = 1, d = 1, h = 0, m = 0, s = 0;
  char ampm[5];
  if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%dZ", &y, &M, &d, &h, &m, &s)) {
    if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%d+00:00", &y, &M, &d, &h, &m, &s)) {
      if(6 != sscanf(dateStr, "%d-%d-%d %d:%d:%d", &y, &M, &d, &h, &m, &s)) {
        if(3 != sscanf(dateStr, "%d:%d %s", &h, &m, ampm)) {
          if(1 == sscanf(dateStr, "%d", &s)) {
            return s;
          }
        } else {
          y = 2020; M = 1; d = 1; s = 0;
          if(12 == h) {
            h -= 12;
          }
          if('P' == ampm[0]) {
            h += 12;
          }
        }
      }
    }
  }

  tm time = {0};
  time.tm_year = y - 1900; // Year since 1900
  time.tm_mon = M - 1;     // 0-11
  time.tm_mday = d;        // 1-31
  time.tm_hour = h;        // 0-23
  time.tm_min = m;         // 0-59
  time.tm_sec = s;         // 0-61 (0-60 in C++11)

  return timegm(&time);
}

int get_watt(const char *val)
{
  float number = 0.0;
  if(1 != sscanf(val, "%f", &number)) {
    throw std::invalid_argument("Not a number");
  }

  if(kw) {
    number *= 1000;
  }

  return (int)round(number);
}

time_t divertmode_get_time()
{
  return simulated_time;
}

int main(int argc, char** argv)
{
  int voltage_arg = -1;
  std::string sep = ",";
  std::string config;

  cxxopts::Options options(argv[0], " - example command line options");
  options
    .positional_help("[optional args]")
    .show_positional_help();

  options
    .add_options()
    ("help", "Print help")
    ("d,date", "The date column", cxxopts::value<int>(date_col), "N")
    ("s,solar", "The solar column", cxxopts::value<int>(solar_col), "N")
    ("g,gridie", "The Grid IE column", cxxopts::value<int>(grid_ie_col), "N")
    ("c,config", "Config options, either a file name or JSON", cxxopts::value<std::string>(config))
    ("v,voltage", "The Voltage column if < 50, else the fixed voltage", cxxopts::value<int>(voltage_arg), "N")
    ("kw", "values are KW")
    ("sep", "Field separator", cxxopts::value<std::string>(sep))
    ("config-check", "Output the config and exit")
    ("config-load", "Simulate loading config from EEPROM")
    ("config-commit", "Simulate saving the config to EEPROM");

  auto result = options.parse(argc, argv);

  if (result.count("help"))
  {
    std::cout << options.help({"", "Group"}) << std::endl;
    exit(0);
  }

  fs::EpoxyFS.begin();
  if(result.count("config-load") > 0) {
    config_load_settings();
  } else {
    config_reset();
  }

  // If config is set and not a JSON string, assume it is a file name
  if(config.length() > 0 && config[0] != '{')
  {
    std::ifstream t(config);
    std::stringstream buffer;
    buffer << t.rdbuf();
    config = buffer.str();
  }
  // If we have some JSON load it
  if(config.length() > 0 && config[0] == '{') {
    config_deserialize(config.c_str());
  }

  if(result.count("config-commit") > 0) {
    config_commit();
  }

  if(result.count("config-check")) 
  {
    String config_out;
    config_serialize(config_out, true, false, false);
    std::cout << config_out.c_str() << std::endl;
    return EXIT_SUCCESS;
  }

  kw = result.count("kw") > 0;

  divert_type = grid_ie_col >= 0 ? 1 : 0;
  
  if(voltage_arg >= 0) {
    if(voltage_arg < 50) {
      voltage_col = voltage_arg;
    } else {
      voltage = voltage_arg;
    }
  }

  solar = 0;
  grid_ie = 0;

  evse.begin();
  divert.begin();

  // Initialise the EVSE Manager
  while (!evse.isConnected()) {
    MicroTask.update();
  }

  divert.setMode(DivertMode::Eco);

  CsvParser parser(std::cin);
  parser.delimiter(sep.c_str()[0]);
  int row_number = 0;

  std::cout << "Date,Solar,Grid IE,Pilot,Charge Power,Min Charge Power,State,Smoothed Available" << std::endl;
  for (auto& row : parser)
  {
    try
    {
      int col = 0;
      std::string val;

      for (auto& field : row)
      {
        val = field;
        if(date_col == col) {
          simulated_time = parse_date(val.c_str());
        } else if (grid_ie_col == col) {
          grid_ie = get_watt(val.c_str());
        } else if (solar_col == col) {
          solar = get_watt(val.c_str());
        } else if (voltage_col == col) {
          voltage = stoi(field);
        }

        col++;
      }

      if(last_time != 0)
      {
        int delta = simulated_time - last_time;
        if(delta > 0) {
          EpoxyTest::add_millis(delta * 1000);
        }
      }
      last_time = simulated_time;

      divert.update_state();
      MicroTask.update();

      tm tm;
      gmtime_r(&simulated_time, &tm);

      char buffer[32];
      std::strftime(buffer, 32, "%d/%m/%Y %H:%M:%S", &tm);

      int ev_pilot = (OPENEVSE_STATE_CHARGING == state ? pilot : 0);
      int ev_watt = ev_pilot * voltage;
      int min_ev_watt = 6 * voltage;

      double smoothed = divert.smoothedAvailableCurrent() * voltage;

      std::cout << buffer << "," << solar << "," << grid_ie << "," << ev_pilot << "," << ev_watt << "," << min_ev_watt << "," << state << "," << smoothed << std::endl;
    }
    catch(const std::invalid_argument& e)
    {
    }
  }
}

void event_send(String event)
{
}

void event_send(JsonDocument &event)
{
}

void emoncms_publish(JsonDocument &data)
{
}
