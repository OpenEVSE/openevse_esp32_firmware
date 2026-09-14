#pragma once

#include <Arduino.h>

static struct {
  String getLongId() { return "dummy-device-id"; }
} ESPAL;
