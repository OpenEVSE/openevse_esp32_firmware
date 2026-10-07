#ifdef ENABLE_DS3231

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>

#include "rtc_ds3231.h"
#include "rtc_ds3231_regs.h"
#include "debug.h"

#ifdef ENABLE_TSDB
#include "tsdb_energy_logger.h"
// The whole point of seeding the clock is to get the logger past its floor. If the
// two ever disagree, seeding could "succeed" and still leave samples discarded.
static_assert(RTC_TIME_VALID_FLOOR == TSDB_TIME_VALID_FLOOR,
              "RTC and tsdb validity floors must agree");
#endif

#ifndef DS3231_I2C_ADDRESS
#define DS3231_I2C_ADDRESS 0x68
#endif

#ifndef I2C_SDA
#define I2C_SDA -1
#endif
#ifndef I2C_SCL
#define I2C_SCL -1
#endif

static bool _present = false;

static bool read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(reg);
  if(Wire.endTransmission(false) != 0) {
    return false;
  }
  if(Wire.requestFrom((uint8_t)DS3231_I2C_ADDRESS, len) != len) {
    return false;
  }
  for(uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

static bool write_regs(uint8_t reg, const uint8_t *buf, uint8_t len)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(reg);
  Wire.write(buf, len);
  return Wire.endTransmission() == 0;
}

bool rtc_begin()
{
  // Shared bus with the MCP9808; EvseMonitor::setup() also opens it. Calling
  // begin() twice on the same pins is harmless, and this runs first at boot.
  Wire.begin(I2C_SDA, I2C_SCL);

  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  _present = (Wire.endTransmission() == 0);

  if(!_present) {
    DBUGLN("[rtc] DS3231 not found");
    return false;
  }

  DBUGF("[rtc] DS3231 found%s", rtc_lost_power() ? " (oscillator stopped - time not held)" : "");
  return true;
}

bool rtc_present()
{
  return _present;
}

bool rtc_lost_power()
{
  uint8_t status = 0;
  if(!_present || !read_regs(DS3231_REG_STATUS, &status, 1)) {
    return true;
  }
  return (status & DS3231_STATUS_OSF) != 0;
}

bool rtc_seed_system_time()
{
  if(!_present) {
    return false;
  }

  if(rtc_lost_power()) {
    DBUGLN("[rtc] oscillator stop flag set, not seeding from a clock that lost power");
    return false;
  }

  uint8_t raw[DS3231_TIME_BYTES];
  if(!read_regs(DS3231_REG_TIME, raw, DS3231_TIME_BYTES)) {
    DBUGLN("[rtc] read failed");
    return false;
  }

  time_t rtc_time = 0;
  if(!ds3231_decode(raw, rtc_time)) {
    DBUGLN("[rtc] held time is not plausible, ignoring it");
    return false;
  }

  // Only seed if the system clock is not already better. On a warm restart the
  // clock survives, and NTP is more accurate than a part with +/-2 ppm drift.
  if(ds3231_time_plausible(time(NULL))) {
    DBUGLN("[rtc] system clock already valid, leaving it alone");
    return false;
  }

  struct timeval tv = { .tv_sec = rtc_time, .tv_usec = 0 };
  timezone tz_utc = {0, 0};
  settimeofday(&tv, &tz_utc);

  DBUGF("[rtc] system clock seeded from RTC: %lu", (unsigned long)rtc_time);
  return true;
}

bool rtc_store_system_time(time_t t)
{
  if(!_present) {
    return false;
  }

  if(!ds3231_time_plausible(t)) {
    DBUGLN("[rtc] refusing to store an implausible time");
    return false;
  }

  uint8_t raw[DS3231_TIME_BYTES];
  if(!ds3231_encode(t, raw)) {
    DBUGLN("[rtc] time not representable by the device");
    return false;
  }

  if(!write_regs(DS3231_REG_TIME, raw, DS3231_TIME_BYTES)) {
    DBUGLN("[rtc] write failed");
    return false;
  }

  // Clear the oscillator-stop flag, otherwise the value we just wrote would be
  // refused on the next boot. Read-modify-write: the other status bits (busy,
  // EN32kHz, alarm flags) are not ours to clobber.
  uint8_t status = 0;
  if(read_regs(DS3231_REG_STATUS, &status, 1) && (status & DS3231_STATUS_OSF))
  {
    status &= (uint8_t)~DS3231_STATUS_OSF;
    write_regs(DS3231_REG_STATUS, &status, 1);
  }

  DBUGF("[rtc] stored %lu", (unsigned long)t);
  return true;
}

#endif // ENABLE_DS3231
