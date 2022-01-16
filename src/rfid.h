/*
 * Author: Oliver Norin
 *         Matthias Akstaller
 */

#include <ArduinoJson.h>
#include <MicroTasks.h>
#include <Wire.h>

#include "evse_man.h"

#define  SCAN_FREQ            1000

#define AUTHENTICATION_TIMEOUT     30000UL

#define MAXIMUM_UNRESPONSIVE_TIME  60000UL //after this period the pn532 is considered offline
#define AUTO_REFRESH_CONNECTION         30 //after this number of polls, the connection to the PN532 will be refreshed

class RfidTask : public MicroTasks::Task {
    private:
        EvseManager *_evse;
        uint8_t waitingForTag = 0;
        String waitingForTagResult {'\0'};
        unsigned long stopWaiting = 0;
        boolean cardFound = false;
        void scanCard(String& uid);
        String authenticatedTag {'\0'};
        ulong authentication_timestamp {0};
        boolean isAuthenticated();
        bool authenticationTimeoutExpired();
        void resetAuthentication();
        void setAuthentication(String& tag);

        /*
         * SAE J1772 state
         */
        bool vehicleConnected = false;
        
        void updateEvseClaim();

        std::function<bool(const String& idTag)> *onCardScanned {nullptr};

        /*
         * NXP PN532
         */
        TwoWire *i2c;

        enum class PN532_DeviceStatus {
            ACTIVE,
            NOT_ACTIVE,
            FAILED
        };
        PN532_DeviceStatus pn532_status = PN532_DeviceStatus::NOT_ACTIVE;

        boolean pn532_hasContact = false;
        bool pn532_listen = false;
        void pn532_initialize();
        void pn532_poll();
        void pn532_read();

        ulong pn532_lastResponse = 0;
        uint pn532_pollCount = 0;

    protected:
        void setup();
        unsigned long loop(MicroTasks::WakeReason reason);

    public:
        RfidTask();
        void begin(EvseManager &evse, TwoWire& wire);
        void waitForTag(uint8_t seconds);
        DynamicJsonDocument rfidPoll();

        String getAuthenticatedTag();
        bool communicationFails();

        void setOnCardScanned(std::function<bool(const String& idTag)> *onCardScanned);
};

extern RfidTask rfid;
