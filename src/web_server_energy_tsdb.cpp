#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_TSDB

#include <Arduino.h>
#include "web_server.h"
#include "esp_tsdb.h"
#include "tsdb_sample.h"
#include "debug.h"
#include <time.h>

// ---------------------------------------------------------------------------
// /energy/raw – tsdb-backed handler
//
// JSON shape is IDENTICAL to the non-tsdb EnergyLogger version so the GUI
// (SessionChart) requires no changes:
//
//   { "samples": [
//       { "ts": <epoch_s>, "a": <amps>, "t": <temp_c>, "e": <energy_wh>,
//         "s": <soc_or_-1>, "v": <volts>, "p": <power_w>, "pilot": <pilot_a> },
//       ...
//   ]}
//
// Keys "v", "p", "pilot" are additive – the GUI ignores unknown keys.
// "s" == -1 matches the existing sentinel for "no SoC reading".
//
// Query params honoured (same as the legacy handler):
//   max=N      limit response to at most N samples
//   before=T   return the 3-hour window immediately before epoch T
//              (mirrors EnergyLogger::getRawSamples paging)
// ---------------------------------------------------------------------------

#define TSDB_RAW_DEFAULT_WINDOW_S  (3 * 3600)   // 3 hours → ~180 samples

void handleEnergyRaw(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = nullptr;

  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    // ---- parse query params ----
    char max_buf[8]    = {0};
    char before_buf[12] = {0};

    int max_samples = 0;
    if (request->getParam("max", max_buf, sizeof(max_buf)) >= 0 && max_buf[0] != '\0') {
      max_samples = atoi(max_buf);
    }

    time_t before_ts = 0;
    if (request->getParam("before", before_buf, sizeof(before_buf)) >= 0 && before_buf[0] != '\0') {
      before_ts = (time_t)atol(before_buf);
    }

    // ---- build time range ----
    uint32_t end_ts, start_ts;
    if (before_ts > 0) {
      // Page back: one full default-window ending at 'before'
      end_ts   = (uint32_t)before_ts;
      start_ts = (uint32_t)((before_ts > TSDB_RAW_DEFAULT_WINDOW_S)
                              ? before_ts - TSDB_RAW_DEFAULT_WINDOW_S : 0);
    } else {
      uint32_t now = (uint32_t)time(NULL);
      end_ts   = now;
      start_ts = (now > TSDB_RAW_DEFAULT_WINDOW_S) ? now - TSDB_RAW_DEFAULT_WINDOW_S : 0;
    }

    // ---- stream JSON directly into response ----
    // Avoid a large DynamicJsonDocument: hand-build the array into the
    // streaming response buffer (identical to what serializeJson would produce).
    response->setCode(200);
    response->print("{\"samples\":[");

    tsdb_query_t q;
    bool opened = false;
    bool first  = true;
    int  count  = 0;

    if (tsdb_query_init(&q, start_ts, end_ts, NULL, TSDB_NUM_COLS) == ESP_OK) {
      opened = true;
      uint32_t ts;
      int16_t  v[TSDB_NUM_COLS];

      while (tsdb_query_next(&q, &ts, v) == ESP_OK) {
        if (max_samples > 0 && count >= max_samples) break;

        double amps     = tsdb_unscale(TSDB_COL_AMPS,   v[TSDB_COL_AMPS]);
        double volts    = tsdb_unscale(TSDB_COL_VOLTS,  v[TSDB_COL_VOLTS]);
        double power_w  = tsdb_unscale(TSDB_COL_POWER,  v[TSDB_COL_POWER]);
        double energy   = tsdb_unscale(TSDB_COL_ENERGY, v[TSDB_COL_ENERGY]);
        double temp_c   = tsdb_unscale(TSDB_COL_TEMP,   v[TSDB_COL_TEMP]);
        double soc_d    = tsdb_unscale(TSDB_COL_SOC,    v[TSDB_COL_SOC]);
        double pilot_a  = tsdb_unscale(TSDB_COL_PILOT,  v[TSDB_COL_PILOT]);
        int    soc      = (int)soc_d;   // -1 when tsdb_unscale returns -1.0

        char buf[192];
        if (soc >= 0) {
          snprintf(buf, sizeof(buf),
            "%s{\"ts\":%lu,\"a\":%.2f,\"t\":%.1f,\"e\":%.2f,\"s\":%d"
            ",\"v\":%.1f,\"p\":%.1f,\"pilot\":%.1f}",
            first ? "" : ",",
            (unsigned long)ts, amps, temp_c, energy, soc,
            volts, power_w, pilot_a);
        } else {
          snprintf(buf, sizeof(buf),
            "%s{\"ts\":%lu,\"a\":%.2f,\"t\":%.1f,\"e\":%.2f,\"s\":-1"
            ",\"v\":%.1f,\"p\":%.1f,\"pilot\":%.1f}",
            first ? "" : ",",
            (unsigned long)ts, amps, temp_c, energy,
            volts, power_w, pilot_a);
        }
        response->print(buf);
        first = false;
        count++;
      }
    }

    if (opened) {
      tsdb_query_close(&q);
    }

    response->print("]}");

  } else if (HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

#endif // ENABLE_TSDB
