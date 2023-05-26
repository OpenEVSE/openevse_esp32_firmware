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
    time_t _event_time = 0;
    int _grid_ie = 0;
    int _solar = 0;
    int _voltage = 0;

  public:
    CsvSimulationEvents() {};
    ~CsvSimulationEvents() {};

    bool open(const char *filename = nullptr, char sep = ',');

    void setDateCol(int col) { _date_col = col; }
    void setGridIeCol(int col) { _grid_ie_col = col; }
    void setSolarCol(int col) { _solar_col = col; }
    void setVoltageCol(int col) { _voltage_col = col; }
    void setKw(bool kw) { _kw = kw; }

    virtual bool hasMoreEvents() { return !_finished; }
    virtual time_t getNextEventTime();
    virtual void processEvent(EvseEngine &engine);

  private:
    void processLine();
};


#endif // CSV_SIMULATION_EVENT_H
