#ifndef _DIVERT_SIM_SIM_CSV_WRITER_H
#define _DIVERT_SIM_SIM_CSV_WRITER_H

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace sim {

// Per-peer columns emitted by the runner. Stored centrally so the writer and
// the runner agree on the schema in one place. All current values are in
// watts to satisfy the unified "all currents in W" requirement.
namespace columns {

inline const std::vector<std::string> &peerColumns()
{
  static const std::vector<std::string> cols = {
      "online",
      "vehicle",
      "solar_w",
      "grid_ie_w",
      "live_pwr_w",
      "divert_smoothed_available_w",
      "shaper_max_w",
      "shaper_smoothed_live_w",
      "pilot_w",
      "charge_available_w",
      "state",
      "ev_max_charge_w",
      "actual_charge_w",
      "soc",
  };
  return cols;
}

} // namespace columns

class CsvWriter
{
public:
  // Open output file. Pass empty string for stdout.
  bool open(const std::string &path);

  // Write the header row using the provided peer ids and known column lists.
  void writeHeader(const std::vector<std::string> &peer_ids);

  // Begin a new row at the given timestamp (ISO8601 UTC).
  void beginRow(const std::string &iso_time);

  // Append a value to the current row.
  void addString(const std::string &v);
  void addBool(bool v);
  void addInt(long v);
  void addDouble(double v, int precision = 3);

  // Commit the current row.
  void endRow();

  void close();

private:
  std::ofstream _file;
  bool _to_stdout = true;
  bool _first_in_row = true;

  std::ostream &out() { return _to_stdout ? std::cout : _file; }
};

} // namespace sim

#endif // _DIVERT_SIM_SIM_CSV_WRITER_H
