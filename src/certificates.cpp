#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_DIVERT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include "emonesp.h"
#include "certificates.h"
#include "root_ca.h"

uint32_t CertificateStore::_next_cert_id = 0;

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

bool CertificateStore::addCertificate(const char *name, const char *cert, const char *key, uint32_t *id)
{
  return true;
}

bool CertificateStore::removeCertificate(uint32_t id)
{
  return true;
}

const char *CertificateStore::getCertificate(uint32_t id)
{
  return nullptr;
}

const char *CertificateStore::getKey(uint32_t id)
{
  return nullptr;
}

bool CertificateStore::serializeCertificates(DynamicJsonDocument &doc)
{
  return true;
}

bool CertificateStore::serializeCertificate(DynamicJsonDocument &doc, uint32_t id)
{
  return true;
}
