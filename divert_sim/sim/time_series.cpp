#include "time_series.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../parser.hpp"

using aria::csv::CsvParser;

namespace sim {

namespace {

// Parse one of the supported timestamp formats and return a unix epoch
// seconds value. Returns -1 on failure.
long parseTimestamp(const std::string &s)
{
  // Try unix epoch first.
  if (!s.empty()) {
    bool all_digits = true;
    for (char c : s) {
      if (c == '-') continue;
      if (c < '0' || c > '9') { all_digits = false; break; }
    }
    if (all_digits) {
      return std::atol(s.c_str());
    }
  }

  int y = 1970, M = 1, d = 1, h = 0, mi = 0, sec = 0;
  char ampm[3] = {0, 0, 0};
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%dZ", &y, &M, &d, &h, &mi, &sec) == 6 ||
      sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d+00:00", &y, &M, &d, &h, &mi, &sec) == 6 ||
      sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &M, &d, &h, &mi, &sec) == 6 ||
      sscanf(s.c_str(), "%d/%d/%d %d:%d:%d", &d, &M, &y, &h, &mi, &sec) == 6) {
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = sec;
    return timegm(&t);
  }

  // Time-only input (e.g. "12:15 AM") is anchored to 2020-01-01.
  int h12 = 0;
  if (sscanf(s.c_str(), "%d:%d %2s", &h12, &mi, ampm) == 3) {
    if ((ampm[0] == 'A' || ampm[0] == 'a' || ampm[0] == 'P' || ampm[0] == 'p') &&
        (ampm[1] == 'M' || ampm[1] == 'm') && h12 >= 1 && h12 <= 12 && mi >= 0 && mi <= 59) {
      h = h12 % 12;
      if (ampm[0] == 'P' || ampm[0] == 'p') {
        h += 12;
      }

      struct tm t = {};
      t.tm_year = 2020 - 1900;
      t.tm_mon = 0;
      t.tm_mday = 1;
      t.tm_hour = h;
      t.tm_min = mi;
      t.tm_sec = 0;
      return timegm(&t);
    }
  }
  return -1;
}

} // namespace

TimeSeries::TimeSeries(double fixed_value)
{
  _fixed = fixed_value;
  _has_fixed = true;
}

bool TimeSeries::loadFromJson(JsonVariantConst v,
                              const std::string &scenario_dir,
                              long start_epoch,
                              long duration_sec)
{
  _points.clear();
  _has_fixed = false;
  _fixed = 0.0;

  if (v.isNull()) {
    _has_fixed = true;
    return true;
  }

  if (v.is<double>() || v.is<long>() || v.is<int>() || v.is<float>()) {
    _fixed = v.as<double>();
    _has_fixed = true;
    return true;
  }

  if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst el : v.as<JsonArrayConst>()) {
      Point p;
      p.t_sec = el["time"] | 0;
      p.value = el["value"] | 0.0;
      _points.push_back(p);
    }
    std::sort(_points.begin(), _points.end(),
              [](const Point &a, const Point &b) { return a.t_sec < b.t_sec; });
    return true;
  }

  if (v.is<JsonObjectConst>()) {
    JsonObjectConst obj = v.as<JsonObjectConst>();
    if (obj.containsKey("value")) {
      _fixed = obj["value"].as<double>();
      _has_fixed = true;
      return true;
    }
    if (obj.containsKey("csv")) {
      std::string path = obj["csv"].as<const char *>();
      if (!path.empty() && path[0] != '/' && !scenario_dir.empty()) {
        path = scenario_dir + "/" + path;
      }
      int time_col = obj["time_column"] | 0;
      int value_col = obj["column"] | 1;
      bool kw = obj["kw"] | false;
      bool skip_header = obj["skip_header"] | true;
      // Allow explicit separator in JSON: "separator": ";".
      char separator = ',';
      if (obj.containsKey("separator")) {
        const char *sep_str = obj["separator"].as<const char *>();
        if (sep_str && sep_str[0]) separator = sep_str[0];
      }
      return loadCsv(path,
             time_col,
             value_col,
             kw,
             skip_header,
             separator,
             start_epoch,
             duration_sec);
    }
  }

  std::cerr << "TimeSeries: unrecognised JSON shape" << std::endl;
  return false;
}

bool TimeSeries::loadCsv(const std::string &path,
                         int time_col,
                         int value_col,
                         bool kw,
                         bool skip_header,
                         char separator,
                         long start_epoch,
                         long duration_sec)
{
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "TimeSeries: cannot open CSV " << path << std::endl;
    return false;
  }

  CsvParser parser(file);
  if (separator != ',') {
    parser.delimiter(separator);
  }

  long anchor = -1; // first row's epoch — used when no explicit start time
  int row = 0;
  for (auto &fields : parser) {
    row++;
    if (row == 1 && skip_header) continue;
    if ((int)fields.size() <= std::max(time_col, value_col)) continue;

    long t = parseTimestamp(fields[time_col]);
    if (t < 0) continue;
    Point p;
    if (start_epoch > 0) {
      p.t_sec = t - start_epoch;
      if (p.t_sec < 0) continue;
      if (duration_sec >= 0 && p.t_sec > duration_sec) continue;
    } else {
      if (anchor < 0) anchor = t;
      p.t_sec = t - anchor;
    }

    double v = 0;
    if (fields[value_col].empty() || sscanf(fields[value_col].c_str(), "%lf", &v) != 1) {
      continue;
    }
    if (kw) v *= 1000.0;
    p.value = v;
    _points.push_back(p);
  }

  std::sort(_points.begin(), _points.end(),
            [](const Point &a, const Point &b) { return a.t_sec < b.t_sec; });
  return true;
}

double TimeSeries::valueAt(long t_sec) const
{
  if (_points.empty()) {
    return _has_fixed ? _fixed : 0.0;
  }

  // Use the most recent sample at or before t_sec; default to first sample
  // for times before the first.
  double v = _points.front().value;
  for (const auto &p : _points) {
    if (p.t_sec <= t_sec) {
      v = p.value;
    } else {
      break;
    }
  }
  return v;
}

} // namespace sim
