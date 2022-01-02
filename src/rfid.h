#include <DFRobot_PN532.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "scheduler.h"

#define  SCAN_FREQ            200
#define  RFID_BLOCK_SIZE      16
#define  PN532_IRQ            (2)
#define  PN532_INTERRUPT      (1)
#define  PN532_POLLING        (0)

#define RFID_STATUS_NOT_ENABLED 0
#define RFID_STATUS_NOT_FOUND 1
#define RFID_STATUS_ACTIVE 2

class RfidTask : public MicroTasks::Task {
    private:
        EvseManager *_evse;
        Scheduler *_scheduler;
        MicroTasks::EventListener _evseStateEvent;
        long state;
        DFRobot_PN532_IIC nfc; 
        uint8_t status = RFID_STATUS_NOT_ENABLED;
        boolean hasContact = false;
        uint8_t waitingForTag = 0;
        unsigned long stopWaiting = 0;
        boolean cardFound = false;
        String authenticatedTag = "";

    protected:
        void setup();
        unsigned long loop(MicroTasks::WakeReason reason);
        struct card NFCcard;
        String getUidHex(card NFCcard);
        void scanCard();
        void startTimer(uint8_t seconds);
        void abortTimer();

    public:
        RfidTask();
        void begin(EvseManager &evse, Scheduler &scheduler);
        uint8_t getStatus();
        void waitForTag(uint8_t seconds);
        DynamicJsonDocument rfidPoll();
        boolean wakeup();
        boolean isAuthenticated();
        String getAuthenticatedTag();
        void resetAuthentication();
};


extern RfidTask rfid;
