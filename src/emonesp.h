#ifndef _EMONESP_H
#define _EMONESP_H

// -------------------------------------------------------------------
// General support code used by all modules
// -------------------------------------------------------------------

#include "debug.h"
#include "profile.h"

#ifndef RAPI_PORT
#ifdef ESP32
#define RAPI_PORT Serial1
#elif defined(ESP8266)
#define RAPI_PORT Serial
#else
#error Platform not supported
#endif
#endif

#endif // _EMONESP_H
