#include "utils.h"
#include <string>
#include <stdexcept>
#include <cmath>

time_t parse_date(const char *dateStr)
{
  int y = 2020, M = 1, d = 1, h = 0, m = 0, s = 0;
  char ampm[5];
  if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%dZ", &y, &M, &d, &h, &m, &s)) {
    if(6 != sscanf(dateStr, "%d-%d-%dT%d:%d:%d+00:00", &y, &M, &d, &h, &m, &s)) {
      if(6 != sscanf(dateStr, "%d-%d-%d %d:%d:%d", &y, &M, &d, &h, &m, &s)) {
        if(3 != sscanf(dateStr, "%d:%d %s", &h, &m, ampm)) {
          if(1 == sscanf(dateStr, "%d", &s)) {
            return s;
          }
        } else {
          y = 2020; M = 1; d = 1; s = 0;
          if(12 == h) {
            h -= 12;
          }
          if('P' == ampm[0]) {
            h += 12;
          }
        }
      }
    }
  }

  tm time = {0};
  time.tm_year = y - 1900; // Year since 1900
  time.tm_mon = M - 1;     // 0-11
  time.tm_mday = d;        // 1-31
  time.tm_hour = h;        // 0-23
  time.tm_min = m;         // 0-59
  time.tm_sec = s;         // 0-61 (0-60 in C++11)

  return timegm(&time);
}

int get_watt(const char *val, bool kw)
{
  float number = 0.0;
  if(1 != sscanf(val, "%f", &number)) {
    throw std::invalid_argument("Not a number");
  }

  if(kw) {
    number *= 1000;
  }

  return (int)round(number);
}
