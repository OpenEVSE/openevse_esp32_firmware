#include <iostream>

#include "Console.h"
#include "emonesp.h"
#include "RapiSender.h"
#include "openevse.h"
#include "divert.h"

#include "parser.hpp"

using namespace aria::csv;

RapiSender rapiSender(&RAPI_PORT);

long pilot = 0;                       // OpenEVSE Pilot Setting
long state = OPENEVSE_STATE_SLEEPING; // OpenEVSE State
String mqtt_solar = "";
String mqtt_grid_ie = "";

int date_col = 0;
int grid_ie_col = -1;
int solar_col = 1;

int main(int, char**)
{
  mqtt_solar = "yes";
  mqtt_grid_ie = "";

  solar = 0;
  grid_ie = 0;

  divertmode_update(DIVERT_MODE_ECO);

  CsvParser parser(std::cin);
  int row_number = 0;
  for (auto& row : parser)
  {
    try
    {    
      int col = 0;
      std::string date;

      for (auto& field : row)
      {
        if(date_col == col) {
          date = field;
        } else if (grid_ie_col == col) {
          grid_ie = stoi(field);
        } else if (solar_col == col) {
          solar = stoi(field);
        }

        col++;
      }
      divert_update_state();
      std::cout << date << "," << solar << "," << grid_ie << "," << charge_rate << std::endl;
    }
    catch(const std::invalid_argument& e)
    {
    }
  }
}

void event_send(String event)
{
}
