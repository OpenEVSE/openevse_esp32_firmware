#ifndef CRASH_PAYLOAD_H
#define CRASH_PAYLOAD_H

#include <ArduinoJson.h>

// Build the report the browser sends to the broker (spec §5): the decoded
// crash summary, diagnostics and config that has been through
// crash_redact_config(). Never the raw memory image, which can hold Wi-Fi and
// MQTT credentials that no redaction reaches.
//
// `reporterId` and `deleteKeyHash` stand in for any hardware identifier: a
// random id, and the SHA-256 of the delete key that erases these reports.
//
// False if anything did not fit: a truncated report would be filed as if it
// were whole, missing whatever was added last (the feature flags).
bool crash_payload_build(JsonDocument &doc, const char *reporterId,
                         const char *deleteKeyHash);

#endif // CRASH_PAYLOAD_H
