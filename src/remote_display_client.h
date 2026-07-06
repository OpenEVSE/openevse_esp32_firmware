// src/remote_display_client.h — HTTP client feeding the LVGL TFT screens from a
// remote OpenEVSE station. Used by the openevse_remote_display build, where the
// display hardware has no RAPI controller attached: instead of EvseManager, the
// LcdTask (lcd_lvgl.cpp) reads its ChargeScreenData/StandbyScreenData snapshot
// from here. Polls http://<remote_display_host>/status (the station's HTTP API)
// and caches the fields the screens need. Selected by ENABLE_REMOTE_DISPLAY_CLIENT.
#ifndef _OPENEVSE_REMOTE_DISPLAY_CLIENT_H
#define _OPENEVSE_REMOTE_DISPLAY_CLIENT_H

#ifdef ENABLE_REMOTE_DISPLAY_CLIENT

#include <Arduino.h>
#include <MicroTasks.h>
#include <MongooseHttpClient.h>

// Poll cadence and freshness. Data older than the valid window renders as
// "no data" (state RAPI_EVSE_STATE unknown -> "--" on screen).
#ifndef REMOTE_DISPLAY_POLL_MS
#define REMOTE_DISPLAY_POLL_MS        5000
#endif
#ifndef REMOTE_DISPLAY_HTTP_TIMEOUT_MS
#define REMOTE_DISPLAY_HTTP_TIMEOUT_MS 10000
#endif
#ifndef REMOTE_DISPLAY_DATA_VALID_MS
#define REMOTE_DISPLAY_DATA_VALID_MS  30000
#endif

// getEvseState() when nothing (fresh) has been received: not a real
// OPENEVSE_STATE_* value, so state_word() renders "--".
#define REMOTE_DISPLAY_STATE_UNKNOWN  0xf0

class RemoteDisplayClient : public MicroTasks::Task
{
  private:
    MongooseHttpClient _client;

    // In-flight request guard: never stack a second GET on a slow/dead station.
    bool     _requestPending = false;
    uint32_t _requestStarted = 0;

    // Host actually connected to: remote_display_host, with a trailing ".local"
    // resolved via mDNS (Mongoose's resolver only does unicast DNS).
    String   _resolvedHost;
    String   _resolvedFrom;      // the remote_display_host value _resolvedHost came from
    uint32_t _resolvedAt = 0;

    // Snapshot of the station's /status, normalised to the same units the
    // EvseManager getters return (A, V, W, Wh, kWh, degC, seconds).
    uint32_t _lastRx = 0;        // millis() of last good parse; 0 = never
    uint8_t  _state = REMOTE_DISPLAY_STATE_UNKNOWN;
    bool     _vehicle = false;
    double   _amps = 0;
    double   _voltage = 0;
    double   _power = 0;         // W
    long     _pilot = 0;         // A
    uint32_t _elapsed = 0;       // s, as reported at _lastRx
    double   _session_wh = 0;
    double   _total_kwh = 0;
    double   _day_kwh = 0;
    bool     _temp_valid = false;
    double   _temp = 0;          // degC

    bool resolveHost(String &host);
    void parseStatus(const char *body, size_t len);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    RemoteDisplayClient();
    void begin();

    // True while the snapshot below is fresh enough to trust.
    bool isDataValid() {
      return _lastRx != 0 && (uint32_t)(millis() - _lastRx) < REMOTE_DISPLAY_DATA_VALID_MS;
    }

    // Accessors mirror the EvseManager getters LcdTask uses, but return
    // safe "no data" values when stale so the screens degrade to "--".
    uint8_t  getEvseState()         { return isDataValid() ? _state : REMOTE_DISPLAY_STATE_UNKNOWN; }
    bool     isVehicleConnected()   { return isDataValid() && _vehicle; }
    double   getAmps()              { return isDataValid() ? _amps : 0; }
    double   getVoltage()           { return isDataValid() ? _voltage : 0; }
    double   getPower()             { return isDataValid() ? _power : 0; }
    long     getChargeCurrent()     { return isDataValid() ? _pilot : 0; }
    uint32_t getSessionElapsed();   // extrapolated between polls while charging
    double   getSessionEnergy()     { return isDataValid() ? _session_wh : 0; }
    double   getTotalEnergy()       { return isDataValid() ? _total_kwh : 0; }
    double   getTotalDay()          { return isDataValid() ? _day_kwh : 0; }
    bool     isTemperatureValid()   { return isDataValid() && _temp_valid; }
    double   getTemperature()       { return _temp; }
};

extern RemoteDisplayClient remoteDisplay;

#endif // ENABLE_REMOTE_DISPLAY_CLIENT
#endif // _OPENEVSE_REMOTE_DISPLAY_CLIENT_H
