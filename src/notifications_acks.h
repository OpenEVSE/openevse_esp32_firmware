#ifndef _OPENEVSE_NOTIFICATIONS_ACKS_H
#define _OPENEVSE_NOTIFICATIONS_ACKS_H

// Persisted acknowledgements, encoded as "key:hextoken;" repeated.
//
// An ack stores the token the advisory carried when it was acked. It applies
// only while the live advisory still carries that token, which is what stops a
// persisted ack from ever hiding something new: a later GFI trip moves the
// count, a settings change moves the safety token, and the stored ack simply
// stops matching.
//
// Pure: no Arduino, no String, host-testable.

#include <stdint.h>
#include <stddef.h>
#include "notifications_rules.h"

// Matches NOTIFICATION_MAX: 16 rules today, with headroom. Sizing this to the
// rule count (rather than lower) means a fully-acked charger can never fill
// the store and silently refuse the next ack.
#define NOTIFICATION_ACK_MAX 20

// Keys are the 2-character codes from the rule table, with room for a NUL and
// one character of growth.
struct NotificationAck {
  char     key[4];
  uint32_t token;
};

// Parse "sg:1234;fg:3;" into `out`. Tolerates an empty or malformed string by
// returning what it could parse - a corrupt blob must degrade to "nothing is
// acked", never to a crash or to a wrong mute.
size_t notification_acks_decode(const char *s, NotificationAck *out, size_t max);

// Render `acks` back to the same form. Always NUL-terminates. Returns the
// number of entries written, which is fewer than `count` if out_size ran out.
size_t notification_acks_encode(const NotificationAck *acks, size_t count, char *out, size_t out_size);

// True when `key` is acked AND the stored token still matches `token`.
bool notification_acks_is_acked(const NotificationAck *acks, size_t count, const char *key, uint32_t token);

// Record an ack, replacing any existing entry for the key. Returns the new
// count. A full store drops the request rather than evicting someone else's.
size_t notification_acks_set(NotificationAck *acks, size_t count, size_t max, const char *key, uint32_t token);

// Drop acks whose advisory is no longer live, so the blob cannot grow without
// bound across a charger's lifetime. Returns the new count.
size_t notification_acks_prune(NotificationAck *acks, size_t count, const Notification *live, size_t live_count);

#endif // _OPENEVSE_NOTIFICATIONS_ACKS_H
