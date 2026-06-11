// Smoke test for the host-side (env:native) doctest harness.
//
// Its only job is to prove the native test toolchain compiles and runs, so the
// `pio test -e native` CI job has something green to report before any feature
// PR adds real suites alongside it. Keep it dependency-free.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("native doctest harness builds and runs") {
  CHECK(1 + 1 == 2);
}
