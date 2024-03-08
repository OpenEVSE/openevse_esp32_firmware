#ifndef _ENERGY_METER_H
#define _ENERGY_METER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "emonesp.h"
#include <LittleFS.h>
#include "app_config.h"

#define MAX_INTERVAL 10000
#define EVENT_INTERVAL 5000
#define ROTATE_INTERVAL 6000
#define SAVE_INTERVAL 5 * 60 * 1000 // save to flash each 5 minutes while charging

#ifndef ENERGY_METER_FILE
#define ENERGY_METER_FILE "/emeter.json"
#endif

// to do calculate this correctly
const size_t capacity = JSON_OBJECT_SIZE(9) + JSON_OBJECT_SIZE(4) + 256;

// Forward declaration
class EvseMonitor;

struct EnergyMeterDate
{
  uint8_t day;
  uint8_t month;
  uint16_t year;
};

class EnergyMeterData
{
public:
  EnergyMeterData();
  double session;	  // wh
  double total;	  // kwh
  double daily;	  // kwh
  double weekly;	  // kwh
  double monthly;	  // kwh
  double yearly;	  // kwh
  double elapsed; // sec
  uint32_t switches; // homw many switches the relay/contactor got
  bool imported;	  // has imported old counter already
  EnergyMeterDate date;

  void reset(bool fullreset, bool import); // fullreset : set total_energy & total_switches to 0 , import: allows to reimport from evse
  void serialize(StaticJsonDocument<capacity> &doc);
  void deserialize(StaticJsonDocument<capacity> &doc);
};

class EnergyMeter
{
private:
  EnergyMeterData _data;
  uint32_t _last_upd;
  uint32_t _write_upd;
  uint32_t _event_upd;
  uint32_t _rotate_upd;
  uint8_t _switch_state; // 0: Undefined, 1: Enabled, 2: Disabled

  EvseMonitor *_monitor;

  EnergyMeterDate getCurrentDate();
  bool createEnergyMeterStorage(bool fullreset, bool forceimport);
  bool write(EnergyMeterData &data);
  void rotate();
  bool load();

public:
  EnergyMeter();
  ~EnergyMeter();
  void begin(EvseMonitor *monitor);
  void end();
  bool reset(bool full, bool import);
  bool update();
  bool publish();
  void clearSession();
  bool importTotalEnergy(double kwh);
  void createEnergyMeterJsonDoc(JsonDocument &doc);
  void increment_switch_counter();

  bool save()
  {
    return write(_data);
  }

  uint32_t getElapsed()
  {
    return _data.elapsed;
  };
  uint32_t getSwitches()
  {
    return _data.switches;
  };
  double getTotal()
  {
    return _data.total;
  };
  double getSession()
  {
    return _data.session;
  };
  double getDaily()
  {
    return _data.daily;
  };
  double getWeekly()
  {
    return _data.weekly;
  };
  double getMonthly()
  {
    return _data.monthly;
  };
  double getYearly()
  {
    return _data.yearly;
  };
  EnergyMeterDate getDate()
  {
    return _data.date;
  };
};

#endif // _ENERGY_METER_H
