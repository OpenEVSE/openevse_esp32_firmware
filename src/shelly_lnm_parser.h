#ifndef _SHELLY_LNM_PARSER_H
#define _SHELLY_LNM_PARSER_H

// Pure parser for Shelly Local Network Messaging (LNM) datagrams.
// No network, divert or EVSE dependencies so it can be unit tested on the host.

#include <stddef.h>
#include <stdint.h>

#define SHELLY_LNM_HEADER_LEN 12

enum class ShellyLnmParseResult {
  Ok,           // at least one of power / voltage was extracted
  TooShort,     // shorter than the binary header
  BadMagic,     // not a Shelly LNM datagram
  NotStatus,    // valid datagram but not a status payload
  Truncated,    // payload/meta lengths exceed the datagram, or empty payload
  BadJson,      // payload is not valid JSON
  NoStatus,     // JSON has no "status" object
  WrongDevice,  // "device" does not match the configured device filter
  NoFields      // none of the requested fields were found
};

struct ShellyLnmReading {
  bool hasPower = false;
  bool hasVoltage = false;
  double power = 0;
  double voltage = 0;
};

// powerField / voltageField: JSON key of the component status to read
// (voltageField may be empty). deviceFilter: only accept datagrams whose
// "device" equals this value; null or empty accepts any device.
ShellyLnmParseResult shelly_lnm_parse(const uint8_t *data, size_t len,
                                      const char *powerField,
                                      const char *voltageField,
                                      const char *deviceFilter,
                                      ShellyLnmReading &out);

#endif // _SHELLY_LNM_PARSER_H
