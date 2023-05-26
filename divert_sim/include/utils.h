#ifndef UTILS_H
#define UTILS_H

#include <time.h>

time_t parse_date(const char *dateStr);
int get_watt(const char *val, bool kw = false);

#endif // UTILS_H
