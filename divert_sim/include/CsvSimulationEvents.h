#ifndef CSV_SIMULATION_EVENT_H
#define CSV_SIMULATION_EVENT_H

#include <iostream>

#include "SimulationEvents.h"
#include "parser.hpp"

class CsvSimulationEvents : public SimulationEvents
{
  private:
    std::ifstream _file;
    aria::csv::CsvParser *_parser;
    int _date_col = -1;
    int _grid_ie_col = -1;
    int _solar_col = -1;
    int _voltage_col = -1;
    bool _kw = false;

    bool _finished = false;
    bool _valid_line = false;
    time_t _event_time = 0;
    int _grid_ie = 0;
    int _solar = 0;
    int _voltage = 0;

    struct CsvLineData
    {
      bool event_time_set = false;
      time_t event_time = 0;
      bool grid_ie_set = false;
      int grid_ie = 0;
      bool solar_set = false;
      int solar = 0;
      bool voltage_set = false;
      int voltage = 0;
    };


  public:
    CsvSimulationEvents() {};
    ~CsvSimulationEvents() {};

    bool open(const char *filename = nullptr, char sep = ',');

    void setDateCol(int col) { _date_col = col; }
    void setGridIeCol(int col) { _grid_ie_col = col; }
    void setSolarCol(int col) { _solar_col = col; }
    void setVoltageCol(int col) { _voltage_col = col; }
    void setKw(bool kw) { _kw = kw; }

    virtual bool hasMoreEvents() { return _valid_line; }
    virtual time_t getNextEventTime();
    virtual void processEvent(EvseEngine &engine);

  private:
    CsvLineData readLine();
    void processLine();
};


#endif // CSV_SIMULATION_EVENT_H
