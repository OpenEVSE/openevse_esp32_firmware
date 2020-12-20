#ifndef _OPENEVSE_TIME_H
#define _OPENEVSE_TIME_H

#include <MongooseSntpClient.h>
#include <MicroTasks.h>
#include <MicroTasksTask.h>

class TimeManager : public MicroTasks::Task
{
  private:
    const char *_timeHost;
    MongooseSntpClient _sntp;
    unsigned long _nextCheckTime;
    bool _fetchingTime;
    bool _setTheTime;

    class TimeChange : public MicroTasks::Event
    {
      public:
        void Trigger() {
          Event::Trigger(false);
        }
    } _timeChange;

    double diffTime(timeval tv1, timeval tv2);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    TimeManager();

    void begin();

    void setHost(const char *host);
    void setTime(struct timeval setTime, const char *source);

    void checkNow() {
      _nextCheckTime = millis();
      MicroTask.wakeTask(this);
    }

    // Register for events
    void onTimeChange(MicroTasks::EventListener *listner) {
      _timeChange.Register(listner);
    }
};

extern TimeManager timeManager;

extern void time_check_now();
extern void time_set_time(struct timeval set_time, const char *source);

extern String time_format_time(time_t time);
extern String time_format_time(tm &time);

#endif // _OPENEVSE_TIME_H
