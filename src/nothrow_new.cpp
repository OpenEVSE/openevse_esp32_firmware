// Make `new (std::nothrow)` return nullptr on allocation failure, as the
// standard requires, instead of aborting the firmware.
//
// These builds compile without C++ exceptions. libstdc++ implements the
// nothrow forms as `try { return ::operator new(n); } catch (...) { return
// nullptr; }`, and with exceptions disabled the throw inside the plain
// operator new ends in __cxa_allocate_exception, which ESP-IDF wraps to call
// abort(). So every "nothrow" allocation failure rebooted the unit. Seen in
// the field: Update.begin() allocates its 4 KB flash-write buffer with
// `new (std::nothrow) uint8_t[]`, and on a heap-tight online update that
// panicked with "abort() was called" in __wrap___cxa_allocate_exception
// instead of failing the update cleanly.
//
// The nothrow forms are replaceable global functions, so defining them here
// takes precedence over the library's copies. They allocate with malloc(),
// which is what the library's operator new uses, so operator delete (free())
// still releases them correctly.
//
// Not built for the native (EpoxyDuino) target: glibc's libstdc++ already
// behaves correctly there, and tests/integration/fail_nothrow_new.cpp
// interposes these same symbols to inject allocation failures.
#ifndef EPOXY_DUINO

#include <cstdlib>
#include <new>

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
  return malloc(size ? size : 1);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
  return malloc(size ? size : 1);
}

#endif // EPOXY_DUINO
