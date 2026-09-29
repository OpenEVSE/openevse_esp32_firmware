#ifndef _DIVERT_SIM_SIM_TIME_SERIES_H
#define _DIVERT_SIM_SIM_TIME_SERIES_H

#include <string>
#include <vector>

#include <ArduinoJson.h>

namespace sim {

// A piecewise-constant series of (time_seconds, value) points, indexed
// relative to the scenario's t=0. `valueAt(t)` returns the value of the
// most recent sample at or before `t` (or the first sample for t below the
// first sample; defaults to 0 if empty).
class TimeSeries
{
public:
  struct Point
  {
    long t_sec;
    double value;
  };

  // Default fixed value when no samples are loaded.
  TimeSeries() = default;
  explicit TimeSeries(double fixed_value);

  // Load from a JSON variant. Accepted shapes:
  //   42                                    -> fixed value 42
  //   { "value": 42 }                       -> fixed value 42
  //   [ { "time": 0, "value": 0 }, ... ]    -> inline series
  //   { "csv": "data/x.csv", "column": 1, "time_column": 0,
  //     "kw": false, "skip_header": true }  -> CSV file
  //
  // `scenario_dir` is used to resolve relative CSV paths.
  // Returns false on parse error.
  bool loadFromJson(JsonVariantConst v,
                    const std::string &scenario_dir,
                    long start_epoch = 0,
                    long duration_sec = -1);

  // Step-interpolated lookup.
  double valueAt(long t_sec) const;

  bool empty() const { return _points.empty() && !_has_fixed; }

private:
  std::vector<Point> _points;
  double _fixed = 0.0;
  bool _has_fixed = false;

  bool loadCsv(const std::string &path,
               int time_col,
               int value_col,
               bool kw,
               bool skip_header,
               char separator = ',',
               long start_epoch = 0,
               long duration_sec = -1);
};

} // namespace sim

#endif // _DIVERT_SIM_SIM_TIME_SERIES_H
