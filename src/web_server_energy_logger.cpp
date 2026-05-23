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
    const size_t capacity = JSON_ARRAY_SIZE(ENERGY_LOGGER_BUFFER_SIZE) + ENERGY_LOGGER_BUFFER_SIZE * JSON_OBJECT_SIZE(4) + 256;
    DynamicJsonDocument doc(capacity);

    char max_buf[8]    = {0};
    char before_buf[12] = {0};

    int max_samples = 0;
    if (request->getParam("max", max_buf, sizeof(max_buf)) >= 0) {
      max_samples = atoi(max_buf);
    }

    time_t before_ts = 0;
    if (request->getParam("before", before_buf, sizeof(before_buf)) >= 0 && before_buf[0] != '\0') {
      before_ts = (time_t)atol(before_buf);
    }

    energyLogger.getRawSamples(doc, max_samples, before_ts);

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
    // Quarterly file: max 92 entries, each with a 10-char date string
    const size_t capacity = JSON_ARRAY_SIZE(93) + 93 * JSON_OBJECT_SIZE(4) + 93 * 16 + 64;
    DynamicJsonDocument doc(capacity);

    char year_buf[6] = {0};
    char qtr_buf[4]  = {0};

    int year = 0;
    if (request->getParam("year", year_buf, sizeof(year_buf)) >= 0 && year_buf[0] != '\0') {
      int y = atoi(year_buf);
      if (y >= 2020 && y <= 2100) year = y;
    }

    int quarter = 0;
    if (request->getParam("quarter", qtr_buf, sizeof(qtr_buf)) >= 0 && qtr_buf[0] != '\0') {
      int q = atoi(qtr_buf);
      if (q >= 1 && q <= 4) quarter = q;
    }

    energyLogger.getDailyMetrics(doc, year, quarter);

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

    char year_buf[6] = {0};
    int year = 0;
    if (request->getParam("year", year_buf, sizeof(year_buf)) >= 0 && year_buf[0] != '\0') {
      int y = atoi(year_buf);
      if (y >= 2020 && y <= 2100) year = y;
    }

    energyLogger.getMonthlyMetrics(doc, year);

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
