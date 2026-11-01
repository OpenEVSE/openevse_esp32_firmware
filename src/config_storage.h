#pragma once

// Checks on the EEPROM config record (#1387). Header-only with no Arduino or
// EEPROM dependency, so it is native tested against ArduinoJson itself.
//
// A record is a little-endian 16-bit length followed by that much JSON.

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_RECORD_HEADER_SIZE 2

// True when a record's first bytes describe a stored config. Mirrors the test in
// ConfigJson::load(), so the two agree on whether there is anything to read.
inline bool config_record_present(uint8_t lengthLow, uint8_t lengthHigh, char start, size_t storageSize)
{
  size_t length = lengthLow | ((size_t)lengthHigh << 8);
  return 2 <= length && length < storageSize && '{' == start;
}

// True when `json` is a complete rendering of `doc` that fits a record of
// `recordSize` bytes.
//
// Saving must check this. Out of memory, ArduinoJson leaves a setting null or
// drops it and still produces valid JSON, and a String that cannot grow stops
// short. Either way a save would replace the good stored config with a damaged
// one, and the loss would only show at the next reboot.
template <typename TString>
bool config_record_complete(const JsonDocument &doc, const TString &json, size_t recordSize)
{
  if(doc.overflowed() || recordSize < CONFIG_RECORD_HEADER_SIZE) {
    return false;
  }
  size_t expected = measureJson(doc);
  return expected >= 2 && json.length() == expected &&
         expected <= recordSize - CONFIG_RECORD_HEADER_SIZE;
}
