#include "rtc_ds3231_regs.h"

// BCD helpers. from_bcd() rejects nibbles above 9 rather than returning a
// plausible-looking number: an unpowered or half-written device reads back 0xFF,
// and 0xFF quietly becoming 165 is worse than a failed read.
static bool from_bcd(uint8_t v, uint8_t &out)
{
  uint8_t hi = v >> 4;
  uint8_t lo = v & 0x0F;
  if(hi > 9 || lo > 9) {
    return false;
  }
  out = (uint8_t)(hi * 10 + lo);
  return true;
}

static uint8_t to_bcd(uint8_t v)
{
  return (uint8_t)(((v / 10) << 4) | (v % 10));
}

// Civil-date <-> days-since-epoch, rather than timegm()/mktime().
//
// timegm() is not declared by ESP-IDF's newlib, and mktime() would drag in the
// configured timezone -- the RTC holds UTC, and this module has to give the same
// answer on the host test build and on the device. These are Howard Hinnant's
// algorithms; they are exact for the whole range the device can represent and
// have no normalising behaviour to trip over.
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
  y -= (m <= 2);
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

static void civil_from_days(int32_t z, int32_t &y, uint32_t &m, uint32_t &d)
{
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t yr = (int32_t)yoe + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y = yr + (m <= 2);
}

static bool is_leap(int32_t y)
{
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static uint8_t days_in_month(int32_t y, uint8_t m)
{
  static const uint8_t len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if(m == 2 && is_leap(y)) {
    return 29;
  }
  return len[m - 1];
}

bool ds3231_time_plausible(time_t t)
{
  return t >= (time_t)RTC_TIME_VALID_FLOOR;
}

bool ds3231_decode(const uint8_t raw[DS3231_TIME_BYTES], time_t &out)
{
  uint8_t sec, min, hour, date, month, year;

  if(!from_bcd((uint8_t)(raw[0] & 0x7F), sec)) { return false; }
  if(!from_bcd((uint8_t)(raw[1] & 0x7F), min)) { return false; }

  // Hours. Bit 6 selects 12-hour mode, in which bit 5 is AM/PM rather than the
  // 20-hour tens bit. We only ever write 24-hour, but a device we did not set
  // (a swapped board, a bench part) can come back in 12-hour mode.
  if(raw[2] & 0x40)
  {
    bool pm = (raw[2] & 0x20) != 0;
    if(!from_bcd((uint8_t)(raw[2] & 0x1F), hour)) { return false; }
    if(hour < 1 || hour > 12) { return false; }
    if(hour == 12) { hour = 0; }
    if(pm) { hour = (uint8_t)(hour + 12); }
  }
  else
  {
    if(!from_bcd((uint8_t)(raw[2] & 0x3F), hour)) { return false; }
  }

  if(!from_bcd((uint8_t)(raw[4] & 0x3F), date)) { return false; }
  if(!from_bcd((uint8_t)(raw[5] & 0x1F), month)) { return false; }
  if(!from_bcd(raw[6], year)) { return false; }

  if(hour > 23 || min > 59 || sec > 59) { return false; }
  if(month < 1 || month > 12 || date < 1) { return false; }

  // Century bit (0x05 bit 7) rolls at 2100. Base is 2000 either way.
  int32_t full_year = 2000 + (int32_t)year + ((raw[5] & 0x80) ? 100 : 0);

  // Reject dates that do not exist, rather than letting them slide into the
  // following month. A 31st of February means the read is bad, not that the
  // caller wanted the 3rd of March.
  if(date > days_in_month(full_year, month)) { return false; }

  int64_t days = days_from_civil(full_year, month, date);
  int64_t t = days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;

  if(!ds3231_time_plausible((time_t)t)) { return false; }

  out = (time_t)t;
  return true;
}

bool ds3231_encode(time_t t, uint8_t raw[DS3231_TIME_BYTES])
{
  int64_t secs = (int64_t)t;
  // Floor-divide: the device cannot hold pre-2000 times anyway, but a negative
  // time_t must not turn into a positive day number on the way to being rejected.
  int32_t days = (int32_t)((secs >= 0 ? secs : secs - 86399) / 86400);
  int32_t rem = (int32_t)(secs - (int64_t)days * 86400);

  int32_t year;
  uint32_t month, date;
  civil_from_days(days, year, month, date);

  if(year < 2000 || year > 2199) {
    return false;
  }

  raw[0] = to_bcd((uint8_t)(rem % 60));
  raw[1] = to_bcd((uint8_t)((rem / 60) % 60));
  raw[2] = to_bcd((uint8_t)(rem / 3600));   // bit 6 clear = 24-hour mode
  // Day-of-week is 1..7 and the device only uses it for the alarm match, which
  // we do not use. Written anyway so a reader sees something coherent.
  // 1970-01-01 was a Thursday, so day 0 is weekday 4.
  raw[3] = (uint8_t)(((days % 7) + 11) % 7 + 1);
  raw[4] = to_bcd((uint8_t)date);
  raw[5] = to_bcd((uint8_t)month);
  if(year >= 2100) {
    raw[5] |= 0x80;
  }
  raw[6] = to_bcd((uint8_t)(year % 100));
  return true;
}
