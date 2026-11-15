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
#define HTTP_UPDATE_ERROR_URL_NOT_ALLOWED             -5
#define HTTP_UPDATE_ERROR_TOO_MANY_REDIRECTS          -6
#define HTTP_UPDATE_ERROR_STALLED                     -7

#define HTTP_UPDATE_OK                                 0

#define HTTP_UPDATE_MAX_REDIRECTS                      5

// True if `url` is an HTTPS URL on a host permitted for firmware fetch
// (OpenEVSE's GitHub release hosts). Enforced on both the initial fetch and any
// redirect target, so a device can only pull firmware from a trusted origin.
bool http_update_url_allowed(const String &url);

// `redirects` counts the 30x hops already followed to reach `url`.
bool http_update_from_url(String url,
  std::function<void(size_t complete, size_t total)> progress,
  std::function<void(int)> success,
  std::function<void(int)> error,
  int redirects = 0);

// Starts redirects queued by the HTTP callbacks, and aborts an online update
// that has stopped receiving data. Call once per main loop, after
// Mongoose.poll(), so the previous TLS connection has been destroyed before
// the next one is allocated.
void http_update_loop();

// True while an online (URL) firmware update is downloading. Network services
// that are not needed for the update (MQTT, emoncms) stand down while it is
// set, so the download has the heap it needs. It clears when the update fails,
// including when http_update_loop() aborts a stalled download.
bool http_update_in_progress();

bool http_update_start(const String &source, size_t total);
bool http_update_write(uint8_t *data, size_t len);
bool http_update_end(bool evenIfRemaining = true);

#endif // _HTTP_UPDATE_H
