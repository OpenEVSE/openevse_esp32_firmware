#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <map>
#include <string>
#include <cstdlib>
#include <cstring>
#include <new>

#include "certificate_storage_transaction.h"

// Count real heap requests only during the helper call. The rejection test can
// also exhaust the heap; assertions and storage bookkeeping run outside it.
static bool count_allocations = false;
static bool fail_allocations = false;
static size_t allocation_count = 0;

/** Instrument ordinary C++ allocations, with a controlled failure for heap tests. */
void *operator new(size_t size)
{
  if(count_allocations) {
    ++allocation_count;
  }
  if(fail_allocations) {
#if defined(__cpp_exceptions)
    throw std::bad_alloc();
#else
    std::abort();
#endif
  }
  void *memory = std::malloc(size == 0 ? 1 : size);
  if(nullptr == memory) {
    std::abort();
  }
  return memory;
}

/** Match the malloc-backed allocation hook, including sized C++14 deallocation. */
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, size_t) noexcept { std::free(memory); }

/**
 * Record storage calls without allocating, so measured heap requests belong to
 * certificate_storage_commit rather than the std::map-based storage fake.
 */
class PathRecordingStorage
{
  public:
    size_t calls = 0;
    char written_path[128] = {};
    char renamed_from[128] = {};
    char renamed_to[128] = {};

    bool exists(const char *) { ++calls; return false; }
    bool remove(const char *) { ++calls; return true; }
    bool hasSpace(size_t) { ++calls; return true; }
    bool write(const char *path, const uint8_t *, size_t size, size_t &written)
    {
      ++calls;
      std::strncpy(written_path, path, sizeof(written_path) - 1);
      written = size;
      return true;
    }
    bool rename(const char *from, const char *to)
    {
      ++calls;
      std::strncpy(renamed_from, from, sizeof(renamed_from) - 1);
      std::strncpy(renamed_to, to, sizeof(renamed_to) - 1);
      return true;
    }
};

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
      if(!rename_succeeds || !exists(from)) {
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

TEST_CASE("maximum certificate filename commits without path allocation")
{
  const char path[] = "/certificates/FFFFFFFFFFFFFFFF.json";
  PathRecordingStorage storage;
  allocation_count = 0;
  count_allocations = true;
  const bool committed = certificate_storage_commit(storage, path, RECORD, sizeof(RECORD));
  count_allocations = false;

  CHECK(committed);
  CHECK(allocation_count == 0);
  CHECK(std::strcmp(storage.written_path, "/certificates/FFFFFFFFFFFFFFFF.json.tmp") == 0);
  CHECK(std::strcmp(storage.renamed_from, storage.written_path) == 0);
  CHECK(std::strcmp(storage.renamed_to, path) == 0);
}

TEST_CASE("oversized final path fails before allocation or storage mutation")
{
  const char path[] = "/certificates/FFFFFFFFFFFFFFFFF.json";
  PathRecordingStorage storage;
  allocation_count = 0;
  count_allocations = true;
  const bool committed = certificate_storage_commit(storage, path, RECORD, sizeof(RECORD));
  count_allocations = false;

  CHECK_FALSE(committed);
  CHECK(allocation_count == 0);
  CHECK(storage.calls == 0);
}

TEST_CASE("oversized path rejection works with an exhausted heap")
{
  const char path[] = "/certificates/FFFFFFFFFFFFFFFFF.json";
  PathRecordingStorage storage;
  bool committed = true;
  bool escaped = false;
  allocation_count = 0;
  count_allocations = true;
  fail_allocations = true;
#if defined(__cpp_exceptions)
  try {
#endif
    committed = certificate_storage_commit(storage, path, RECORD, sizeof(RECORD));
#if defined(__cpp_exceptions)
  } catch(const std::bad_alloc &) {
    escaped = true;
  }
#endif
  fail_allocations = false;
  count_allocations = false;

  CHECK_FALSE(escaped);
  CHECK_FALSE(committed);
  CHECK(allocation_count == 0);
  CHECK(storage.calls == 0);
}

TEST_CASE("maximum certificate filename commits with an exhausted heap")
{
  const char path[] = "/certificates/FFFFFFFFFFFFFFFF.json";
  PathRecordingStorage storage;
  bool committed = false;
  bool escaped = false;
  allocation_count = 0;
  count_allocations = true;
  fail_allocations = true;
#if defined(__cpp_exceptions)
  try {
#endif
    committed = certificate_storage_commit(storage, path, RECORD, sizeof(RECORD));
#if defined(__cpp_exceptions)
  } catch(const std::bad_alloc &) {
    escaped = true;
  }
#endif
  fail_allocations = false;
  count_allocations = false;

  CHECK_FALSE(escaped);
  CHECK(committed);
  CHECK(allocation_count == 0);
  CHECK(std::strcmp(storage.written_path, "/certificates/FFFFFFFFFFFFFFFF.json.tmp") == 0);
  CHECK(std::strcmp(storage.renamed_to, path) == 0);
}


TEST_CASE("configured directory bound includes the full ID suffix and terminator")
{
  // Match the production caller's capacity calculation for an overridden base.
  const char path[] = "/custom/certificate/archive/FFFFFFFFFFFFFFFF.json";
  PathRecordingStorage storage;
  allocation_count = 0;
  count_allocations = true;
  fail_allocations = true;
  const bool committed = certificate_storage_commit<sizeof(path) - 1>(storage, path, RECORD, sizeof(RECORD));
  fail_allocations = false;
  count_allocations = false;

  CHECK(committed);
  CHECK(allocation_count == 0);
  CHECK(std::strcmp(storage.written_path, "/custom/certificate/archive/FFFFFFFFFFFFFFFF.json.tmp") == 0);
  CHECK(std::strcmp(storage.renamed_from, storage.written_path) == 0);
  CHECK(std::strcmp(storage.renamed_to, path) == 0);

  PathRecordingStorage too_small;
  CHECK_FALSE(certificate_storage_commit<sizeof(path) - 2>(too_small, path, RECORD, sizeof(RECORD)));
  CHECK(too_small.calls == 0);
}

TEST_CASE("empty invalid and shortest paths respect the supplied bound")
{
  PathRecordingStorage storage;
  CHECK_FALSE(certificate_storage_commit<1>(storage, nullptr, RECORD, sizeof(RECORD)));
  CHECK_FALSE(certificate_storage_commit<1>(storage, "", RECORD, sizeof(RECORD)));
  CHECK_FALSE(certificate_storage_commit<1>(storage, "a", nullptr, sizeof(RECORD)));
  CHECK_FALSE(certificate_storage_commit<1>(storage, "a", RECORD, 0));
  CHECK_FALSE(certificate_storage_commit<0>(storage, "a", RECORD, sizeof(RECORD)));
  CHECK(storage.calls == 0);
  CHECK(certificate_storage_commit<1>(storage, "a", RECORD, sizeof(RECORD)));
  CHECK(std::strcmp(storage.written_path, "a.tmp") == 0);
}

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

TEST_CASE("complete certificate record atomically replaces an existing record")
{
  FakeCertificateStorage storage;
  storage.files[FINAL_PATH] = "existing";

  CHECK(certificate_storage_commit(storage, FINAL_PATH, RECORD, sizeof(RECORD)));
  CHECK(storage.files[FINAL_PATH] == "{}\n");
  CHECK_FALSE(storage.exists(TEMP_PATH));
}

TEST_CASE("failed replacement preserves the existing record")
{
  FakeCertificateStorage storage;
  storage.files[FINAL_PATH] = "existing";
  storage.reported_write = sizeof(RECORD) - 1;

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
