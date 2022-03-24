/*
 * Author: Matthias Akstaller
 */

#ifndef PN532_H
#define PN532_H

#include "rfid.h"
#include <MicroTasks.h>

class PN532 : public RfidReader, public MicroTasks::Task {
private:
    std::function<void(String &uid)> onCardDetected = [] (String&) {};

    enum class DeviceStatus {
        ACTIVE,
        NOT_ACTIVE,
        FAILED
    };

    DeviceStatus status = DeviceStatus::NOT_ACTIVE;
    boolean hasContact = false;
    bool listen = false;

    void initialize();
    void poll();
    void read();

    ulong lastResponse = 0;
    uint pollCount = 0;
protected:
    void setup() { }
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    PN532();
    void begin();

    void setOnCardDetected(std::function<void(String&)> onCardDet) override {onCardDetected = onCardDet;}
    bool readerFailure() override;
};

extern PN532 pn532;

#endif
