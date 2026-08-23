#ifndef _OPENEVSE_DIAGNOSTICS_H
#define _OPENEVSE_DIAGNOSTICS_H

#include <ArduinoJson.h>

// Runtime health instrumentation.
//
// `free_heap` alone cannot distinguish "plenty of memory" from "plenty of
// memory in fragments too small to allocate from". These helpers expose the
// metrics that tell the two apart, plus the reset reason, so an unexplained
// reboot leaves evidence behind instead of just a fresh uptime counter.

// Capture the reset reason. Must run before anything can trigger another
// reset, i.e. at the top of setup().
void diagnostics_begin();

// Sample the heap and task watermarks. Cheap and internally rate limited, so
// it is safe to call every pass of the main loop.
void diagnostics_loop();

// Add the collected metrics to a /status document.
void diagnostics_status(JsonDocument &doc);

// Close any websocket client whose outbound buffer has grown past
// DIAG_WS_SEND_LIMIT. A client that stops reading (half-open socket, stalled
// integration) otherwise accumulates every broadcast forever, and nothing in
// the send path applies backpressure. Returns the number closed.
int diagnostics_ws_reap();

// Largest-free-block probes, for attributing fragmentation to a code section.
// begin() returns the current largest block; end() records the worst drop seen
// for that slot. Reported as probe0_max..probe3_max in /status. Diagnostic
// only -- each call walks the free list.
#define DIAG_PROBE_SLOTS 4
uint32_t diagnostics_probe_begin();
void diagnostics_probe_end(int slot, uint32_t start);

#endif // _OPENEVSE_DIAGNOSTICS_H
