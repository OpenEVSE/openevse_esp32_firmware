#ifndef SIMULATION_EVENT_H
#define SIMULATION_EVENT_H

#include <time.h>
#include "EvseEngine.h"

class SimulationEvents
{
  private:

  public:
    SimulationEvents() {};
    ~SimulationEvents() {};

    virtual bool hasMoreEvents() = 0;
    virtual time_t getNextEventTime() = 0;
    virtual void processEvent(EvseEngine &engine) = 0;
};


#endif // SIMULATION_EVENT_H
