#ifndef _OPENEVSE_TIME_H
#define _OPENEVSE_TIME_H

#include <MongooseSntpClient.h>
#include <MicroTasks.h>
#include <MicroTasksTask.h>
#include <ArduinoJson.h>

class TimeManager : public MicroTasks::Task
{
  private:
    const char *_timeHost;
    MongooseSntpClient _sntp;
    unsigned long _nextCheckTime;
    bool _fetchingTime;
    bool _setTheTime;
    bool _sntpEnabled;

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
    bool setTimeZone(String tz);

    bool isSntpEnabled() {
      return _sntpEnabled;
    }
    void setSntpEnabled(bool enabled);

    void checkNow() {
      _nextCheckTime = millis();
      MicroTask.wakeTask(this);
    }

    // Register for events
    void onTimeChange(MicroTasks::EventListener *listner) {
      _timeChange.Register(listner);
    }

    void serialise(JsonDocument &doc);
};

extern TimeManager timeManager;

extern void time_set_time(struct timeval set_time, const char *source);

extern String time_format_time(time_t time, bool local_time = true);
extern String time_format_time(tm &time);

#endif // _OPENEVSE_TIME_H
