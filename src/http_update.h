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

#define HTTP_UPDATE_OK                                 0

// True if `url` is an HTTPS URL on a host permitted for firmware fetch
// (OpenEVSE's GitHub release hosts). Enforced on both the initial fetch and any
// redirect target, so a device can only pull firmware from a trusted origin.
bool http_update_url_allowed(const String &url);

bool http_update_from_url(String url,
  std::function<void(size_t complete, size_t total)> progress,
  std::function<void(int)> success,
  std::function<void(int)> error);

bool http_update_start(String source, size_t total);
bool http_update_write(uint8_t *data, size_t len);
bool http_update_end(bool evenIfRemaining = true);

#endif // _HTTP_UPDATE_H
