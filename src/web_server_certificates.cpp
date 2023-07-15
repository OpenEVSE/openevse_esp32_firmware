#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB_CETRIFICATES)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>

typedef const __FlashStringHelper *fstr_t;

#include "emonesp.h"
#include "web_server.h"
#include "certificates.h"

// -------------------------------------------------------------------
//
// url: /certificates
// -------------------------------------------------------------------
void
handleCertificatesGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint32_t certificate)
{
  const size_t capacity = 16 * 1024;
  DynamicJsonDocument doc(capacity);

  bool success = (UINT32_MAX == certificate) ?
    certs.serializeCertificates(doc) :
    certs.serializeCertificate(doc, certificate);

  if(success) {
    response->setCode(200);
    serializeJson(doc, *response);
  } else {
    response->setCode(404);
    response->print("{\"msg\":\"Not found\"}");
  }
}

void
handleCertificatesPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint32_t certificate)
{
  String body = request->body().toString();
  DBUGVAR(body);

  if(UINT32_MAX == certificate)
  {
    DynamicJsonDocument doc(8 * 1024);
    DeserializationError jsonError = deserializeJson(doc, body);
    if(DeserializationError::Ok == jsonError)
    {
      uint32_t id = UINT32_MAX;
      if(certs.addCertificate(doc, &id))
      {
        DBUGVAR(id);
        doc.clear();
        doc["id"] = id;
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

void handleCertificatesDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint32_t certificate)
{
  if(UINT32_MAX != certificate)
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

void
handleCertificates(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  uint32_t certificate = UINT32_MAX;

  String path = request->uri();
  if(path.length() > CERTIFICATES_PATH_LEN) {
    String clientStr = path.substring(CERTIFICATES_PATH_LEN);
    DBUGVAR(clientStr);
    certificate = clientStr.toInt();
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
