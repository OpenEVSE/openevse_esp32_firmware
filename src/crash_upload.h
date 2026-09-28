#ifndef CRASH_UPLOAD_H
#define CRASH_UPLOAD_H

#include <Arduino.h>

// One-click crash reporting (spec §3). The device POSTs a metadata document to
// the compiled-in broker, PUTs the 64 KB core dump straight out of its flash
// mapping, then POSTs a completion -- and erases the dump only after both the
// PUT and the completion answered 2xx (spec §7).
//
// Everything here is asynchronous. A synchronous network call on this path is
// the same shape as the MQTT DNS bug that tripped the 5 s task watchdog and
// became upstream #1252 (spec §6.4).

enum CrashUploadState {
  CrashUpload_Idle,
  CrashUpload_Metadata,
  CrashUpload_Raw,
  CrashUpload_Completing,
  CrashUpload_Done,
  CrashUpload_Failed,
  // Deferred to the next boot: the heap could not carry a TLS handshake now
  // (spec §6.1 tier 2). The click already happened; this is that one authorised
  // action finishing later, never a new one (spec §8).
  CrashUpload_Deferred,
};

void crash_upload_begin();
void crash_upload_loop();

// Start an upload, or explain why not. `message` is user-facing.
// Returns false when there is no dump, one is already running, or the feature
// is not built in.
bool crash_upload_request(String &message);

CrashUploadState crash_upload_state();
const char *crash_upload_state_name();
size_t crash_upload_sent();
size_t crash_upload_total();
// The user asked once and it is waiting for the next boot.
bool crash_upload_deferred_armed();
// Forget a deferred upload. The user changing their mind is the only caller.
void crash_upload_cancel_deferred();

#endif // CRASH_UPLOAD_H
