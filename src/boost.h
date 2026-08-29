#ifndef _OPENEVSE_BOOST_H
#define _OPENEVSE_BOOST_H

// Boost: "charge NOW until a target is reached, then hand control back."
//
// The inverse of Limit: while armed it holds an Active claim at
// EvseManager_Priority_Boost (200) — overriding Divert (50) and the
// Scheduler (100), but losing to Manual (1000), Limit (1100) and Safety —
// and when the target is reached it RELEASES the claim, so whatever was in
// control resumes. Limit, by contrast, claims Disabled when its target hits.
//
// Dimensions share Limit's type/value vocabulary but differ in basis:
//   time   — wall-clock seconds from activation (Limit: session minutes)
//   energy — Wh added since activation             (Limit: session total)
//   soc    — absolute %  (already met => arm is a no-op, no claim taken)
//   range  — absolute distance (same)
//
// One boost at a time; re-arming replaces (fresh activation snapshot).
// Not persisted: a reboot clears any active boost.

#ifndef EVSE_BOOST_LOOP_TIME
#define EVSE_BOOST_LOOP_TIME 1000
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "limit.h"            // LimitType: shared type/value vocabulary
#include "deadline_timer.h"

#define BOOST_MAX_TIME_S DEADLINE_TIMER_MAX_DURATION_S

enum BoostArmResult : int {
  Boost_Armed      = 0,
  Boost_BadRequest = -1,   // unknown/none type, zero value, bad JSON
  Boost_Unsupported = -2,  // soc/range without a vehicle data source
};

class Boost : public MicroTasks::Task
{
  private:
    EvseManager *_evse = nullptr;
    LimitType _type = LimitType::None;
    uint32_t _value = 0;
    uint32_t _deadline_ms = 0;     // Time dimension only
    uint32_t _energy_basis_wh = 0; // Energy dimension only
    time_t   _started = 0;
    uint8_t  _version = 0;
    MicroTasks::EventListener _sessionCompleteListener;

    void endBoost(const char *reason);
    void sendEvent(bool active, const char *reason);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    Boost();
    ~Boost();
    void begin(EvseManager &evse);

    int arm(LimitType type, uint32_t value);
    int arm(const char *json);
    bool cancel();

    bool isActive() { return _type != LimitType::None; }
    uint8_t getVersion() { return _version; }
    LimitType getType() { return _type; }
    uint32_t getValue() { return _value; }
    time_t getStarted() { return _started; }
    uint32_t getRemaining();
    void serialize(JsonDocument &doc);
};

extern Boost boost;

#endif // _OPENEVSE_BOOST_H
