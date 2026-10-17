#pragma once

#include <Arduino.h>

static struct {
  /** Return a synthetic device identity for the isolated peer-state tests. */
  String getLongId() { return "dummy-device-id"; }
} ESPAL;
