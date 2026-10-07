#ifndef REPLAY_REDACT_H
#define REPLAY_REDACT_H

#include <ArduinoJson.h>

// The replay package (/debug/replay) carries the configuration the simulator
// needs to reproduce charging behaviour -- and nothing that could identify or
// authenticate anybody.
//
// An ALLOWLIST, never a blocklist (same rule as crash_redact): an option added
// later is left out until someone decides it is both safe and useful here.
bool replay_redact_key_allowed(const char *key);

// Placeholder tag the replay tool presents for an authorised RFID card. The
// stored tags themselves never leave the device: `rfid_storage` is rewritten
// to one placeholder per stored tag.
#define REPLAY_RFID_PLACEHOLDER "REPLAY"

// Copy the allowed keys from `full` into `out`, and rewrite rfid_storage.
void replay_redact_config(JsonObjectConst full, JsonObject out);

#endif // REPLAY_REDACT_H
