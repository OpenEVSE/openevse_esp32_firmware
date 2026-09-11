#ifndef _OPENEVSE_NOTIFICATIONS_H
#define _OPENEVSE_NOTIFICATIONS_H

// The advisory engine: reads EvseMonitor's cache on a slow loop, evaluates the
// rule table, applies persisted acks and publishes the result.
//
// Advisories sit between live faults (fault_screen, full screen, blocks
// charging) and the event log (history). Nothing here ever describes a
// condition that is stopping the EVSE charging right now.

#ifndef EVSE_NOTIFICATIONS_LOOP_TIME
#define EVSE_NOTIFICATIONS_LOOP_TIME 5000
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "notifications_rules.h"
#include "notifications_acks.h"

class Notifications : public MicroTasks::Task
{
  private:
    EvseManager *_evse;

    Notification _live[NOTIFICATION_MAX];
    size_t       _count;

    NotificationAck _acks[NOTIFICATION_ACK_MAX];
    size_t          _ack_count;

    // first_seen / last_seen per live advisory, parallel to _live.
    uint32_t _first_seen[NOTIFICATION_MAX];
    uint32_t _last_seen[NOTIFICATION_MAX];

    // Ids already logged to the event log this boot: caps event-log rows at
    // one per advisory id per boot so a flapping thermal rule cannot flood
    // History. Keyed on id (a string literal from the rule table, safe to
    // store as a pointer), not on array index - an index shifts as advisories
    // come and go, an id does not.
    const char *_logged_ids[NOTIFICATION_MAX];
    size_t      _logged_count;

    void gather(NotificationInputs &in);
    void saveAcks();

    // Compacts the currently-live advisories that are NOT acked into `out`
    // (sized NOTIFICATION_MAX by every caller). Shared by maxSeverity() and
    // worst() so the tie-break rule lives once, in notifications_rules.cpp.
    size_t unmuted(Notification *out);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    Notifications();

    void begin(EvseManager &evse);

    // Unmuted advisories only - what the badge and the LCD count.
    size_t count();
    uint8_t maxSeverity();

    // The advisory the LCD should name. False when there is nothing to name.
    bool worst(const char *&id, uint8_t &severity);

    // Ack by id. False when the id is not currently live.
    bool ack(const char *id);

    // The full list, muted entries included.
    void serialize(JsonDocument &doc);
};

extern Notifications notifications;

// "info" / "warning" / "critical" for a NotificationSeverity value. Shared by
// Notifications::serialize() and the /status "notifications" object so both
// endpoints name severities the same way.
const char *notification_severity_name(uint8_t severity);

// Short, upper-case English for the LCD line. Never a number: the detail lives
// on the GUI's Monitoring -> Health page, which this points people at.
const char *notification_short_text(const char *id);

#endif // _OPENEVSE_NOTIFICATIONS_H
