#ifndef CRASH_REDACT_H
#define CRASH_REDACT_H

#include <ArduinoJson.h>

// Spec §5. The crash report carries enough configuration to explain a crash
// and nothing that could authenticate anybody.
//
// An ALLOWLIST, never a blocklist: a blocklist looks correct right up until
// someone adds a credential-bearing option and does not think about this file,
// and then it silently leaks. The default answer for an unrecognised key is no.
bool crash_redact_key_allowed(const char *key);

// Copy the allowed keys from `full` into `out`, leaving everything else behind.
void crash_redact_config(JsonObjectConst full, JsonObject out);

#endif // CRASH_REDACT_H
