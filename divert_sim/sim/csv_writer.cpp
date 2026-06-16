#include "csv_writer.h"

#include <iomanip>
#include <sstream>

namespace sim {

bool CsvWriter::open(const std::string &path)
{
  if (path.empty()) {
    _to_stdout = true;
    return true;
  }
  _file.open(path);
  if (!_file.is_open()) {
    std::cerr << "CsvWriter: cannot open " << path << std::endl;
    return false;
  }
  _to_stdout = false;
  return true;
}

void CsvWriter::close()
{
  if (_file.is_open()) _file.close();
}

void CsvWriter::writeHeader(const std::vector<std::string> &peer_ids)
{
  beginRow("time");
  for (const auto &id : peer_ids) {
    for (const auto &col : columns::peerColumns()) {
      addString(id + "_" + col);
    }
  }
  endRow();
}

void CsvWriter::beginRow(const std::string &iso_time)
{
  out() << iso_time;
  _first_in_row = false; // timestamp is column 0; subsequent fields prefix ','
}

void CsvWriter::addString(const std::string &v)
{
  out() << ',' << v;
}

void CsvWriter::addBool(bool v)
{
  out() << ',' << (v ? '1' : '0');
}

void CsvWriter::addInt(long v)
{
  out() << ',' << v;
}

void CsvWriter::addDouble(double v, int precision)
{
  std::ostringstream s;
  s << std::fixed << std::setprecision(precision) << v;
  out() << ',' << s.str();
}

void CsvWriter::endRow()
{
  out() << '\n';
  _first_in_row = true;
}

} // namespace sim
