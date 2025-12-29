#pragma once

// Shim to avoid INADDR_NONE macro collision between <arpa/inet.h> (mongoose) and EpoxyDuino's IPAddress.h.
// Used only for PlatformIO `native` builds.

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif

// Pull in the real EpoxyDuino IPAddress.h
#if defined(__GNUC__)
#include_next <IPAddress.h>
#else
#include <IPAddress.h>
#endif
