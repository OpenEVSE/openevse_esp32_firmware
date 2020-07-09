#include <iostream>
#include <sys/time.h>
#include <string>

#ifndef RAPI_PORT
#define RAPI_PORT Console
#endif

#include "Console.h"
#include "emonesp.h"
#include "RapiSender.h"
#include "openevse.h"
#include "divert.h"
#include "emonesp.h"
#include "event.h"

#include "parser.hpp"
#include "cxxopts.hpp"

using namespace aria::csv;

RapiSender rapiSender(&RAPI_PORT);

long pilot = 32;                      // OpenEVSE Pilot Setting
long state = OPENEVSE_STATE_SLEEPING; // OpenEVSE State
String mqtt_solar = "";
String mqtt_grid_ie = "";

int date_col = 0;
int grid_ie_col = -1;
int solar_col = 1;
int voltage_col = 1;

time_t simulated_time = 0;

bool kw = false;

extern double smoothed_available_current;
double divert_attack_smoothing_factor = 0.4;
double divert_decay_smoothing_factor = 0.05;
uint32_t divert_min_charge_time = (10 * 60);
double voltage = DEFAULT_VOLTAGE;     // Voltage from OpenEVSE or MQTT

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

  tm time;
  time.tm_year = y - 1900; // Year since 1900
  time.tm_mon = M - 1;     // 0-11
  time.tm_mday = d;        // 1-31
  time.tm_hour = h;        // 0-23
  time.tm_min = m;         // 0-59
  time.tm_sec = s;         // 0-61 (0-60 in C++11)

  return mktime(&time);
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
    ("attack", "The attack factor for the smoothing", cxxopts::value<double>(divert_attack_smoothing_factor))
    ("decay", "The decay factor for the smoothing", cxxopts::value<double>(divert_decay_smoothing_factor))
    ("v,voltage", "The Voltage column if < 50, else the fixed voltage", cxxopts::value<int>(voltage_arg), "N")
    ("kw", "values are KW")
    ("sep", "Field separator", cxxopts::value<std::string>(sep));

  auto result = options.parse(argc, argv);

  if (result.count("help"))
  {
    std::cout << options.help({"", "Group"}) << std::endl;
    exit(0);
  }

  kw = result.count("kw") > 0;

  mqtt_solar = grid_ie_col >= 0 ? "" : "yes";
  mqtt_grid_ie = grid_ie_col >= 0 ? "yes" : "";

  if(voltage_arg >= 0) {
    if(voltage_arg < 50) {
      voltage_col = voltage_arg;
    } else {
      voltage = voltage_arg;
    }
  }

  solar = 0;
  grid_ie = 0;

  divertmode_update(DIVERT_MODE_ECO);

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

      divert_update_state();

      tm tm;
      gmtime_r(&simulated_time, &tm);

      char buffer[32];
      std::strftime(buffer, 32, "%d/%m/%Y %H:%M:%S", &tm);

      int ev_pilot = (OPENEVSE_STATE_CHARGING == state ? pilot : 0);
      int ev_watt = ev_pilot * voltage;
      int min_ev_watt = 6 * voltage;

      double smoothed = smoothed_available_current * voltage;

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
