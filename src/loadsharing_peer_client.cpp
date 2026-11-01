#include "loadsharing_peer_client.h"

#include <ArduinoJson.h>
#include <MongooseHttpClient.h>
#include <memory>

#include "app_config.h"
#include "debug.h"
#include "loadsharing_auth.h"

static MongooseHttpClient nonceHttpClient;

void loadSharingAuthorisePeerRequest(const String &baseUrl, const char *method, const char *path,
                                     const String &body, LoadSharingPeerAuthHandler ready)
{
  if (loadsharing_group_key.isEmpty()) {
    ready("");
    return;
  }

  // onClose follows onResponse on a good exchange and is the only event on a
  // failed one, so whichever comes first answers and the other finds it empty.
  auto pending = std::make_shared<LoadSharingPeerAuthHandler>(std::move(ready));
  auto answer = [pending](const String &authorization) {
    if (*pending) {
      LoadSharingPeerAuthHandler handler = std::move(*pending);
      *pending = nullptr;
      handler(authorization);
    }
  };

  String url = baseUrl + "/loadsharing/nonce";
  MongooseHttpClientRequest *req = nonceHttpClient.beginRequest(url.c_str());
  req->setMethod(HTTP_GET);

  String signedMethod(method);
  String signedPath(path);
  req->onResponse([answer, signedMethod, signedPath, body](MongooseHttpClientResponse *response) {
    if (response->respCode() != 200) {
      answer("");
      return;
    }

    JsonDocument doc;
    MongooseString responseBody = response->body();
    if (deserializeJson(doc, responseBody.c_str(), responseBody.length()) ||
        !doc["nonce"].is<const char *>()) {
      answer("");
      return;
    }

    answer(peer_auth_header(loadsharing_group_key.c_str(), signedMethod.c_str(),
                            signedPath.c_str(), doc["nonce"].as<const char *>(),
                            std::string(body.c_str(), body.length())).c_str());
  });

  req->onClose([answer]() {
    answer("");
  });

  if (!req->send()) {
    // send() failing means onClose() never fires (see syncPeerGroupMembership).
    req->onResponse(MongooseHttpResponseHandler());
    req->onClose(MongooseSocketCloseHandler());
    delete req;
    answer("");
  }
}
