#include "CsvSimulationEvents.h"
#include "utils.h"
#include <stdexcept>
#include <cmath>
#include <string>

using namespace aria::csv;


bool CsvSimulationEvents::open(const char *filename, char sep)
{
  if(filename)
  {
    if(filename[0] == '-' && filename[1] == '\0') {
      filename = nullptr;
    } else {
      _file.open(filename);
      if(!_file.is_open()) {
        return false;
      }
    }
  }

  _parser = new CsvParser(filename ? _file : std::cin);
  _parser->delimiter(sep);

  // Skip the header
  for(auto field = _parser->next_field();
      field.type == FieldType::DATA;
      field = _parser->next_field())
  {
  }
  processLine();

  return true;
}

time_t CsvSimulationEvents::getNextEventTime()
{
  return _event_time;
}

void CsvSimulationEvents::processEvent(EvseEngine &engine)
{
  if(_grid_ie_col >= 0) {
    engine.setGridIe(_grid_ie);
  }
  if(_solar_col >= 0) {
    engine.setSolar(_solar);
  }
  if(_voltage_col >= 0) {
    engine.setVoltage(_voltage);
  }

  processLine();
}

CsvSimulationEvents::CsvLineData CsvSimulationEvents::readLine()
{
  CsvLineData data;

  try
  {
    int col = 0;

    FieldType field_type;

    do
    {
      auto field = _parser->next_field();

      field_type = field.type;
      switch (field_type)
      {
        case FieldType::DATA: {
          const std::string *val = field.data;
          if(_date_col == col) {
            data.event_time = parse_date(val->c_str());
            data.event_time_set = true;
          } else if (_grid_ie_col == col) {
            data.grid_ie = get_watt(val->c_str(), _kw);
            data.grid_ie_set = true;
          } else if (_solar_col == col) {
            data.solar = get_watt(val->c_str(), _kw);
            data.solar_set = true;
          } else if (_voltage_col == col) {
            data.voltage = std::stoi(*val);
            data.voltage_set = true;
          }

          col++;
        } break;

        case FieldType::CSV_END:
          _finished = true;
          break;

        default:
          break;
      }
    } while(field_type == FieldType::DATA);
  }
  catch(const std::invalid_argument& e)
  {
  }

  return data;
}

void CsvSimulationEvents::processLine()
{
  _valid_line = false;

  while(!_finished)
  {
    CsvLineData data = readLine();

    // Have we got all the data we need?
    if((data.event_time_set || -1 == _date_col) &&
       (data.grid_ie_set || -1 == _grid_ie_col) &&
       (data.solar_set || -1 == _solar_col) &&
       (data.voltage_set || -1 == _voltage_col))
    {
      _event_time = data.event_time;
      _grid_ie = data.grid_ie;
      _solar = data.solar;
      _voltage = data.voltage;
      _valid_line = true;
      break;
    }
  }
}
