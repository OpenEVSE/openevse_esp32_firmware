#include <DFRobot_PN532.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"

#define  SCAN_FREQ            200
#define  RFID_BLOCK_SIZE      16
#define  PN532_IRQ            (2)
#define  PN532_INTERRUPT      (1)
#define  PN532_POLLING        (0)

#define AUTHENTICATION_TIMEOUT 30000UL

class RfidTask : public MicroTasks::Task {
    private:
        EvseManager *_evse;
        DFRobot_PN532_IIC nfc;

        enum class NfcDeviceStatus {
            ACTIVE,
            NOT_ACTIVE
        };
        NfcDeviceStatus status = NfcDeviceStatus::NOT_ACTIVE;
        boolean hasContact = false;
        uint8_t waitingForTag = 0;
        unsigned long stopWaiting = 0;
        boolean cardFound = false;
        String authenticatedTag {'\0'};
        ulong authentication_timestamp {0};

        /*
         * SAE J1772 state
         */
        bool vehicleConnected = false;
        
        void updateEvseClaim();

        std::function<bool(const String& idTag)> *onCardScanned {nullptr};

    protected:
        void setup();
        unsigned long loop(MicroTasks::WakeReason reason);
        struct card NFCcard;
        String getUidHex(card NFCcard);
        void scanCard();

    public:
        RfidTask();
        void begin(EvseManager &evse);
        void waitForTag(uint8_t seconds);
        DynamicJsonDocument rfidPoll();
        boolean wakeup();

        bool authenticationTimeoutExpired();
        boolean isAuthenticated();
        String getAuthenticatedTag();
        void resetAuthentication();
        void setAuthentication(String& tag);

        void setOnCardScanned(std::function<bool(const String& idTag)> *onCardScanned);
};

extern RfidTask rfid;
