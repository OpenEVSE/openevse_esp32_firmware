#ifndef _EMONESP_HTTP_H
#define _EMONESP_HTTP_H

// -------------------------------------------------------------------
// HTTP(S) support functions
// -------------------------------------------------------------------

#include <Arduino.h>

// -------------------------------------------------------------------
// HTTPS SECURE GET Request
// url: N/A
// -------------------------------------------------------------------
extern String get_https(const char* fingerprint, const char* host, String url, int httpsPort);

// -------------------------------------------------------------------
// HTTP GET Request
// url: N/A
// -------------------------------------------------------------------
extern String get_http(const char* host, String url);

#endif // _EMONESP_HTTP_H
