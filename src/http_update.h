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

#define HTTP_UPDATE_OK                                 0

bool http_update_from_url(String url,
  std::function<void(size_t complete, size_t total)> progress,
  std::function<void(int)> success,
  std::function<void(int)> error);

bool http_update_start(String source, size_t total);
bool http_update_write(uint8_t *data, size_t len);
bool http_update_end();

#endif // _HTTP_UPDATE_H
