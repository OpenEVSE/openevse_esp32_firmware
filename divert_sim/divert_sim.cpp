#include <iostream>

#define RAPI_PORT Console

#include "Console.h"
#include "emonesp.h"
#include "RapiSender.h"
#include "openevse.h"
#include "divert.h"

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

time_t parse_date(const char *dateStr)
{
  int y,M,d,h,m,s;
  if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%dZ", &y, &M, &d, &h, &m, &s)) {
    if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%d+00:00", &y, &M, &d, &h, &m, &s)) {
      if(6 != sscanf(dateStr, "%d-%d-%d %d:%d:%d", &y, &M, &d, &h, &m, &s)) {
        if(1 == sscanf(dateStr, "%d", &s)) {
          return s;
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

int main(int argc, char** argv)
{
  cxxopts::Options options(argv[0], " - example command line options");
  options
    .positional_help("[optional args]")
    .show_positional_help();

  options
    .add_options()
    ("help", "Print help")
    ("d,date", "The date column", cxxopts::value<int>(date_col), "N")
    ("s,solar", "The solar column", cxxopts::value<int>(solar_col), "N")
    ("g,gridie", "The Grid IE column", cxxopts::value<int>(grid_ie_col), "N");

  auto result = options.parse(argc, argv);

  if (result.count("help"))
  {
    std::cout << options.help({"", "Group"}) << std::endl;
    exit(0);
  }

  mqtt_solar = grid_ie_col >= 0 ? "" : "yes";
  mqtt_grid_ie = grid_ie_col >= 0 ? "yes" : "";

  solar = 0;
  grid_ie = 0;

  divertmode_update(DIVERT_MODE_ECO);

  CsvParser parser(std::cin);
  int row_number = 0;
  
  std::cout << "Date,Solar,Grid IE,Pilot" << std::endl;
  for (auto& row : parser)
  {
    try
    {    
      int col = 0;
      std::string date;
      time_t time;

      for (auto& field : row)
      {
        if(date_col == col) {
          date = field;
          time = parse_date(date.c_str());
        } else if (grid_ie_col == col) {
          grid_ie = stoi(field);
        } else if (solar_col == col) {
          solar = stoi(field);
        }

        col++;
      }
      divert_update_state();

      tm tm;
      gmtime_r(&time, &tm);

      char buffer[32];
      std::strftime(buffer, 32, "%d/%m/%Y %H:%M:%S", &tm);  

      std::cout << buffer << "," << solar << "," << grid_ie << "," << (OPENEVSE_STATE_CHARGING == state ? pilot : 0) << std::endl;
    }
    catch(const std::invalid_argument& e)
    {
    }
  }
}

void event_send(String event)
{
}
