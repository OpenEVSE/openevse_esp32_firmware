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
    _id = std::stoull(obj["id"].as<std::string>(), nullptr, 16);
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

bool CertificateStore::addCertificate(const char *name, const char *certificate, const char *key, uint64_t *id)
{
  Certificate *cert = new Certificate(certificate, key);
  if(cert)
  {
    if(addCertificate(cert, id)) {
      return true;
    }

    delete cert;
  }
  return false;
}

bool CertificateStore::addCertificate(const char *name, const char *certificate, uint64_t *id)
{
  Certificate *cert = new Certificate(certificate);
  if(cert)
  {
    if(addCertificate(cert, id)) {
      return true;
    }

    delete cert;
  }
  return false;
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

bool CertificateStore::addCertificate(Certificate *cert, uint64_t *id, bool save)
{
  uint64_t certId = cert->getId();
  if(findCertificate(certId, cert)) {
    DBUGF("Certificate already exists");
    return false;
  }

  const char *prepared_root_ca = nullptr;
  if(cert->getType() == Certificate::Type::Root && !prepareRootCa(cert, prepared_root_ca)) {
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

bool CertificateStore::removeCertificate(uint64_t id)
{
  for(std::vector<Certificate *>::iterator it = _certs.begin(); it != _certs.end(); ++it)
  {
    Certificate *cert = *it;
    if(cert->getId() == id)
    {
      DBUGF("Removing certificate %p", cert);
      DBUGVAR(cert->getId(), HEX);

      _certs.erase(it);
      if(cert->getType() == Certificate::Type::Root) {
        buildRootCa();
      }

      removeCertificate(cert);
      delete cert;

      return true;
    }
  }

  return false;
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

bool CertificateStore::prepareRootCa(Certificate *additional, const char *&prepared)
{
  size_t len = 1;
  for(auto &c : _certs)
  {
    if(c->getType() == Certificate::Type::Root) {
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
    if(c->getType() == Certificate::Type::Root) {
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

void CertificateStore::replaceRootCa(const char *replacement)
{
  if(_root_ca != root_ca) {
    delete[] _root_ca;
  }
  _root_ca = replacement;
}

bool CertificateStore::buildRootCa()
{
  const char *prepared = nullptr;
  if(!prepareRootCa(nullptr, prepared)) {
    return false;
  }
  replaceRootCa(prepared);
  return true;
}

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
  std::string record;
  record.reserve(expected);
  size_t serialized = serializeJson(doc, record);
  if(0 == expected || serialized != expected) {
    return false;
  }

  class LittleFsCertificateStorage
  {
    public:
      bool exists(const char *path) const { return LittleFS.exists(path); }
      bool remove(const char *path) { return LittleFS.remove(path); }
      bool hasSpace(size_t needed) const { return littlefs_has_space(needed); }
      bool rename(const char *from, const char *to) { return LittleFS.rename(from, to); }

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

  return certificate_storage_commit(storage, name.c_str(),
                                    reinterpret_cast<const uint8_t *>(record.data()),
                                    record.size());
}

bool CertificateStore::removeCertificate(Certificate *cert)
{
  std::string id = certificate_id_hex(cert->getId());
  String name = String(CERTIFICATE_BASE_DIRECTORY) + "/" + id.c_str() + ".json";
  if(LittleFS.remove(name))
  {
    return true;
  }

  return false;
}
