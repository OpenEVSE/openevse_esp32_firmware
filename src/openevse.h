#ifndef __OPENEVSE_H
#define __OPENEVSE_H

#include <functional>

#include "RapiSender.h"

#define OPENEVSE_STATE_INVALID               -1

#define OPENEVSE_STATE_STARTING               0
#define OPENEVSE_STATE_NOT_CONNECTED          1
#define OPENEVSE_STATE_CONNECTED              2
#define OPENEVSE_STATE_CHARGING               3
#define OPENEVSE_STATE_VENT_REQUIRED          4
#define OPENEVSE_STATE_DIODE_CHECK_FAILED     5
#define OPENEVSE_STATE_GFI_FAULT              6
#define OPENEVSE_STATE_NO_EARTH_GROUND        7
#define OPENEVSE_STATE_STUCK_RELAY            8
#define OPENEVSE_STATE_GFI_SELF_TEST_FAILED   9
#define OPENEVSE_STATE_OVER_TEMPERATURE      10
#define OPENEVSE_STATE_OVER_CURRENT          11
#define OPENEVSE_STATE_SLEEPING             254
#define OPENEVSE_STATE_DISABLED             255

#define OPENEVSE_POST_CODE_OK                     0
#define OPENEVSE_POST_CODE_NO_EARTH_GROUND        7
#define OPENEVSE_POST_CODE_STUCK_RELAY            8
#define OPENEVSE_POST_CODE_GFI_SELF_TEST_FAILED   9

// J1772EVSEController volatile m_wVFlags bits - not saved to EEPROM
#define OPENEVSE_VFLAG_AUTOSVCLVL_SKIPPED   0x0001 // auto svc level test skipped during post
#define OPENEVSE_VFLAG_HARD_FAULT           0x0002 // in non-autoresettable fault
#define OPENEVSE_VFLAG_LIMIT_SLEEP          0x0004 // currently sleeping after reaching time/charge limit
#define OPENEVSE_VFLAG_AUTH_LOCKED          0x0008 // locked pending authentication
#define OPENEVSE_VFLAG_AMMETER_CAL          0x0010 // ammeter calibration mode
#define OPENEVSE_VFLAG_NOGND_TRIPPED        0x0020 // no ground has tripped at least once
#define OPENEVSE_VFLAG_CHARGING_ON          0x0040 // charging relay is closed
#define OPENEVSE_VFLAG_GFI_TRIPPED          0x0080 // gfi has tripped at least once since boot
#define OPENEVSE_VFLAG_EV_CONNECTED         0x0100 // EV connected - valid only when pilot not N12
#define OPENEVSE_VFLAG_SESSION_ENDED        0x0200 // used for charging session time calc
#define OPENEVSE_VFLAG_EV_CONNECTED_PREV    0x0400 // prev EV connected flag
#define OPENEVSE_VFLAG_UI_IN_MENU           0x0800 // onboard UI currently in a menu
#if defined(AUTH_LOCK) && (AUTH_LOCK != 0)
#define OPENEVSE_VFLAG_DEFAULT              ECVF_AUTH_LOCKED|ECVF_SESSION_ENDED
#else
#define OPENEVSE_VFLAG_DEFAULT              ECVF_SESSION_ENDED
#endif

#define OPENEVSE_WIFI_MODE_AP 0
#define OPENEVSE_WIFI_MODE_CLIENT 1
#define OPENEVSE_WIFI_MODE_AP_DEFAULT 2

#define OPENEVSE_ENCODE_VERSION(a,b,c) (((a)*1000) + ((b)*100) + (c))

#define OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION  OPENEVSE_ENCODE_VERSION(5,0,0)


typedef std::function<void(uint8_t post_code, const char *firmware)> OpenEVSEBootCallback;
typedef std::function<void(uint8_t evse_state, uint8_t pilot_state, uint32_t current_capacity, uint32_t vflags)> OpenEVSEStateCallback;
typedef std::function<void(uint8_t event)> OpenEVSEWiFiCallback;

class OpenEVSEClass
{
  private:
    RapiSender *_sender;

    bool _connected;
    uint32_t _protocol;

    OpenEVSEBootCallback _boot;
    OpenEVSEStateCallback _state;
    OpenEVSEWiFiCallback _wifi;

    void onEvent();

  public:
    OpenEVSEClass();
    ~OpenEVSEClass() { }

    void begin(RapiSender &sender, std::function<void(bool connected)> callback);

    void getStatus(std::function<void(int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)> callback);
    void getVersion(std::function<void(int ret, const char *firmware, const char *protocol)> callback);

    bool isConnected() {
      return _connected;
    }

    void onBoot(OpenEVSEBootCallback callback) {
      _boot = callback;
    }
    void onState(OpenEVSEStateCallback callback) {
      _state = callback;
    }
    void onWiFi(OpenEVSEWiFiCallback callback) {
      _wifi = callback;
    }
};

extern OpenEVSEClass OpenEVSE;

#endif
