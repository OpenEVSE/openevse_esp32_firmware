/*
 * Author: Oliver Norin
 *         Matthias Akstaller
 */

#ifndef RFIDTASK_H
#define RFIDTASK_H

#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"

class RfidReader {
public:
    virtual ~RfidReader() = default;
    virtual void setOnCardDetected(std::function<void(String&)> onCardDet) = 0;
    virtual bool readerFailure() = 0;
};

class RfidReaderNullDevice : public RfidReader {
public:
    void setOnCardDetected(std::function<void(String&)> onCardDet) override {}
    bool readerFailure() override;
};

class RfidTask : public MicroTasks::Task {
    private:
        EvseManager *_evse;
        RfidReader *_rfid;
        bool waitingForTag = false;
        unsigned long waitingBegin = 0;
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

    protected:
        void setup();
        unsigned long loop(MicroTasks::WakeReason reason);

    public:
        RfidTask();
        void begin(EvseManager &evse, RfidReader &rfid);
        void waitForTag();

        String getAuthenticatedTag();
        bool communicationFails();

        void setOnCardScanned(std::function<bool(const String& idTag)> *onCardScanned);
};

extern RfidReaderNullDevice rfidNullDevice;

extern RfidTask rfid;

#endif
