#ifndef EVSE_ENGINE_H
#define EVSE_ENGINE_H

#include "evse_man.h"

class EvseEngine
{
  private:
    EvseManager &_evse;
    int &_solar;
    int &_grid_ie;
    double &_voltage;
  public:
    EvseEngine(
      EvseManager &evse,
      int &solar,
      int &grid_ie,
      double &voltage) :
      _evse(evse),
      _solar(solar),
      _grid_ie(grid_ie),
      _voltage(voltage)
      {};

    EvseManager &getEvse() { return _evse; }

    int &getSolar() { return _solar; }
    void setSolar(int solar) { _solar = solar; }

    int &getGridIe() { return _grid_ie; }
    void setGridIe(int grid_ie) { _grid_ie = grid_ie; }

    double &getVoltage() { return _voltage; }
    void setVoltage(double voltage) { _voltage = voltage; }
};



#endif // EVSE_ENGINE_H
