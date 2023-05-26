#include "CsvSimulationEvents.h"
#include "utils.h"
#include <stdexcept>
#include <cmath>
#include <string>

using namespace aria::csv;

bool CsvSimulationEvents::open(const char *filename, char sep)
{
  if(filename) {
    _file.open(filename);
    if(!_file.is_open()) {
      return false;
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

void CsvSimulationEvents::processLine()
{
  try
  {
    int col = 0;

    for(;;)
    {
      auto field = _parser->next_field();
      switch (field.type)
      {
        case FieldType::DATA: {
          const std::string *val = field.data;
          if(_date_col == col) {
            _event_time = parse_date(val->c_str());
          } else if (_grid_ie_col == col) {
            _grid_ie = get_watt(val->c_str(), _kw);
          } else if (_solar_col == col) {
            _solar = get_watt(val->c_str(), _kw);
          } else if (_voltage_col == col) {
            _voltage = std::stoi(*val);
          }

          col++;
        } break;

        case FieldType::ROW_END:
          return;

        case FieldType::CSV_END:
          _finished = true;
          return;
      }
    }

  }
  catch(const std::invalid_argument& e)
  {
  }
}
