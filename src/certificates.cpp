#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_CETRIFICATES)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <LittleFS.h>
#include <memory>
#include <new>

#include "emonesp.h"
#include "certificate_id.h"
#include "certificate_storage_transaction.h"
#include "certificates.h"
#include "fs_util.h"
#include "root_ca.h"
#include "certificate_validator.h"

bool certificate_id_from_string(const char *str, uint64_t &id)
{
  if(NULL == str || '\0' == *str) {
    return false;
  }

  // A 64-bit id is at most 16 hex digits; longer cannot be valid.
  if(strlen(str) > 16) {
    return false;
  }

  for(const char *c = str; '\0' != *c; c++) {
    if(!isxdigit((unsigned char)*c)) {
      return false;
    }
  }

  id = strtoull(str, nullptr, 16);
  return true;
}

bool CertificateStore::Certificate::deserialize(JsonObject &obj)
{
  _name = obj["name"].as<std::string>();

  std::string cert = obj["certificate"].as<std::string>();

  // Get the certificate validator instance
  std::unique_ptr<CertificateValidator> validator(createCertificateValidator());
  if(!validator) {
    DBUGLN("Failed to create certificate validator");
    return false;
  }

  // Validate the certificate
  CertificateValidator::ValidationResult result = validator->validateCertificate(cert);
  if(!result.valid) {
    DBUGF("Certificate validation failed: %s", result.error.c_str());
    DBUGVAR(cert.c_str());
    return false;
  }

#if defined(ENABLE_DEBUG_CETRIFICATES)
  DBUGF("issuer: %s", result.issuer.c_str());
  DBUGF("subject: %s", result.subject.c_str());
#endif

  _cert = cert;
  if(obj.containsKey("id")) {
    std::string id_str = obj["id"].as<std::string>();
    if(!certificate_id_from_string(id_str.c_str(), _id)) {
      DBUGF("Invalid certificate id '%s'", id_str.c_str());
      return false;
    }
  } else {
    _id = result.serial;
  }

  if(obj.containsKey("key"))
  {
    std::string key = obj["key"].as<std::string>();

    if(!validator->validatePrivateKey(key)) {
      DBUGVAR(key.c_str());
      return false;
    }

    _type = Type::Client;
    _key = key;
  } else {
    _type = Type::Root;
    _key = "";
  }

  DBUGVAR(_id, HEX);
  DBUGVAR(_name.c_str());
  DBUGVAR(_cert.c_str());
  DBUGVAR(_key.c_str());

  return true;
}

bool CertificateStore::Certificate::serialize(JsonObject &doc, uint32_t flags)
{
  doc["id"] = certificate_id_hex(_id);
  doc["type"] = _type.toString();
  doc["name"] = _name;
  doc["certificate"] = _cert;
  if(_type == Type::Client) {
    doc["key"] = flags & Flags::REDACT_PRIVATE_KEY ? "__REDACTED__" : _key.c_str();
  }

  return true;
}

CertificateStore::CertificateStore() :
  _certs(),
  _root_ca(root_ca)
{
}

/** Release owned certificate objects and any dynamically allocated root bundle. */
CertificateStore::~CertificateStore()
{
  if(begin())
  {
    for(std::vector<Certificate *>::iterator it = _certs.begin(); it != _certs.end(); _certs.erase(it))
    {
      Certificate *cert = *it;
      delete cert;
    }
  }

  if(_root_ca != root_ca) {
    delete[] _root_ca;
  }
}

bool CertificateStore::begin()
{
  if(loadCertificates()) {
    return true;
  }

  return false;
}

const char *CertificateStore::getRootCa()
{
  return _root_ca;
}

/**
 * Validate and store a client certificate using its serial as the store ID.
 * @param name Display name stored with the certificate.
 * @param certificate PEM certificate passed to the shared validation path.
 * @param key PEM private key passed to the shared validation path.
 * @param id Optional output for the validated certificate serial.
 * @return True when the certificate is added and saved; false on rejection or failure.
 */
bool CertificateStore::addCertificate(const char *name, const char *certificate, const char *key, uint64_t *id)
{
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(3));
  doc["name"] = name;
  doc["certificate"] = certificate;
  doc["key"] = key;
  return addCertificate(doc, id);
}

/**
 * Validate and store a root certificate using its serial as the store ID.
 * @param name Display name stored with the certificate.
 * @param certificate PEM certificate passed to the shared validation path.
 * @param id Optional output for the validated certificate serial.
 * @return True when the certificate is added and saved; false on rejection or failure.
 */
bool CertificateStore::addCertificate(const char *name, const char *certificate, uint64_t *id)
{
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(2));
  doc["name"] = name;
  doc["certificate"] = certificate;
  return addCertificate(doc, id);
}

bool CertificateStore::addCertificate(DynamicJsonDocument &doc, uint64_t *id, bool save)
{
  Certificate *cert = new Certificate();
  if(cert)
  {
    if(cert->deserialize(doc))
    {
      if(addCertificate(cert, id, save)) {
        return true;
      }
    }
    delete cert;
  }

  return false;
}

/**
 * Add a validated certificate, publishing prepared trust only after storage succeeds.
 * @param cert Certificate whose ownership transfers to the store on success.
 * @param id Optional output written only after the addition succeeds.
 * @param save False when loading an existing record; true to persist an upload.
 * @return False for duplicates or reported preparation/storage failures; the caller
 * retains ownership of cert on failure.
 */
bool CertificateStore::addCertificate(Certificate *cert, uint64_t *id, bool save)
{
  uint64_t certId = cert->getId();
  if(findCertificate(certId, cert)) {
    DBUGF("Certificate already exists");
    return false;
  }

  const char *prepared_root_ca = nullptr;
  if(cert->getType() == Certificate::Type::Root && !prepareRootCa(cert, nullptr, prepared_root_ca)) {
    return false;
  }

  _certs.push_back(cert);

  if(save && !saveCertificate(cert))
  {
    _certs.pop_back();
    if(nullptr != prepared_root_ca && prepared_root_ca != root_ca) {
      delete[] prepared_root_ca;
    }
    return false;
  }

  if(nullptr != prepared_root_ca) {
    replaceRootCa(prepared_root_ca);
  }

  if(id != nullptr) {
    *id = cert->getId();
  }

  return true;
}

/**
 * Prepare replacement trust and remove backing records before removing a live ID.
 * @return Removed on success, NotFound if no live ID matches, or Error on a
 * reported preparation/storage failure. Error retains the live certificate and
 * trust; it does not restore aliases already deleted by the storage operation.
 */
CertificateStore::RemoveResult CertificateStore::removeCertificate(uint64_t id)
{
  for(std::vector<Certificate *>::iterator it = _certs.begin(); it != _certs.end(); ++it)
  {
    Certificate *cert = *it;
    if(cert->getId() == id)
    {
      DBUGF("Removing certificate %p", cert);
      DBUGVAR(cert->getId(), HEX);

      const char *prepared_root_ca = nullptr;
      if(cert->getType() == Certificate::Type::Root &&
         !prepareRootCa(nullptr, cert, prepared_root_ca)) {
        return RemoveResult::Error;
      }

      if(!removeCertificate(cert)) {
        if(nullptr != prepared_root_ca && prepared_root_ca != root_ca) {
          delete[] prepared_root_ca;
        }
        return RemoveResult::Error;
      }

      _certs.erase(it);
      if(nullptr != prepared_root_ca) {
        replaceRootCa(prepared_root_ca);
      }
      delete cert;

      return RemoveResult::Removed;
    }
  }

  return RemoveResult::NotFound;
}

const char *CertificateStore::getCertificate(uint64_t id)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    return cert->getCert().c_str();
  }

  return nullptr;
}

const char *CertificateStore::getKey(uint64_t id)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    return cert->getKey().c_str();
  }

  return nullptr;
}

bool CertificateStore::getCertificate(uint64_t id, std::string &certificate)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    certificate = cert->getCert();
    return true;
  }

  return false;
}

bool CertificateStore::getKey(uint64_t id, std::string &key)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    key = cert->getKey();
    return true;
  }

  return false;
}

bool CertificateStore::serializeCertificates(DynamicJsonDocument &doc, uint32_t flags)
{
  doc.to<JsonArray>();
  for(auto &c : _certs)
  {
    DBUGF("c = %p", c);
    DBUGVAR(c->getId(), HEX);
    JsonObject obj = doc.createNestedObject();
    c->serialize(obj);
  }
  return true;
}

bool CertificateStore::serializeCertificate(DynamicJsonDocument &doc, uint64_t id, uint32_t flags)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    cert->serialize(doc);
    return true;
  }
  return false;
}

size_t CertificateStore::certificateCount()
{
  return _certs.size();
}

bool CertificateStore::serializeCertificateAt(DynamicJsonDocument &doc, size_t index, uint32_t flags)
{
  if(index >= _certs.size()) {
    return false;
  }

  Certificate *cert = _certs[index];
  if(nullptr == cert) {
    return false;
  }

  JsonObject obj = doc.to<JsonObject>();
  return cert->serialize(obj, flags);
}

bool CertificateStore::findCertificate(uint64_t id, Certificate *&cert)
{
  for(auto &c : _certs)
  {
    if(c && c->getId() == id)
    {
      cert = c;
      return true;
    }
  }

  return false;
}

bool CertificateStore::findCertificate(uint64_t id, int &index)
{
  int i = 0;
  for(auto &c : _certs)
  {
    if(c && c->getId() == id) {
      index = i;
      return true;
    }
    i++;
  }

  return false;
}

/**
 * Prepare default and custom roots without replacing the active bundle.
 * @param additional Optional root to include before it enters the live list.
 * @param excluded Optional live root to omit from the prepared bundle.
 * @param prepared Receives root_ca or a new array on success; unchanged on failure.
 * The caller must publish the array with replaceRootCa or release it with delete[].
 * @return False if the replacement array cannot be allocated.
 */
bool CertificateStore::prepareRootCa(Certificate *additional, Certificate *excluded,
                                     const char *&prepared)
{
  size_t len = 1;
  for(auto &c : _certs)
  {
    if(c != excluded && c->getType() == Certificate::Type::Root) {
      len += c->getCert().length();
    }
  }

  if(nullptr != additional && additional->getType() == Certificate::Type::Root) {
    len += additional->getCert().length();
  }

  DBUGVAR(len);

  if(len <= 1)
  {
    DBUGLN("Using default root certificates");
    prepared = root_ca;
    return true;
  }

  len += root_ca_len;
  DBUGVAR(len);

  char *new_root_ca = new (std::nothrow) char[len];
  if(new_root_ca == nullptr)
  {
    DBUGLN("Memory allocation failed while preparing root certificates");
    return false;
  }

  char *ptr = new_root_ca;
  memcpy(ptr, root_ca, root_ca_len);
  ptr += root_ca_len;

  for(auto &c : _certs)
  {
    if(c != excluded && c->getType() == Certificate::Type::Root) {
      strcpy(ptr, c->getCert().c_str());
      ptr += c->getCert().length();
    }
  }

  if(nullptr != additional && additional->getType() == Certificate::Type::Root)
  {
    strcpy(ptr, additional->getCert().c_str());
  }

  DBUGLN("Using custom root certificates");
  prepared = new_root_ca;
  return true;
}

/** Take ownership of a prepared bundle (or root_ca), releasing the old owned array. */
void CertificateStore::replaceRootCa(const char *replacement)
{
  if(_root_ca != root_ca) {
    delete[] _root_ca;
  }
  _root_ca = replacement;
}

/**
 * Load committed records and attempt to discard .tmp entries without loading them.
 * @return False if an encountered record fails to load or stale-file removal fails;
 * iteration continues so other valid records can still enter the live store.
 */
bool CertificateStore::loadCertificates()
{
  bool loaded = true;

  File certificateDir = LittleFS.open(CERTIFICATE_BASE_DIRECTORY);
  if(certificateDir && certificateDir.isDirectory())
  {
    File file = certificateDir.openNextFile();
    while(file)
    {
      if(!file.isDirectory())
      {
        String name = file.name();
        DBUGVAR(name.c_str());
        if(name.endsWith(".tmp"))
        {
          file.close();
          String path = String(CERTIFICATE_BASE_DIRECTORY) + "/" + name;
          if(!LittleFS.remove(path)) {
            loaded = false;
          }
        }
        else if(false == loadCertificate(name)) {
          loaded = false;
        }
      }

      file = certificateDir.openNextFile();
    }
  } else {
    LittleFS.mkdir(CERTIFICATE_BASE_DIRECTORY);
  }

  return loaded;
}

bool CertificateStore::loadCertificate(String &name)
{
  bool loaded = false;

  String path = String(CERTIFICATE_BASE_DIRECTORY) + "/" + name;
  DBUGF("Loading certificate %s", path.c_str());

  File file = LittleFS.open(path);
  if(file)
  {
    DynamicJsonDocument doc(CERTIFICATE_JSON_BUFFER_SIZE);
    DeserializationError err = deserializeJson(doc, file);
    if(DeserializationError::Code::Ok == err)
    {
      //#ifdef ENABLE_DEBUG
      //DBUG("Certificate loaded: ");
      //serializeJson(doc, DEBUG_PORT);
      //DBUGLN("");
      //#endif
      loaded = addCertificate(doc, nullptr, false);
    }

    file.close();
  }

  return loaded;
}

/**
 * Serialize a complete private record, stage it, and publish through rename.
 * @return False on reported serialization, allocation or storage failure.
 * The Arduino flush/close APIs do not expose their failures through this path.
 */
bool CertificateStore::saveCertificate(Certificate *cert)
{
  std::string id = certificate_id_hex(cert->getId());
  String name = String(CERTIFICATE_BASE_DIRECTORY) + "/" + id.c_str() + ".json";

  DynamicJsonDocument doc(CERTIFICATE_JSON_BUFFER_SIZE);
  JsonObject object = doc.to<JsonObject>();
  cert->serialize(object, Certificate::Flags::SHOW_PRIVATE_KEY);

  if(doc.overflowed()) {
    return false;
  }

  size_t expected = measureJson(doc);
  if(0 == expected) {
    return false;
  }

  std::unique_ptr<char[]> record(new (std::nothrow) char[expected + 1]);
  if(!record) {
    return false;
  }

  size_t serialized = serializeJson(doc, record.get(), expected + 1);
  if(serialized != expected) {
    return false;
  }

  class LittleFsCertificateStorage
  {
    public:
      /** Return the backend's boolean lookup result, which cannot classify errors. */
      bool exists(const char *path) const { return LittleFS.exists(path); }
      /** Attempt to remove a staging path and return the backend's result. */
      bool remove(const char *path) { return LittleFS.remove(path); }
      /** Check record space plus the filesystem helper's metadata margin. */
      bool hasSpace(size_t needed) const { return littlefs_has_space(needed); }
      /** Publish the staged path using the backend's rename result. */
      bool rename(const char *from, const char *to) { return LittleFS.rename(from, to); }

      /**
       * Open staging storage and report the number of bytes accepted by write.
       * @return False if opening fails; true otherwise, even for a short write.
       * The caller checks written. Flush and close are invoked but their void
       * APIs cannot report a late failure.
       */
      bool write(const char *path, const uint8_t *data, size_t size, size_t &written)
      {
        File file = LittleFS.open(path, "w");
        if(!file) {
          written = 0;
          return false;
        }

        written = file.write(data, size);
        file.flush();
        file.close();
        return true;
      }
  } storage;

  // Include the configured directory and the largest 64-bit hex ID in the bound.
  constexpr size_t MAX_FINAL_PATH_LENGTH = sizeof(CERTIFICATE_BASE_DIRECTORY "/FFFFFFFFFFFFFFFF.json") - 1;
  return certificate_storage_commit<MAX_FINAL_PATH_LENGTH>(storage, name.c_str(),
                                    reinterpret_cast<const uint8_t *>(record.get()),
                                    serialized);
}

bool CertificateStore::removeCertificate(Certificate *cert)
{
  std::string id = certificate_id_hex(cert->getId());
  String canonical = String(CERTIFICATE_BASE_DIRECTORY) + "/" + id.c_str() + ".json";
  String legacy_id = id.c_str();
  legacy_id.toLowerCase();
  String legacy = String(CERTIFICATE_BASE_DIRECTORY) + "/" + legacy_id + ".json";

  bool found = false;
  if(LittleFS.exists(canonical)) {
    found = true;
    if(!LittleFS.remove(canonical)) {
      return false;
    }
  }

  if(legacy != canonical && LittleFS.exists(legacy)) {
    found = true;
    if(!LittleFS.remove(legacy)) {
      return false;
    }
  }

  return found;
}
