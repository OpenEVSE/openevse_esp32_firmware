#pragma once

#include <Arduino.h>
#include "doctest.h"

// This suite has no persisted peers. Unexpected filesystem access is a failure.
/** Reject file I/O in a suite that models an empty persisted peer collection. */
class File {
public:
  /** Represent an unopened file. */
  explicit operator bool() const { return false; }
  /** Fail the test on an unexpected single-byte read. */
  int read() { FAIL("unexpected filesystem read"); return -1; }
  /** Fail the test on an unexpected buffered read. */
  size_t readBytes(char *, size_t) { FAIL("unexpected filesystem read"); return 0; }
  /** Fail the test on an unexpected single-byte write. */
  size_t write(uint8_t) { FAIL("unexpected filesystem write"); return 0; }
  /** Fail the test on an unexpected buffered write. */
  size_t write(const uint8_t *, size_t) { FAIL("unexpected filesystem write"); return 0; }
  /** Closing an unopened fake file has no effect. */
  void close() {}
};

static struct {
  /** Model the absence of persisted peer files. */
  bool exists(const char *) { return false; }
  /** Fail the test if peer handling attempts to open a file. */
  File open(const char *, const char *) { FAIL("unexpected filesystem open"); return {}; }
  /** Fail the test if peer handling attempts to remove a file. */
  bool remove(const char *) { FAIL("unexpected filesystem remove"); return false; }
  /** Fail the test if peer handling attempts to rename a file. */
  bool rename(const char *, const char *) { FAIL("unexpected filesystem rename"); return false; }
} LittleFS;
