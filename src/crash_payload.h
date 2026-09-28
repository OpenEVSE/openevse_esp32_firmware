#ifndef CRASH_PAYLOAD_H
#define CRASH_PAYLOAD_H

#include <ArduinoJson.h>

// Build the metadata document the broker indexes (spec §5). Everything here is
// small and non-secret; the 64 KB image goes separately, and the config in it
// has been through crash_redact_config().
//
// `rawBytes` is how many bytes the device intends to PUT afterwards, or 0 for
// a summary-only report. The broker uses it to bound the upload before it
// happens, so it has to be the real figure.
void crash_payload_build(JsonDocument &doc, size_t rawBytes);

#endif // CRASH_PAYLOAD_H
