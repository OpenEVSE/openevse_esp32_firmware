#ifndef _HTTP_UPDATE_H
#define _HTTP_UPDATE_H

// -------------------------------------------------------------------
// Support for updating the fitmware os the ESP8266
// -------------------------------------------------------------------

#include <Arduino.h>
#include <functional>

#define HTTP_UPDATE_ERROR_FAILED_TO_START_UPDATE      -1
#define HTTP_UPDATE_ERROR_WRITE_FAILED                -2
#define HTTP_UPDATE_ERROR_FAILED_TO_END_UPDATE        -3
#define HTTP_UPDATE_ERROR_INCOMPLETE_DOWNLOAD         -4
#define HTTP_UPDATE_ERROR_LOW_MEMORY                  -5

#define HTTP_UPDATE_OK                                 0

// Minimum free heap required to safely start an OTA update. Update.begin()
// itself needs a sizeable contiguous allocation; sharing the device with
// another open connection that's already claimed a chunk of heap (an open
// SSH session costs ~20-40KB, an already-open HTTPS session has its own TLS
// buffers) can leave too little for it to succeed, where it would otherwise
// fail deep inside Update.begin() with a generic, confusing error. Checked
// up front by every update entry point (multipart upload, URL fetch) so the
// caller gets a clear, immediate rejection instead.
// Calibrated against the live device: a solo HTTPS upload still has ~58KB
// free right before Update.begin() (the connection's own TLS buffers already
// cost ~30KB of whatever /status reported a moment earlier); an open SSH
// session (~20-30KB) on top of that is what was observed to push Update.begin()
// into failing. 50KB sits between the two with margin on both sides.
#define HTTP_UPDATE_MIN_FREE_HEAP (50 * 1024)
bool http_update_has_sufficient_heap();

bool http_update_from_url(String url,
  std::function<void(size_t complete, size_t total)> progress,
  std::function<void(int)> success,
  std::function<void(int)> error);

bool http_update_start(String source, size_t total);
bool http_update_write(uint8_t *data, size_t len);
bool http_update_end(bool evenIfRemaining = true);

#endif // _HTTP_UPDATE_H
