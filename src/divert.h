// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#ifndef _EMONESP_DIVERT_H
#define _EMONESP_DIVERT_H

#include <Arduino.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "input_filter.h"

enum divert_type {
  DIVERT_TYPE_UNSET = -1,
  DIVERT_TYPE_SOLAR = 0,
  DIVERT_TYPE_GRID = 1
};

extern int solar;
extern int grid_ie;

class DivertMode
{
  public:
    enum Value : uint8_t {
      Normal = 1,
      Eco = 2
    };

  DivertMode() = default;
  constexpr DivertMode(Value value) : _value(value) { }
  constexpr DivertMode(long value) : _value((Value)value) { }

  operator Value() const { return _value; }
  explicit operator bool() = delete;        // Prevent usage: if(state)
  DivertMode operator= (const Value val) {
    _value = val;
    return *this;
  }
  DivertMode operator= (const long val) {
    _value = (Value)val;
    return *this;
  }

  private:
    Value _value;
};

class DivertTask : public MicroTasks::Task
{
  private:
    // global variable
    EvseManager *_evse;
    DivertMode _mode;
    EvseState _state;
    uint32_t _last_update;
    MicroTasks::EventListener _evseState;
    double _available_current;
    double _smoothed_available_current;
    time_t _min_charge_end;
    uint8_t _evse_last_state;
    InputFilter _inputFilter;

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);
    double power_w_to_current_a(double p);

  public:
    DivertTask(EvseManager &evse);
    ~DivertTask();

    void begin();

    // Change mode
    void setMode(DivertMode mode);
    DivertMode getMode() {
      return _mode;
    }

    uint32_t lastUpdate() {
      return _last_update;
    }


    double availableCurrent() {
      return _available_current;
    }

    double smoothedAvailableCurrent() {
      return _smoothed_available_current;
    }

    // Set charge rate depending on charge mode and solarPV output
    void update_state();

    EvseState getState() {
      return _state;
    }

    uint32_t getLastUpdate() {
      return _last_update;
    }

    time_t getMinChargeTimeRemaining();

    bool isActive();
    void initDivertType();
};

extern class DivertTask divert;

#endif // _EMONESP_DIVERT_H
