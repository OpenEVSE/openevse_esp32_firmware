#ifndef _EMONESP_HTTP_H
#define _EMONESP_HTTP_H

#include <Arduino.h>

extern String get_https(const char* fingerprint, const char* host, String url, int httpsPort);
extern String get_http(const char* host, String url);

#endif // _EMONESP_HTTP_H
