#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_CETRIFICATES)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
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
    _id = obj["id"];
  } else {
    uint64_t a =  x509.sig.p[0] << 24 | x509.sig.p[1] << 16 |
                  x509.sig.p[2] << 8 | x509.sig.p[3];
    uint64_t b =  x509.sig.p[4] << 24 | x509.sig.p[5] << 16 |
                  x509.sig.p[6] << 8 | x509.sig.p[7];
    _id = a << 32 | b;
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
  doc["id"] = _id;
  doc["type"] = _type.toString();
  doc["name"] = _name;
  doc["certificate"] = _cert;
  if(_type == Type::Client) {
    doc["key"] = flags & Flags::REDACT_PRIVATE_KEY ? "__REDACTED__" : _key;
  }

  return true;
}

CertificateStore::CertificateStore() :
  _certs()
{
}

CertificateStore::~CertificateStore()
{
}

bool CertificateStore::begin()
{
  return true;
}

const char *CertificateStore::getRootCa()
{
  return root_ca;
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

bool CertificateStore::addCertificate(DynamicJsonDocument &doc, uint64_t *id)
{
  Certificate *cert = new Certificate();
  if(cert)
  {
    if(cert->deserialize(doc))
    {
      if(addCertificate(cert, id)) {
        return true;
      }
    }
    delete cert;
  }

  return false;
}

bool CertificateStore::addCertificate(Certificate *cert, uint64_t *id)
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
  return true;
}

bool CertificateStore::removeCertificate(uint64_t id)
{
  for(std::vector<Certificate *>::iterator it = _certs.begin(); it != _certs.end(); ++it)
  {
    if((*it)->getId() == id)
    {
      _certs.erase(it);
      delete *it;
      return true;
    }
  }

  return false;
}

const char *CertificateStore::getCertificate(uint64_t id)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    return cert->getCert();
  }

  return nullptr;
}

const char *CertificateStore::getKey(uint64_t id)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    return cert->getKey();
  }

  return nullptr;
}

bool CertificateStore::serializeCertificates(DynamicJsonDocument &doc, uint32_t flags)
{
  doc.to<JsonArray>();
  for(auto &c : _certs)
  {
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
    if(c->getId() == id)
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
    if(c->getId() == id) {
      index = i;
      return true;
    }
    i++;
  }

  return false;
}
