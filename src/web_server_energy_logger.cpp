#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include "web_server.h"
#include "energy_logger.h"
#include "debug.h"

// Forward declarations
extern EnergyLogger energyLogger;

typedef const __FlashStringHelper *fstr_t;

void handleEnergyRaw(MongooseHttpServerRequest *request);
void handleEnergyDaily(MongooseHttpServerRequest *request);
void handleEnergyMonthly(MongooseHttpServerRequest *request);
void handleEnergyAnnual(MongooseHttpServerRequest *request);

void handleEnergyRaw(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = nullptr;

  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    const size_t capacity = JSON_ARRAY_SIZE(720) + 720 * JSON_OBJECT_SIZE(4) + 256;
    DynamicJsonDocument doc(capacity);

    // Get max_samples from query parameter if provided
    int max_samples = 0;
    char param_buf[8];
    if (request->getParam("max", param_buf, sizeof(param_buf)) >= 0) {
      max_samples = atoi(param_buf);
    }

    energyLogger.getRawSamples(doc, max_samples);

    response->setCode(200);
    serializeJson(doc, *response);
  } else if (HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

void handleEnergyDaily(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = nullptr;

  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    const size_t capacity = JSON_ARRAY_SIZE(400) + 400 * JSON_OBJECT_SIZE(4) + 256;
    DynamicJsonDocument doc(capacity);

    // Get days from query parameter if provided (default 365)
    int days = 365;
    char param_buf[8];
    if (request->getParam("days", param_buf, sizeof(param_buf)) >= 0) {
      int requested = atoi(param_buf);
      if (requested > 0 && requested <= 400) {
        days = requested;
      }
    }

    energyLogger.getDailyMetrics(doc, days);

    response->setCode(200);
    serializeJson(doc, *response);
  } else if (HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

void handleEnergyMonthly(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = nullptr;

  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    const size_t capacity = JSON_ARRAY_SIZE(200) + 200 * JSON_OBJECT_SIZE(4) + 256;
    DynamicJsonDocument doc(capacity);

    energyLogger.getMonthlyMetrics(doc);

    response->setCode(200);
    serializeJson(doc, *response);
  } else if (HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

void handleEnergyAnnual(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = nullptr;

  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    const size_t capacity = JSON_ARRAY_SIZE(100) + 100 * JSON_OBJECT_SIZE(4) + 256;
    DynamicJsonDocument doc(capacity);

    energyLogger.getAnnualMetrics(doc);

    response->setCode(200);
    serializeJson(doc, *response);
  } else if (HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}
