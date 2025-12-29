#pragma once

// Compatibility for builds where ArduinoMongoose is compiled with
// MG_ENABLE_HTTP_STREAMING_MULTIPART disabled.
// The firmware references these event IDs unconditionally.

#ifndef MG_EV_HTTP_PART_BEGIN
#define MG_EV_HTTP_PART_BEGIN 122
#endif

#ifndef MG_EV_HTTP_PART_DATA
#define MG_EV_HTTP_PART_DATA 123
#endif

#ifndef MG_EV_HTTP_PART_END
#define MG_EV_HTTP_PART_END 124
#endif
