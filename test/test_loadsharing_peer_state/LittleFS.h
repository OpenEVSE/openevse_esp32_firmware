#pragma once

#include <Arduino.h>
#include "doctest.h"

// This suite has no persisted peers. Unexpected filesystem access is a failure.
class File {
public:
  explicit operator bool() const { return false; }
  int read() { FAIL("unexpected filesystem read"); return -1; }
  size_t readBytes(char *, size_t) { FAIL("unexpected filesystem read"); return 0; }
  size_t write(uint8_t) { FAIL("unexpected filesystem write"); return 0; }
  size_t write(const uint8_t *, size_t) { FAIL("unexpected filesystem write"); return 0; }
  void close() {}
};

static struct {
  bool exists(const char *) { return false; }
  File open(const char *, const char *) { FAIL("unexpected filesystem open"); return {}; }
  bool remove(const char *) { FAIL("unexpected filesystem remove"); return false; }
  bool rename(const char *, const char *) { FAIL("unexpected filesystem rename"); return false; }
} LittleFS;
