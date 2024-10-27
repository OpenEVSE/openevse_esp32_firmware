#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_CETRIFICATES)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <LittleFS.h>

#include "emonesp.h"
#include "certificates.h"
#include "root_ca.h"

#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"

bool CertificateStore::Certificate::deserialize(JsonObject &obj)
{
  _name = obj["name"].as<std::string>();

  std::string cert = obj["certificate"].as<std::string>();

  // Check if the certificate is valid
  mbedtls_x509_crt x509;
  mbedtls_x509_crt_init(&x509);
  int ret = mbedtls_x509_crt_parse(&x509, (const unsigned char *)cert.c_str(), cert.length() + 1);
  if(ret != 0) {
    DBUGVAR(ret);
    DBUGVAR(cert.c_str());
    return false;
  }

#if defined(ENABLE_DEBUG_CETRIFICATES)
  char p[1024];
  mbedtls_x509_dn_gets(p, sizeof(p), &x509.issuer);
  DBUGF("issuer: %s", p);
  mbedtls_x509_dn_gets(p, sizeof(p), &x509.subject);
  DBUGF("subject: %s", p);
#endif

  _cert = cert;
  if(obj.containsKey("id")) {
    _id = std::stoull(obj["id"].as<std::string>(), nullptr, 16);
  } else {
    uint64_t id = 0;
    for(int i = 0; i < x509.serial.len; i++) {
      id = id << 8 | x509.serial.p[i];
    }
    _id = id;
  }
  if(obj.containsKey("key"))
  {
    std::string key = obj["key"].as<std::string>();

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_key(&pk, (const unsigned char *)key.c_str(), key.length() + 1, NULL, 0);
    if(ret != 0) {
      DBUGVAR(ret);
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
  doc["id"] = String(_id, HEX);
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
    delete _root_ca;
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

  if(id != nullptr) {
    *id = cert->getId();
  }

  _certs.push_back(cert);

  if(cert->getType() == Certificate::Type::Root) {
    buildRootCa();
  }

  if(save) {
    return saveCertificate(cert);
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

bool CertificateStore::buildRootCa()
{
  size_t len = 1;
  for(auto &c : _certs)
  {
    if(c->getType() == Certificate::Type::Root) {
      len += c->getCert().length();
    }
  }

  DBUGVAR(len);
  DBUGF("%p != %p", _root_ca, root_ca);

  if(_root_ca != root_ca) {
    delete _root_ca;
  }

  if(len <= 1)
  {
    DBUGLN("Using default root certificates");
    _root_ca = root_ca;
    return true;
  }

  len += root_ca_len;
  DBUGVAR(len);

  char *new_root_ca = new char[len];
  if(new_root_ca == nullptr)
  {
    DBUGLN("Memmory allocation failed, using default root certificates");
    _root_ca = root_ca;
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

  DBUGLN("Using custom root certificates");
  _root_ca = new_root_ca;
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
        if(false == loadCertificate(name)) {
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
  String name = String(CERTIFICATE_BASE_DIRECTORY) + "/" + String(cert->getId(), HEX) + ".json";
  File file = LittleFS.open(name, "w");
  if(file)
  {
    DynamicJsonDocument doc(CERTIFICATE_JSON_BUFFER_SIZE);
    JsonObject object = doc.to<JsonObject>();
    cert->serialize(object, Certificate::Flags::SHOW_PRIVATE_KEY);
    serializeJson(doc, file);
    file.close();
    return true;
  }

  return false;
}

bool CertificateStore::removeCertificate(Certificate *cert)
{
  String name = String(CERTIFICATE_BASE_DIRECTORY) + "/" + String(cert->getId(), HEX) + ".json";
  if(LittleFS.remove(name))
  {
    return true;
  }

  return false;
}
