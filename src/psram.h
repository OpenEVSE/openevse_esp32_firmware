#ifndef _OPENEVSE_PSRAM_H
#define _OPENEVSE_PSRAM_H

// External RAM, decided at runtime rather than from BOARD_HAS_PSRAM. That
// symbol comes from the PlatformIO board definition, and some definitions set
// it for modules that may have none: the stock TFT board's `denky32` sets it
// unconditionally on a WROOM-32E with no PSRAM fitted. Asking the heap is the
// only answer that matches the hardware.

// True if PSRAM is fitted and the SDK has added it to the heap.
bool psram_available();

// Route what can usefully live in PSRAM there. Call once, early in setup(),
// before the first TLS connection. A no-op on boards without PSRAM.
void psram_setup();

#endif // _OPENEVSE_PSRAM_H
