#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "config_storage.h"

// A heap with a fixed amount left, so these run against what ArduinoJson really
// does when memory runs out rather than a mock of it.
class BudgetAllocator : public ArduinoJson::Allocator {
  public:
    explicit BudgetAllocator(long budget) : _left(budget) {}

    void *allocate(size_t size) override {
      if((long)size > _left) return nullptr;
      void *p = malloc(size);
      _sizes[p] = size;
      _left -= size;
      return p;
    }
    void deallocate(void *p) override {
      _left += _sizes[p];
      _sizes.erase(p);
      free(p);
    }
    void *reallocate(void *p, size_t size) override {
      long delta = (long)size - (long)_sizes[p];
      if(delta > _left) return nullptr;
      void *q = realloc(p, size);
      _sizes.erase(p);
      _sizes[q] = size;
      _left -= delta;
      return q;
    }

  private:
    long _left;
    std::map<void *, size_t> _sizes;
};

static void fillConfig(JsonDocument &doc) {
  doc["ws"] = std::string("HomeNetwork");
  doc["ap"] = std::string("my-web-password");
  doc["wsk"] = std::string("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  doc["hn"] = std::string("garage-evse");
  doc["tz"] = std::string("Europe/Paris|CET-1CEST,M3.5.0,M10.5.0/3");
  doc["ms"] = std::string("192.168.1.10");
  doc["mt"] = std::string("openevse/garage");
  doc["mu"] = std::string("mqttuser");
  doc["slf"] = std::string("em:0.total_act_power");
  doc["fl"] = 12345678;
}

TEST_CASE("a complete config passes") {
  JsonDocument doc;
  fillConfig(doc);
  std::string json;
  serializeJson(doc, json);
  CHECK(config_record_complete(doc, json, 3072));
}

TEST_CASE("every amount of memory gives a complete record or a refusal") {
  JsonDocument whole;
  fillConfig(whole);
  std::string wholeJson;
  serializeJson(whole, wholeJson);

  int accepted = 0;
  int refused = 0;
  // Far enough to fit the whole config with 64-bit pointers, not just the ESP32's
  for(long budget = 0; budget <= 16384; budget += 4) {
    BudgetAllocator heap(budget);
    JsonDocument doc(&heap);
    fillConfig(doc);
    std::string json;
    serializeJson(doc, json);

    if(config_record_complete(doc, json, 3072)) {
      CHECK(json == wholeJson);
      accepted++;
    } else {
      refused++;
    }
  }
  CHECK(accepted > 0);
  CHECK(refused > 0);
}

TEST_CASE("a damaged config still parses, so parsing cannot be the check") {
  BudgetAllocator heap(700);
  JsonDocument doc(&heap);
  fillConfig(doc);
  REQUIRE(doc.overflowed());

  std::string json;
  serializeJson(doc, json);

  JsonDocument reloaded;
  CHECK(deserializeJson(reloaded, json) == DeserializationError::Ok);
  CHECK_FALSE(config_record_complete(doc, json, 3072));
}

TEST_CASE("output that stopped short is refused") {
  JsonDocument doc;
  fillConfig(doc);
  std::string json;
  serializeJson(doc, json);
  CHECK_FALSE(config_record_complete(doc, json.substr(0, json.size() - 1), 3072));
  CHECK_FALSE(config_record_complete(doc, std::string(), 3072));
}

TEST_CASE("the record must fit its region, header included") {
  JsonDocument doc;
  doc["k"] = std::string(100, 'x');
  std::string json;
  serializeJson(doc, json);
  size_t fits = json.size() + CONFIG_RECORD_HEADER_SIZE;

  CHECK(config_record_complete(doc, json, fits));
  CHECK_FALSE(config_record_complete(doc, json, fits - 1));
  CHECK_FALSE(config_record_complete(doc, json, 0));
}

static bool regionBlank(const std::vector<uint8_t> &region) {
  return config_region_blank([&](size_t i) { return region[i]; }, region.size());
}

TEST_CASE("only an erased region counts as blank") {
  CHECK(regionBlank(std::vector<uint8_t>(3072, 0xff)));
  CHECK(regionBlank(std::vector<uint8_t>(3072, 0x00)));

  // A header too damaged for load() to recognise, with the settings behind it
  std::vector<uint8_t> damagedHeader(3072, 0xff);
  damagedHeader[10] = '"';
  CHECK_FALSE(regionBlank(damagedHeader));
  CHECK_FALSE(config_record_present(damagedHeader[0], damagedHeader[1], (char)damagedHeader[2], 4096));

  // The last byte of the region still counts
  std::vector<uint8_t> lastByte(3072, 0x00);
  lastByte[3071] = 'x';
  CHECK_FALSE(regionBlank(lastByte));
}

TEST_CASE("a stored record is recognised the way ConfigJson::load() does") {
  CHECK(config_record_present(0x10, 0x00, '{', 4096));
  CHECK(config_record_present(0xff, 0x0f, '{', 4096));

  // Erased by ResetEEPROM(), and a never-written blob
  CHECK_FALSE(config_record_present(0xff, 0xff, (char)0xff, 4096));
  CHECK_FALSE(config_record_present(0x00, 0x00, 0x00, 4096));

  CHECK_FALSE(config_record_present(0x10, 0x00, 'x', 4096));
  CHECK_FALSE(config_record_present(0x01, 0x00, '{', 4096));
  CHECK_FALSE(config_record_present(0x00, 0x10, '{', 4096));
}
