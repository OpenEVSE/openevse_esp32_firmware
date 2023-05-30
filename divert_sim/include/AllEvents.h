#ifndef ALL_EVENT_H
#define ALL_EVENT_H

#include "SimulationEvents.h"
#include "parser.hpp"

class AllEvents : public SimulationEvents
{
  private:
    std::vector<SimulationEvents *> _eventSources;
    SimulationEvents *_currentEventSource;

  public:
    AllEvents() :
      _eventSources(),
      _currentEventSource(nullptr)
       {};
    ~AllEvents() {};

    void addEventSource(SimulationEvents *eventSource);

    virtual bool hasMoreEvents() { return _currentEventSource != nullptr; }
    virtual time_t getNextEventTime() { return _currentEventSource != nullptr ? _currentEventSource->getNextEventTime() : 0; }
    virtual void processEvent(EvseEngine &engine);
};


#endif // ALL_EVENT_H
