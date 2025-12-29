#pragma once

// Host-build shim to avoid INADDR_NONE macro colliding with EpoxyDuino's
// `extern const IPAddress INADDR_NONE;` declaration.
//
// Some libc implementations define INADDR_NONE as a macro in <arpa/inet.h>.
// By shadowing <arpa/inet.h> on the include path, we can include the system
// header then undefine the macro so later includes of EpoxyDuino's IPAddress.h
// don't break.

#include_next <arpa/inet.h>

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif
