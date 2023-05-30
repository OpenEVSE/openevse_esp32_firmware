#ifndef CLOCK_EVENT_H
#define CLOCK_EVENT_H

#include "SimulationEvents.h"
#include "parser.hpp"

class ClockEvents : public SimulationEvents
{
  private:
    time_t _event_time = 0;
    time_t _start_time = 0;
    time_t _end_time = 0;
    int _interval = 0;

  public:
    ClockEvents(
      time_t start_time,
      time_t end_time,
      int interval) :
      _event_time(start_time),
      _start_time(start_time),
      _end_time(end_time),
      _interval(interval) {};
    ~ClockEvents() {};

    virtual bool hasMoreEvents() { return _event_time < _end_time; }
    virtual time_t getNextEventTime() { return _event_time; }
    virtual void processEvent(EvseEngine &engine) { _event_time += _interval; }
};


#endif // CLOCK_EVENT_H
