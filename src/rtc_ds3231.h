// DS3231MZ battery-backed RTC (I2C 0x68), on the ESP32-S3 LCD board.
//
// This is not a convenience feature. tsdb_energy_logger discards every sample
// until wall-clock passes TSDB_TIME_VALID_FLOOR, because writing a pre-NTP epoch
// would corrupt the time index. The board is powered by the EVSE, so it loses
// power whenever the EVSE does — and the case where that costs the most data is
// exactly the one where WiFi is also down and NTP never answers. The coin cell on
// BT1 (~225 mAh against the DS3231MZ's ~2.5 uA) is what closes that hole.
//
// Behaviour:
//   1. rtc_seed_system_time() at boot, before the logger starts, so time(NULL) is
//      already past the floor and the logger needs no special case.
//   2. rtc_store_system_time() whenever the time is set from a trusted source,
//      so the cell carries a fresh value across the next outage.
//
// INT/SQW is not connected on v1.2, so this polls and offers no alarms. On v1.3 it
// reaches IO17 and alarm/wake support becomes possible.
#ifndef __RTC_DS3231_H
#define __RTC_DS3231_H

#include <time.h>
#include <stdbool.h>

#ifdef ENABLE_DS3231

// Probe the device and report whether it answered. Safe to call before Wire has
// been opened elsewhere; it opens the bus itself if needed.
bool rtc_begin();

// True once a probe has found the device.
bool rtc_present();

// True if the oscillator has stopped since the time was last written — i.e. the
// backup cell is dead or was never fitted, and the held time is meaningless.
bool rtc_lost_power();

// Read the RTC and, if it holds a plausible time and the system clock does not,
// set the system clock from it. Returns true if the system clock was seeded.
bool rtc_seed_system_time();

// Write `t` to the RTC and clear the oscillator-stop flag. No-op if absent.
bool rtc_store_system_time(time_t t);

#else

static inline bool rtc_begin() { return false; }
static inline bool rtc_present() { return false; }
static inline bool rtc_lost_power() { return false; }
static inline bool rtc_seed_system_time() { return false; }
static inline bool rtc_store_system_time(time_t) { return false; }

#endif // ENABLE_DS3231

#endif // __RTC_DS3231_H
