#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB_CETRIFICATES)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

typedef const __FlashStringHelper *fstr_t;

#include "emonesp.h"
#include "web_server.h"
#include "certificates.h"
#include "self_signed_cert.h"
#include "app_config.h" // esp_hostname
#include "net_manager.h" // net.getIp()

// -------------------------------------------------------------------
//
// url: /certificates/root
// -------------------------------------------------------------------
void handleCertificatesGetRootCa(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  response->setCode(200);
  response->setContentType(CONTENT_TYPE_TEXT);
  response->print(certs.getRootCa());
}

// -------------------------------------------------------------------
//
// url: /certificates/self-signed (POST) — generate a self-signed ECDSA
// P-256 certificate and store it through the same validated path as an
// uploaded one (certs.addCertificate(doc, &id)), so it's selectable via
// www_certificate_id exactly like any other stored certificate.
// -------------------------------------------------------------------
void handleCertificatesGenerateSelfSigned(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String certPem, keyPem;
  if(!generateSelfSignedCertificate(esp_hostname, net.getIp(), certPem, keyPem)) {
    response->setCode(500);
    response->print("{\"msg\":\"Failed to generate certificate\"}");
    return;
  }

  DynamicJsonDocument doc(CERTIFICATE_JSON_BUFFER_SIZE);
  doc["name"] = "Self-signed (" + esp_hostname + ")";
  doc["certificate"] = certPem;
  doc["key"] = keyPem;

  uint64_t id = UINT64_MAX;
  if(certs.addCertificate(doc, &id)) {
    doc.clear();
    doc["id"] = String(id, HEX);
    doc["msg"] = "done";
    serializeJson(doc, *response);
    response->setCode(200);
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"Could not add certificate\"}");
  }
}

// -------------------------------------------------------------------
//
// url: /certificates
// -------------------------------------------------------------------
void handleCertificatesGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint64_t certificate)
{
  // List responses redact private keys, so each entry is small — size the
  // buffer off the actual stored certificate count rather than a fixed
  // worst-case block. A blind 4 * CERTIFICATE_JSON_BUFFER_SIZE (32 KB)
  // single allocation was failing silently (ArduinoJson just returns an
  // empty document on OOM, no error) whenever heap was tight, e.g. while an
  // HTTPS session's TLS buffers were already using a chunk of it.
  size_t bufferSize = (UINT64_MAX == certificate) ?
    (1024 + certs.count() * 2048) :
    CERTIFICATE_JSON_BUFFER_SIZE;

  DynamicJsonDocument doc(bufferSize);

  bool success = (UINT64_MAX == certificate) ?
    certs.serializeCertificates(doc) :
    certs.serializeCertificate(doc, certificate);

  if(doc.overflowed()) {
    DBUGF("Certificate JSON document overflowed (buffer %u bytes)", (unsigned)bufferSize);
    response->setCode(507);
    response->print("{\"msg\":\"Out of memory\"}");
    return;
  }

  if(success) {
    response->setCode(200);
    serializeJson(doc, *response);
  } else {
    response->setCode(404);
    response->print("{\"msg\":\"Not found\"}");
  }
}

void handleCertificatesPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint64_t certificate)
{
  String body = request->body().toString();
  DBUGVAR(body);

  if(UINT64_MAX == certificate)
  {
    DynamicJsonDocument doc(CERTIFICATE_JSON_BUFFER_SIZE);
    DeserializationError jsonError = deserializeJson(doc, body);
    if(DeserializationError::Ok == jsonError)
    {
      uint64_t id = UINT64_MAX;
      if(certs.addCertificate(doc, &id))
      {
        DBUGVAR(id, HEX);
        doc.clear();
        doc["id"] = String(id, HEX);
        doc["msg"] = "done";
        serializeJson(doc, *response);
        response->setCode(200);
      } else {
        response->setCode(400);
        response->print("{\"msg\":\"Could not add certificate\"}");
      }
    } else {
      response->setCode(400);
      response->print("{\"msg\":\"Could not parse JSON\"}");
    }
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }
}

void handleCertificatesDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint64_t certificate)
{
  if(UINT64_MAX != certificate)
  {
    if(certs.removeCertificate(certificate)) {
      response->setCode(200);
      response->print("{\"msg\":\"done\"}");
    } else {
      response->setCode(404);
      response->print("{\"msg\":\"Not found\"}");
    }
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }
}

#define CERTIFICATES_PATH_LEN (sizeof("/certificates/") - 1)

void handleCertificates(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  uint64_t certificate = UINT64_MAX;

  String path = request->uri();
  if(path.length() > CERTIFICATES_PATH_LEN) {
    String clientStr = path.substring(CERTIFICATES_PATH_LEN);
    DBUGVAR(clientStr);

    if(clientStr == "root")
    {
      if(HTTP_GET == request->method())
      {
        handleCertificatesGetRootCa(request, response);
        request->send(response);
        return;
      } else {
        response->setCode(405);
        response->print("{\"msg\":\"Method not allowed\"}");
      }
    } else if(clientStr == "self-signed")
    {
      if(HTTP_POST == request->method())
      {
        handleCertificatesGenerateSelfSigned(request, response);
        request->send(response);
        return;
      } else {
        response->setCode(405);
        response->print("{\"msg\":\"Method not allowed\"}");
      }
    } else {
      certificate = std::stoull(clientStr.c_str(), nullptr, 16);
    }
  }

  DBUGVAR(certificate, HEX);

  if(HTTP_GET == request->method()) {
    handleCertificatesGet(request, response, certificate);
  } else if(HTTP_POST == request->method()) {
    handleCertificatesPost(request, response, certificate);
  } else if(HTTP_DELETE == request->method()) {
    handleCertificatesDelete(request, response, certificate);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}
