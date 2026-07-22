#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <map>
#include <string>

#include "certificate_storage_transaction.h"

class FakeCertificateStorage
{
  public:
    bool has_space = true;
    bool remove_succeeds = true;
    bool write_starts = true;
    bool rename_succeeds = true;
    size_t reported_write = SIZE_MAX;
    std::map<std::string, std::string> files;

    bool exists(const char *path) const
    {
      return files.find(path) != files.end();
    }

    bool remove(const char *path)
    {
      if(!remove_succeeds) {
        return false;
      }
      files.erase(path);
      return true;
    }

    bool hasSpace(size_t) const
    {
      return has_space;
    }

    bool write(const char *path, const uint8_t *data, size_t size, size_t &written)
    {
      if(!write_starts) {
        written = 0;
        return false;
      }

      written = SIZE_MAX == reported_write ? size : reported_write;
      size_t stored = written < size ? written : size;
      files[path] = std::string(reinterpret_cast<const char *>(data), stored);
      return true;
    }

    bool rename(const char *from, const char *to)
    {
      if(!rename_succeeds || !exists(from) || exists(to)) {
        return false;
      }
      files[to] = files[from];
      files.erase(from);
      return true;
    }
};

static const char FINAL_PATH[] = "/certificates/1234.json";
static const char TEMP_PATH[] = "/certificates/1234.json.tmp";
static const uint8_t RECORD[] = {'{', '}', '\n'};

TEST_CASE("complete certificate record commits by rename")
{
  FakeCertificateStorage storage;

  CHECK(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK(storage.files[FINAL_PATH] == "{}\n");
  CHECK_FALSE(storage.exists(TEMP_PATH));
}

TEST_CASE("invalid transaction arguments fail without mutation")
{
  FakeCertificateStorage storage;

  CHECK_FALSE(certificate_storage_commit(storage, nullptr, RECORD, sizeof(RECORD)));
  CHECK_FALSE(certificate_storage_commit(storage, "", RECORD, sizeof(RECORD)));
  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, nullptr, sizeof(RECORD)));
  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, 0));
  CHECK(storage.files.empty());
}

TEST_CASE("existing final record is never replaced")
{
  FakeCertificateStorage storage;
  storage.files[FINAL_PATH] = "existing";

  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK(storage.files[FINAL_PATH] == "existing");
  CHECK_FALSE(storage.exists(TEMP_PATH));
}

TEST_CASE("stale temporary record is removed before a new attempt")
{
  FakeCertificateStorage storage;
  storage.files[TEMP_PATH] = "stale";

  CHECK(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK(storage.files[FINAL_PATH] == "{}\n");
  CHECK_FALSE(storage.exists(TEMP_PATH));
}

TEST_CASE("stale temporary cleanup failure fails closed")
{
  FakeCertificateStorage storage;
  storage.files[TEMP_PATH] = "stale";
  storage.remove_succeeds = false;

  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK_FALSE(storage.exists(FINAL_PATH));
  CHECK(storage.files[TEMP_PATH] == "stale");
}

TEST_CASE("insufficient space fails before writing")
{
  FakeCertificateStorage storage;
  storage.has_space = false;

  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK(storage.files.empty());
}

TEST_CASE("write startup failure leaves no certificate")
{
  FakeCertificateStorage storage;
  storage.write_starts = false;

  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK_FALSE(storage.exists(FINAL_PATH));
  CHECK_FALSE(storage.exists(TEMP_PATH));
}

TEST_CASE("zero partial and over-reported writes never commit")
{
  for(size_t reported : {size_t(0), sizeof(RECORD) - 1, sizeof(RECORD) + 1})
  {
    CAPTURE(reported);
    FakeCertificateStorage storage;
    storage.reported_write = reported;

    CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
    CHECK_FALSE(storage.exists(FINAL_PATH));
    CHECK_FALSE(storage.exists(TEMP_PATH));
  }
}

TEST_CASE("rename failure removes the complete temporary record")
{
  FakeCertificateStorage storage;
  storage.rename_succeeds = false;

  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK_FALSE(storage.exists(FINAL_PATH));
  CHECK_FALSE(storage.exists(TEMP_PATH));
}

TEST_CASE("cleanup failure never publishes a failed write")
{
  FakeCertificateStorage storage;
  storage.reported_write = sizeof(RECORD) - 1;
  storage.remove_succeeds = false;

  CHECK_FALSE(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK_FALSE(storage.exists(FINAL_PATH));
  CHECK(storage.exists(TEMP_PATH));
}
