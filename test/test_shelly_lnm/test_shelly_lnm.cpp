#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>
#include <vector>
#include "shelly_lnm_parser.h"

static std::vector<uint8_t> packet(const std::string &json, uint8_t type = 1, uint16_t metaLen = 0)
{
  std::vector<uint8_t> p(12, 0);
  p[0] = 0x53; p[1] = 0x4C; p[3] = type;
  p[4] = json.size() & 0xff; p[5] = json.size() >> 8;
  p[6] = metaLen & 0xff; p[7] = metaLen >> 8;
  p.insert(p.end(), json.begin(), json.end());
  p.insert(p.end(), metaLen, 0);
  return p;
}

static ShellyLnmParseResult parse(const std::vector<uint8_t> &p, const char *pf, const char *vf,
                                  const char *dev, ShellyLnmReading &r)
{
  return shelly_lnm_parse(p.data(), p.size(), pf, vf, dev, r);
}

static const char *EM1 = R"({"device":"shellyproem50-aa","status":{"em1:0":{"id":0,"voltage":241.8,"current":3.559,"act_power":291.5}}})";
static const char *EM3 = R"({"device":"shellypro3em-bb","ts":1790183354.65,"status":{"em:0":{"id":0,"a_voltage":236.2,"a_act_power":0.0,"b_voltage":237.1,"b_act_power":20.9,"c_act_power":165.5,"n_current":null,"total_act_power":186.310,"user_calibrated_phase":[]}}})";

TEST_CASE("single phase EM") {
  ShellyLnmReading r;
  CHECK(parse(packet(EM1), "act_power", "voltage", "", r) == ShellyLnmParseResult::Ok);
  CHECK(r.hasPower); CHECK(r.hasVoltage);
  CHECK(r.power == doctest::Approx(291.5));
  CHECK(r.voltage == doctest::Approx(241.8));
}

TEST_CASE("3EM total power and phase voltage") {
  ShellyLnmReading r;
  CHECK(parse(packet(EM3), "total_act_power", "b_voltage", nullptr, r) == ShellyLnmParseResult::Ok);
  CHECK(r.power == doctest::Approx(186.31));
  CHECK(r.voltage == doctest::Approx(237.1));
  CHECK(parse(packet(EM3), "c_act_power", "", nullptr, r) == ShellyLnmParseResult::Ok);
  CHECK(r.power == doctest::Approx(165.5));
  CHECK_FALSE(r.hasVoltage);
}

TEST_CASE("device filter") {
  ShellyLnmReading r;
  CHECK(parse(packet(EM1), "act_power", "voltage", "shellyproem50-aa", r) == ShellyLnmParseResult::Ok);
  CHECK(parse(packet(EM1), "act_power", "voltage", "other", r) == ShellyLnmParseResult::WrongDevice);
}

TEST_CASE("missing fields") {
  ShellyLnmReading r;
  CHECK(parse(packet(EM1), "total_act_power", "a_voltage", "", r) == ShellyLnmParseResult::NoFields);
}

TEST_CASE("only power present still succeeds") {
  ShellyLnmReading r;
  CHECK(parse(packet(EM3), "total_act_power", "voltage", "", r) == ShellyLnmParseResult::Ok);
  CHECK(r.hasPower); CHECK_FALSE(r.hasVoltage);
}

TEST_CASE("header validation") {
  ShellyLnmReading r;
  std::vector<uint8_t> p = packet(EM1);
  CHECK(shelly_lnm_parse(p.data(), 5, "act_power", "voltage", "", r) == ShellyLnmParseResult::TooShort);

  auto bad = p; bad[0] = 'X';
  CHECK(parse(bad, "act_power", "voltage", "", r) == ShellyLnmParseResult::BadMagic);

  CHECK(parse(packet(EM1, 2), "act_power", "voltage", "", r) == ShellyLnmParseResult::NotStatus);

  CHECK(shelly_lnm_parse(p.data(), p.size() - 10, "act_power", "voltage", "", r) == ShellyLnmParseResult::Truncated);
  CHECK(parse(packet(EM1, 1, 4), "act_power", "voltage", "", r) == ShellyLnmParseResult::Ok);

  auto oversized = p; oversized[4] = 0xff; oversized[5] = 0xff;
  CHECK(parse(oversized, "act_power", "voltage", "", r) == ShellyLnmParseResult::Truncated);

  CHECK(parse(packet(""), "act_power", "voltage", "", r) == ShellyLnmParseResult::Truncated);
}

TEST_CASE("bad payloads") {
  ShellyLnmReading r;
  CHECK(parse(packet("{not json"), "act_power", "voltage", "", r) == ShellyLnmParseResult::BadJson);
  CHECK(parse(packet(R"({"device":"x"})"), "act_power", "voltage", "", r) == ShellyLnmParseResult::NoStatus);
  CHECK(parse(packet(R"({"status":{"em1:0":{"act_power":"abc"}}})"), "act_power", "", "", r) == ShellyLnmParseResult::NoFields);
}

TEST_CASE("non-finite values are rejected") {
  ShellyLnmReading r;
  const char *inf = R"({"device":"x","status":{"em1:0":{"voltage":1e999,"act_power":-1e999}}})";
  auto res = parse(packet(inf), "act_power", "voltage", "", r);
  CHECK(!(res == ShellyLnmParseResult::Ok && (r.hasPower || r.hasVoltage)));
}
