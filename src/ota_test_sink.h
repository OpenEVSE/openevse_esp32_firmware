#pragma once

// Test-only record of what the OTA path hands to the flash updater.
//
// The native build's Update library is a stub that accepts anything, so the
// end-to-end suite cannot otherwise see what an upgrade delivered. Builds with
// -DOTA_TEST_SINK (native_openevse only) write the update the device received
// into the directory named by OPENEVSE_OTA_SINK_DIR:
//   ota_begin.json  the size passed to Update.begin() and the source
//   ota_image.bin   every byte passed to Update.write(), in order
//   ota_end.json    whether Update.end() reported success, and bytes written
// Without the flag, or without the environment variable, these are no-ops, so
// release firmware is unaffected.

#include <stddef.h>
#include <stdint.h>

#ifdef OTA_TEST_SINK
void ota_test_sink_begin(const char *source, size_t begin_size, size_t expected_size);
void ota_test_sink_write(const uint8_t *data, size_t len);
void ota_test_sink_end(bool ok, size_t written);
#else
#define ota_test_sink_begin(source, begin_size, expected_size) ((void)0)
#define ota_test_sink_write(data, len) ((void)0)
#define ota_test_sink_end(ok, written) ((void)0)
#endif
