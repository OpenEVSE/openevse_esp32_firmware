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
// url: /certificates
// -------------------------------------------------------------------
void handleCertificatesGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint64_t certificate)
{
  if(UINT64_MAX == certificate)
  {
    // Emit the array one certificate at a time. Building it in a single
    // document required a buffer sized for every PEM body at once — 32KB here
    // — which is larger than the biggest contiguous block this board can
    // reliably allocate, so the request would fail rather than merely cost a
    // lot. Peak allocation is now one certificate.
    response->setCode(200);
    response->print("[");

    size_t count = certs.certificateCount();
    size_t emitted = 0;
    for(size_t i = 0; i < count; i++)
    {
      DynamicJsonDocument doc(CERTIFICATE_JSON_BUFFER_SIZE);
      if(!certs.serializeCertificateAt(doc, i)) {
        continue;
      }
      // Count what was actually written, not the loop index: a skipped entry
      // would otherwise put a leading comma in front of the first element.
      if(emitted > 0) {
        response->print(",");
      }
      serializeJson(doc, *response);
      emitted++;
    }

    response->print("]");
    return;
  }

  DynamicJsonDocument doc(CERTIFICATE_JSON_BUFFER_SIZE);
  if(certs.serializeCertificate(doc, certificate)) {
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
    } else {
      // A non-hex path segment parsed with a throwing conversion aborted and
      // rebooted the board on GET /certificates/zz. Anything that is not a
      // complete hex id is simply not found.
      if(!certificate_id_from_string(clientStr.c_str(), certificate)) {
        response->setCode(404);
        response->print("{\"msg\":\"Not found\"}");
        request->send(response);
        return;
      }
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
