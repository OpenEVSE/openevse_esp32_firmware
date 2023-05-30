#include "AllEvents.h"

void AllEvents::addEventSource(SimulationEvents *eventSource)
{
  _eventSources.push_back(eventSource);
  if(_currentEventSource == nullptr || eventSource->getNextEventTime() < _currentEventSource->getNextEventTime()) {
    _currentEventSource = eventSource;
  }
}

void AllEvents::processEvent(EvseEngine &engine)
{
  if(_currentEventSource != nullptr)
  {
    _currentEventSource->processEvent(engine);
    _currentEventSource = nullptr;
    for(auto eventSource : _eventSources)
    {
      if(eventSource->hasMoreEvents() &&
         (_currentEventSource == nullptr ||
          eventSource->getNextEventTime() < _currentEventSource->getNextEventTime()
         )
        )
      {
        _currentEventSource = eventSource;
      }
    }
  }
}
