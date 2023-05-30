#include <sys/time.h>
#include <string>

#include "StdioSerial.h"
#include "RapiSender.h"
#include "openevse.h"
#include "divert.h"
#include "event.h"
#include "event_log.h"
#include "manual.h"

#include "cxxopts.hpp"

#include <MicroTasks.h>
#include <EpoxyFS.h>

#include <epoxy_test/ArduinoTest.h>

#include "OpenEvseSimulator.h"
//#include "AllEvents.h"
#include "CsvSimulationEvents.h"
//#include "ClockEvents.h"
#include "utils.h"

OpenEvseSimulator evse_simulator;
EventLog eventLog;
EvseManager evse(evse_simulator.stream(), eventLog);
DivertTask divert(evse);
ManualOverride manual(evse);

double voltage = 240; // Voltage from OpenEVSE or MQTT

extern double smoothed_available_current;

int date_col = 0;
int grid_ie_col = -1;
int solar_col = 1;
int voltage_col = -1;

time_t simulated_time = 0;
time_t last_time = 0;
unsigned long last_millis = 0;

class EvseWatcherTask : public MicroTasks::Task
{
  private:
    MicroTasks::EventListener _evseBoot;

    bool _booted = false;

  protected:
    void setup()
    {
      evse.onBootReady(&_evseBoot);
    }

    unsigned long loop(MicroTasks::WakeReason reason)
    {
      DBUG("EvseWatcherTask woke: ");
      DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
            WakeReason_Event == reason ? "WakeReason_Event" :
            WakeReason_Message == reason ? "WakeReason_Message" :
            WakeReason_Manual == reason ? "WakeReason_Manual" :
            "UNKNOWN");

      if(_evseBoot.IsTriggered()) {
        _booted = true;
      }

      return MicroTask.Infinate;
    }
  public:
    EvseWatcherTask() :
      MicroTasks::Task(),
      _evseBoot(this)
    {

    }

    bool booted() const {
      return _booted;
    }
} evse_watcher;


time_t divertmode_get_time()
{
  return simulated_time;
}

int main(int argc, char** argv)
{
  int voltage_arg = -1;
  std::string sep = ",";
  std::string config;
  std::string time_start;
  std::string time_end;
  int time_increment;
  std::string events;
  std::string schedule;
  std::string csv_file;

  cxxopts::Options options(argv[0], " - example command line options");
  options
    .positional_help("[optional args]")
    .show_positional_help();

  options
    .add_options()
    ("help", "Print help")
    ("i,input", "CSV file with data feeds", cxxopts::value<std::string>(csv_file), "N")
    ("d,date", "The date column", cxxopts::value<int>(date_col), "N")
    ("s,solar", "The solar column", cxxopts::value<int>(solar_col), "N")
    ("g,gridie", "The Grid IE column", cxxopts::value<int>(grid_ie_col), "N")
    ("c,config", "Config options, either a file name or JSON", cxxopts::value<std::string>(config))
    ("v,voltage", "The Voltage column if < 50, else the fixed voltage", cxxopts::value<int>(voltage_arg), "N")
    ("e,events", "Vehicle events JSON file", cxxopts::value<std::string>(events), "N")
    ("schedule", "Schedule JSON file", cxxopts::value<std::string>(schedule), "N")
    ("time-start", "start of the simulated time", cxxopts::value<std::string>(time_start), "N")
    ("time-end", "end of the simulated time", cxxopts::value<std::string>(time_end), "N")
    ("time-increment", "Number of seconds to increment the time", cxxopts::value<int>(time_increment), "N")
    ("kw", "values are KW")
    ("sep", "Field separator", cxxopts::value<std::string>(sep));

  auto result = options.parse(argc, argv);

  if (result.count("help"))
  {
    std::cout << options.help({"", "Group"}) << std::endl;
    exit(0);
  }

  fs::EpoxyFS.begin();
  config_reset();

  // If config is set and not a JSON string, assume it is a file name
  if(config.length() > 0 && config[0] != '{')
  {
    std::ifstream t(config);
    std::stringstream buffer;
    buffer << t.rdbuf();
    config = buffer.str();
  }
  // If we have some JSON load it
  if(config.length() > 0 && config[0] == '{') {
    config_deserialize(config.c_str());
  }

  bool kw = result.count("kw") > 0;

  divert_type = grid_ie_col >= 0 ? 1 : 0;

  if(voltage_arg >= 0) {
    if(voltage_arg < 50) {
      voltage_col = voltage_arg;
    } else {
      voltage = voltage_arg;
    }
  }

  solar = 0;
  grid_ie = 0;

  EpoxyTest::set_millis(millis());

  MicroTask.startTask(&evse_watcher);
  evse_simulator.begin();
  evse.begin();
  divert.begin();

  // Initialise the EVSE Manager
  while (!evse_watcher.booted()) {
    MicroTask.update();
    EpoxyTest::add_millis(10);
  }

  divert.setMode(DivertMode::Eco);

//  AllEvents eventSources;
  CsvSimulationEvents csvEvents;

  if(csv_file.length() > 0)
  {
//    CsvSimulationEvents csvEvents;
    csvEvents.setDateCol(date_col);
    csvEvents.setSolarCol(solar_col);
    csvEvents.setGridIeCol(grid_ie_col);
    csvEvents.setVoltageCol(voltage_col);
    csvEvents.setKw(kw);

    if(csvEvents.open(csv_file, sep[0])) {
//      eventSources.addEventSource(csvEvents);
    }
  }

//  if(time_start.length() > 0 && time_end.length() > 0 && time_increment > 0)
//  {
//    ClockEvents clockEvents(
//      parse_date(time_start.c_str()),
//      parse_date(time_end.c_str()),
//      time_increment);
//    eventSources.addEventSource(clockEvents);
//  }

  int row_number = 0;

  EvseEngine engine(evse, solar, grid_ie, voltage);

  last_millis = millis();

  std::cout << "Date,Solar,Grid IE,Pilot,Charge Power,Min Charge Power,State,Smoothed Available" << std::endl;
  //while(eventSources.hasMoreEvents())
  while(csvEvents.hasMoreEvents())
  {
    //simulated_time = eventSources.getNextEventTime();
    //eventSources.processEvent(engine);
    simulated_time = csvEvents.getNextEventTime();
    csvEvents.processEvent(engine);

    if(last_time != 0)
    {
      int delta = simulated_time - last_time;
      if(delta > 0) {
        last_millis += delta * 1000;
        EpoxyTest::set_millis(last_millis);
      }
    }
    last_time = simulated_time;

    divert.update_state();
    for (size_t i = 0; i < 10; i++) {
      MicroTask.update();
      EpoxyTest::add_millis(10);
    }

    tm tm;
    gmtime_r(&simulated_time, &tm);

    char buffer[32];
    std::strftime(buffer, 32, "%d/%m/%Y %H:%M:%S", &tm);

    int ev_pilot = (OPENEVSE_STATE_CHARGING == evse_simulator.state() ? evse_simulator.pilot() : 0);
    int ev_watt = ev_pilot * voltage;
    int min_ev_watt = 6 * voltage;

    double smoothed = divert.smoothedAvailableCurrent() * voltage;

    std::cout << buffer << "," << solar << "," << grid_ie << "," << ev_pilot << "," << ev_watt << "," << min_ev_watt << "," << evse_simulator.state() << "," << smoothed << std::endl;
  }
}

void event_send(String event)
{
}

void event_send(JsonDocument &event)
{
}

void emoncms_publish(JsonDocument &data)
{
}
