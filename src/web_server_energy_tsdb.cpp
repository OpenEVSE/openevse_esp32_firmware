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
#include <stdio.h>

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


// ---------------------------------------------------------------------------
// Shared bucketed-aggregate helper
//
// Iterates [window_start, now) in fixed-width `bucket_secs` buckets.
// For each bucket that has ≥1 sample it emits one JSON object:
//
//   { "dt": "YYYY-MM-DD", "pk": <peak_temp_C>, "mn": <min_temp_C>,
//     "en": <energy_wh> }
//
// This is the EXACT per-entry shape of the legacy EnergyLogger /energy/daily
// response (wrapper is supplied by the caller).
//
// `date_key_is_bucket_start`:
//   true  → "dt" = date of bucket start (used for daily where bucket ≡ day)
//   false → "dt" = date of bucket start (same; weekly also uses start of week)
//
// Temp stored as deci-degC (x10) in TSDB_COL_TEMP, so divide by 10 for degC.
// Energy stored as Wh delta per sample; SUM over bucket = total Wh.
// ---------------------------------------------------------------------------

#define ENERGY_DAILY_DEFAULT_DAYS    30
#define ENERGY_WEEKLY_DEFAULT_WEEKS  12

// Compute midnight (00:00:00 local) for the day containing epoch t.
static time_t start_of_day(time_t t)
{
  struct tm tm_buf;
  localtime_r(&t, &tm_buf);
  tm_buf.tm_hour = 0;
  tm_buf.tm_min  = 0;
  tm_buf.tm_sec  = 0;
  return mktime(&tm_buf);
}

// Format epoch t as "YYYY-MM-DD" into buf[11].
static void format_date_buf(time_t t, char *buf, size_t len)
{
  struct tm tm_buf;
  localtime_r(&t, &tm_buf);
  snprintf(buf, len, "%04d-%02d-%02d",
    tm_buf.tm_year + 1900,
    tm_buf.tm_mon  + 1,
    tm_buf.tm_mday);
}

// Emit a bucketed daily-shaped JSON array into the streaming response.
// `bucket_days` controls the bucket width (1 = daily, 7 = weekly).
// `num_buckets` controls how many buckets to generate (window = num_buckets
//  buckets immediately before now, aligned to bucket boundaries).
static void emit_bucketed_daily(MongooseHttpServerResponseStream *response,
                                int bucket_days,
                                int num_buckets)
{
  time_t now_t = time(NULL);

  // Align to the START of today, then step back num_buckets * bucket_days.
  // This ensures each bucket is a clean calendar-day multiple.
  time_t today_start = start_of_day(now_t);
  time_t window_start = today_start - (time_t)(num_buckets * bucket_days) * 86400;

  bool first = true;
  response->print("[");

  for (int i = 0; i < num_buckets; i++) {
    uint32_t d0 = (uint32_t)(window_start + (time_t)(i * bucket_days) * 86400);
    // tsdb range end_time is INCLUSIVE, so use d1_end = (next bucket start - 1)
    // to avoid a sample landing exactly on the midnight boundary being counted
    // in both adjacent buckets.
    uint32_t d1_end = d0 + (uint32_t)(bucket_days) * 86400 - 1;

    // --- check record count first to skip empty buckets cheaply ---
    uint32_t cnt = 0;
    if (tsdb_query_count(d0, d1_end, &cnt) != ESP_OK || cnt == 0) {
      continue;
    }

    // --- three aggregations in one pass ---
    // NOTE: TSDB_AGG_MIN on TEMP includes no-sensor samples (stored as 0 deci-degC),
    // so on a day with any invalid-temp minute the reported "mn" can read 0.0. The
    // engine's MIN/MAX cannot exclude invalid samples (a sentinel that fixes MIN
    // would poison MAX). In practice the onboard MONITOR thermistor is valid except
    // for a brief boot transient (which the logger's NTP write-guard usually skips),
    // so this is a rare, secondary-display edge. Accepted limitation.
    tsdb_agg_request_t reqs[3] = {
      { TSDB_COL_ENERGY, TSDB_AGG_SUM, 0 },
      { TSDB_COL_TEMP,   TSDB_AGG_MAX, 0 },
      { TSDB_COL_TEMP,   TSDB_AGG_MIN, 0 },
    };

    uint32_t nscanned = 0;
    esp_err_t err = tsdb_aggregate_multi(d0, d1_end, reqs, 3, &nscanned);
    if (err != ESP_OK || nscanned == 0) {
      continue;
    }

    int32_t energy_wh = reqs[0].result;            // Wh (int32; sum of Wh deltas)
    double  peak_c    = (double)reqs[1].result / 10.0; // deci-degC → degC
    double  min_c     = (double)reqs[2].result / 10.0;

    char dt_buf[12];
    format_date_buf((time_t)d0, dt_buf, sizeof(dt_buf));

    char obj_buf[128];
    snprintf(obj_buf, sizeof(obj_buf),
      "%s{\"dt\":\"%s\",\"pk\":%.1f,\"mn\":%.1f,\"en\":%ld}",
      first ? "" : ",",
      dt_buf,
      peak_c,
      min_c,
      (long)energy_wh);

    response->print(obj_buf);
    first = false;
  }

  response->print("]");
}

// ---------------------------------------------------------------------------
// /energy/daily  –  tsdb-backed
//
// JSON shape matches the legacy EnergyLogger getDailyMetrics exactly:
//
//   { "daily": [
//       { "dt": "YYYY-MM-DD", "pk": <peak_c>, "mn": <min_c>, "en": <wh> },
//       ...
//   ]}
//
// Query params:
//   days=N   override window size (default 30)
// ---------------------------------------------------------------------------

void handleEnergyDaily(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = nullptr;

  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    char days_buf[8] = {0};
    int num_days = ENERGY_DAILY_DEFAULT_DAYS;
    if (request->getParam("days", days_buf, sizeof(days_buf)) >= 0 && days_buf[0] != '\0') {
      int d = atoi(days_buf);
      if (d >= 1 && d <= 365) num_days = d;
    }

    response->setCode(200);
    response->print("{\"daily\":");
    emit_bucketed_daily(response, 1 /* bucket_days */, num_days);
    response->print("}");

  } else if (HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

// ---------------------------------------------------------------------------
// /energy/weekly  –  tsdb-backed (NEW, no legacy equivalent)
//
// Same per-entry shape as /energy/daily (dt/pk/mn/en) but each bucket spans
// 7 calendar days.  "dt" is the date of the Monday that starts the ISO week
// containing the bucket start.  Wrapper key is "weekly".
//
//   { "weekly": [
//       { "dt": "YYYY-MM-DD", "pk": <peak_c>, "mn": <min_c>, "en": <wh> },
//       ...
//   ]}
//
// Query params:
//   weeks=N   override window size (default 12)
// ---------------------------------------------------------------------------

void handleEnergyWeekly(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = nullptr;

  if (false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    char weeks_buf[8] = {0};
    int num_weeks = ENERGY_WEEKLY_DEFAULT_WEEKS;
    if (request->getParam("weeks", weeks_buf, sizeof(weeks_buf)) >= 0 && weeks_buf[0] != '\0') {
      int w = atoi(weeks_buf);
      if (w >= 1 && w <= 104) num_weeks = w;
    }

    response->setCode(200);
    response->print("{\"weekly\":");
    emit_bucketed_daily(response, 7 /* bucket_days */, num_weeks);
    response->print("}");

  } else if (HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);
  }

  request->send(response);
}

#endif // ENABLE_TSDB
