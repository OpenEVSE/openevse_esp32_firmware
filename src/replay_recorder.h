#ifndef REPLAY_RECORDER_H
#define REPLAY_RECORDER_H

// Replay recorder: keeps the last hour of what the charger saw and decided,
// so a user can download it (/debug/replay) and the divert_sim simulator can
// replay the same inputs through the same firmware logic
// (divert_sim/replay.py).
//
// Passive: it polls public getters once a second and never drives anything.
// RAM: REPLAY_SAMPLES * 26 B + REPLAY_EVENTS * 24 B, allocated in begin().

#ifndef ENABLE_REPLAY_RECORDER
#define ENABLE_REPLAY_RECORDER 1
#endif

#if ENABLE_REPLAY_RECORDER

#include <Arduino.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "replay_format.h"
#include "replay_ring.h"

#ifndef REPLAY_SAMPLE_INTERVAL_MS
#define REPLAY_SAMPLE_INTERVAL_MS 10000
#endif
#ifndef REPLAY_SAMPLES
#define REPLAY_SAMPLES 360          // 1 hour at 10 s
#endif
#ifndef REPLAY_EVENTS
#define REPLAY_EVENTS 128
#endif

class DivertTask;
class CurrentShaperTask;
class Limit;
class Boost;
class RfidTask;
class Scheduler;

class ReplayRecorder : public MicroTasks::Task
{
  private:
    EvseManager *_evse = nullptr;
    DivertTask *_divert = nullptr;
    CurrentShaperTask *_shaper = nullptr;
    Limit *_limit = nullptr;
    Boost *_boost = nullptr;
    RfidTask *_rfid = nullptr;
    Scheduler *_scheduler = nullptr;

    ReplayRing<ReplaySample> _samples;
    ReplayRing<ReplayEvent> _events;

    ReplayClaim _claims[EVSE_MANAGER_MAX_CLIENT_CLAIMS];
    size_t _claim_count = 0;

    uint8_t _claims_version = 0;
    uint8_t _limit_version = 0;
    uint8_t _boost_version = 0;
    uint32_t _schedule_version = 0;
    uint32_t _config_version = 0;
    bool _rfid_auth = false;
    bool _primed = false;

    uint64_t _uptime_ms = 0;
    unsigned long _last_millis = 0;
    uint64_t _next_sample_ms = 0;

    uint32_t uptime() const { return (uint32_t)(_uptime_ms / 1000); }
    void takeSample();
    void pollChanges();
    size_t readClaims(ReplayClaim *out, size_t max);
    void pushEvent(ReplayEventType type, const ReplayClaim *claim = nullptr,
                   uint8_t state = REPLAY_STATE_NONE, uint32_t value = 0,
                   bool auto_release = false);
    void recordLimit();
    void recordBoost();

    void writeEvents(Print &out, size_t max_events);
    void writeSamples(Print &out, size_t max_samples);

  protected:
    void setup() override;
    unsigned long loop(MicroTasks::WakeReason reason) override;

  public:
    void begin(EvseManager &evse, DivertTask &divert, CurrentShaperTask &shaper,
               Limit &limit, Boost &boost, RfidTask &rfid, Scheduler &scheduler);

    bool ready() const { return _samples.ready() && _events.ready(); }

    // Write the replay package (JSON) to `out`. `budget` caps its size in
    // bytes: when the whole recording does not fit, the newest samples and
    // events that do are written and the package says how many were left out.
    void serialize(Print &out, size_t budget = SIZE_MAX);
};

extern ReplayRecorder replayRecorder;

#endif // ENABLE_REPLAY_RECORDER
#endif // REPLAY_RECORDER_H
