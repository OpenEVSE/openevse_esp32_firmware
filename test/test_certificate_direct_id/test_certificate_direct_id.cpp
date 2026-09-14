// Run: pio test -e native_certificate_test
// Real CertificateStore, OpenSSL validation and EpoxyFS persistence, with no
// firmware process, network listener or shared integration cleanup fixture.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <LittleFS.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>

#include "certificates.h"

// Make an uninitialized Certificate ID reproducible instead of depending on
// whichever bytes the allocator happened to leave behind.
void *operator new(std::size_t size)
{
  void *memory = std::malloc(size);
  if(!memory) throw std::bad_alloc();
  if(size == sizeof(CertificateStore::Certificate)) {
    std::memset(memory, 0xa5, size);
  }
  return memory;
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

namespace {
constexpr uint64_t CERTIFICATE_SERIAL = 0x12345678;

struct Fixture
{
  std::string directory;
  std::string certificate;
  std::string key;

  Fixture()
  {
    // Keys are generated in memory only, never read from a developer's files.
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    REQUIRE(context != nullptr);
    REQUIRE(EVP_PKEY_keygen_init(context) == 1);
    REQUIRE(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context, NID_X9_62_prime256v1) == 1);
    EVP_PKEY *private_key = nullptr;
    REQUIRE(EVP_PKEY_keygen(context, &private_key) == 1);
    EVP_PKEY_CTX_free(context);

    X509 *cert = X509_new();
    REQUIRE(cert != nullptr);
    REQUIRE(X509_set_version(cert, 2) == 1);
    REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(cert), CERTIFICATE_SERIAL) == 1);
    REQUIRE(X509_gmtime_adj(X509_getm_notBefore(cert), -60) != nullptr);
    REQUIRE(X509_gmtime_adj(X509_getm_notAfter(cert), 3600) != nullptr);
    REQUIRE(X509_set_pubkey(cert, private_key) == 1);
    X509_NAME *name = X509_get_subject_name(cert);
    REQUIRE(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char *>("example.invalid"), -1, -1, 0) == 1);
    REQUIRE(X509_set_issuer_name(cert, name) == 1);
    REQUIRE(X509_sign(cert, private_key, EVP_sha256()) > 0);

    BIO *output = BIO_new(BIO_s_mem());
    REQUIRE(PEM_write_bio_X509(output, cert) == 1);
    char *data = nullptr;
    long length = BIO_get_mem_data(output, &data);
    certificate.assign(data, length);
    BIO_free(output);
    output = BIO_new(BIO_s_mem());
    REQUIRE(PEM_write_bio_PrivateKey(output, private_key, nullptr, nullptr, 0, nullptr, nullptr) == 1);
    length = BIO_get_mem_data(output, &data);
    key.assign(data, length);
    BIO_free(output);
    X509_free(cert);
    EVP_PKEY_free(private_key);

    auto path = (std::filesystem::temp_directory_path() / "openevse-direct-id-XXXXXX").string();
    REQUIRE(mkdtemp(&path[0]) != nullptr);
    directory = path;
    REQUIRE(setenv("EPOXY_FS_ROOT", directory.c_str(), 1) == 0);
    REQUIRE(LittleFS.begin());
  }

  ~Fixture()
  {
    LittleFS.end();
    std::filesystem::remove_all(directory);
    unsetenv("EPOXY_FS_ROOT");
  }
};

void check_direct_add(bool client)
{
  Fixture fixture;
  CertificateStore store;
  REQUIRE(store.begin());
  uint64_t id = 0;
  bool added = client
    ? store.addCertificate("dummy", fixture.certificate.c_str(), fixture.key.c_str(), &id)
    : store.addCertificate("dummy", fixture.certificate.c_str(), &id);
  REQUIRE(added);
  REQUIRE(id == CERTIFICATE_SERIAL);
  CHECK(store.certificateCount() == 1);
  REQUIRE(store.getCertificate(id) != nullptr);
  CHECK(std::string(store.getCertificate(id)) == fixture.certificate);
  CHECK(std::string(store.getKey(id)) == (client ? fixture.key : ""));
  CHECK((std::string(store.getRootCa()).find(fixture.certificate) != std::string::npos) == !client);

  DynamicJsonDocument doc(4096);
  REQUIRE(store.serializeCertificate(doc, id));
  CHECK(doc["id"].as<std::string>() == "12345678");
  CHECK(doc["type"].as<std::string>() == (client ? "client" : "root"));

  File persisted = LittleFS.open("/certificates/12345678.json");
  REQUIRE(persisted);
  REQUIRE(deserializeJson(doc, persisted) == DeserializationError::Ok);
  persisted.close();
  CHECK(doc["id"].as<std::string>() == "12345678");
  CHECK(doc["certificate"].as<std::string>() == fixture.certificate);
  if(client) CHECK(doc["key"].as<std::string>() == fixture.key);

  uint64_t duplicate_id = 99;
  bool duplicate = client
    ? store.addCertificate("duplicate", fixture.certificate.c_str(), fixture.key.c_str(), &duplicate_id)
    : store.addCertificate("duplicate", fixture.certificate.c_str(), &duplicate_id);
  CHECK_FALSE(duplicate);
  CHECK(duplicate_id == 99);
  CHECK(store.certificateCount() == 1);
  CHECK(std::string(store.getCertificate(id)) == fixture.certificate);

  CertificateStore reloaded;
  REQUIRE(reloaded.begin());
  CHECK(reloaded.certificateCount() == 1);
  REQUIRE(reloaded.getCertificate(id) != nullptr);
  CHECK(std::string(reloaded.getCertificate(id)) == fixture.certificate);
  CHECK(std::string(reloaded.getKey(id)) == (client ? fixture.key : ""));
}
}

TEST_CASE("direct root add uses the validated serial for lookup and persistence")
{
  check_direct_add(false);
}

TEST_CASE("direct client add uses the validated serial for lookup and persistence")
{
  check_direct_add(true);
}
