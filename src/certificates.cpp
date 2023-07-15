#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_CETRIFICATES)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include "emonesp.h"
#include "certificates.h"
#include "root_ca.h"

uint32_t CertificateStore::_next_cert_id = 0;


bool CertificateStore::Certificate::deserialize(JsonObject &obj)
{
  if(obj.containsKey("id")) {
    _id = obj["id"];
  }
  _name = obj["name"].as<std::string>();
  _cert = obj["certificate"].as<std::string>();
  if(obj.containsKey("key")) {
    _type = Type::Client;
    _key = obj["key"].as<std::string>();
  } else {
    _type = Type::Root;
    _key = "";
  }

  DBUGVAR(_id);
  DBUGVAR(_name.c_str());
  DBUGVAR(_cert.c_str());
  DBUGVAR(_key.c_str());

  return true;
}

bool CertificateStore::Certificate::serialize(JsonObject &doc)
{
  doc["id"] = _id;
  doc["type"] = _type.toString();
  doc["name"] = _name;
  doc["certificate"] = _cert;
  if(_type == Type::Client) {
    doc["key"] = _key;
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

bool CertificateStore::addCertificate(Certificate *cert, uint32_t *id)
{
  if(cert != nullptr)
  {
    if(id != nullptr) {
      *id = cert->getId();
    }

    _certs.push_back(cert);
    return true;
  }
  return false;
}

bool CertificateStore::removeCertificate(uint32_t id)
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

const char *CertificateStore::getCertificate(uint32_t id)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    return cert->getCert();
  }

  return nullptr;
}

const char *CertificateStore::getKey(uint32_t id)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    return cert->getKey();
  }

  return nullptr;
}

bool CertificateStore::serializeCertificates(DynamicJsonDocument &doc)
{
  doc.to<JsonArray>();
  for(auto &c : _certs)
  {
    JsonObject obj = doc.createNestedObject();
    c->serialize(obj);
  }
  return true;
}

bool CertificateStore::serializeCertificate(DynamicJsonDocument &doc, uint32_t id)
{
  Certificate *cert = nullptr;
  if(findCertificate(id, cert)) {
    cert->serialize(doc);
    return true;
  }
  return false;
}

bool CertificateStore::findCertificate(uint32_t id, Certificate *&cert)
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

bool CertificateStore::findCertificate(uint32_t id, int &index)
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
