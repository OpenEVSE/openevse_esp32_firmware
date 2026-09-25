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

// Last-panic forensics.
//
// The IDF writes a core dump to the `coredump` partition when it panics, and
// that survives the reboot. Reading it back over the network is the difference
// between diagnosing a crash and unbolting the unit from a live charger to get
// a serial cable on it.
//
// diagnostics_coredump_json() reports what the device can decode on its own:
// panic reason, faulting task, PC and a backtrace. Run the PCs through
// addr2line against the matching firmware.elf and that usually names the
// culprit without ever pulling the image.
void diagnostics_coredump_json(JsonDocument &doc);

// Walk the INTERNAL heap and append a plain-text layout report: totals, the
// largest live blocks, the largest free gaps and a log2 size histogram.
// Answers "what is sitting in internal DRAM" without heap tracing.
void diagnostics_heapmap(String &out);

// Hand back the raw dump for the cases the summary cannot answer, mapped
// straight out of flash so a 64KB image costs no heap. `data` stays valid for
// the life of the boot -- responses are sent asynchronously, so the pointer
// must outlive the request handler. Returns false when no dump is stored.
bool diagnostics_coredump_image(const uint8_t **data, size_t *len);

// Erase the stored dump, so the next panic is unambiguously the next panic.
bool diagnostics_coredump_erase();

#endif // _OPENEVSE_DIAGNOSTICS_H
