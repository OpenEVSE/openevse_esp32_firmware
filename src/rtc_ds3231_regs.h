// DS3231 register codec — pure logic, no Arduino or I2C dependency.
//
// Split out from the device layer so the BCD conversion and the validity rules
// can be exercised on the build host; the board this ships on cannot be bench
// tested without an RTC and a coin cell, and a wrong century or BCD nibble is
// exactly the kind of bug that only shows up as a corrupted time index months
// later.
#ifndef __RTC_DS3231_REGS_H
#define __RTC_DS3231_REGS_H

#include <stdint.h>
#include <time.h>

// Timekeeping registers 0x00..0x06, in device order.
#define DS3231_REG_TIME     0x00
#define DS3231_REG_STATUS   0x0F
#define DS3231_STATUS_OSF   0x80   // oscillator stopped since it was last cleared
#define DS3231_TIME_BYTES   7

// A time read back from the RTC must be at least this to be believed. Same value
// and same reasoning as TSDB_TIME_VALID_FLOOR (2023-11-14): seeding the clock
// with a pre-NTP epoch would corrupt the tsdb time index, so a battery that has
// gone flat must present as "no time" rather than as 1970 or 2000-01-01.
#define RTC_TIME_VALID_FLOOR 1700000000UL

// True if `t` is late enough to be a real wall clock rather than a reset default.
bool ds3231_time_plausible(time_t t);

// Decode 7 timekeeping registers into a UTC time_t.
// Returns false on malformed BCD, an out-of-range field, or an implausible result.
bool ds3231_decode(const uint8_t raw[DS3231_TIME_BYTES], time_t &out);

// Encode a UTC time_t into 7 timekeeping registers.
// Returns false if `t` is not representable (before 2000, or past 2099).
bool ds3231_encode(time_t t, uint8_t raw[DS3231_TIME_BYTES]);

#endif // __RTC_DS3231_REGS_H
