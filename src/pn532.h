/*
 * Author: Matthias Akstaller
 */

#if defined(ENABLE_PN532)

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
    bool listenAck = false;
    bool listen = false;
    bool _reader_present = false;   // probed once on the I2C bus at boot
    bool _timer_scanning = false;   // true while a scheduler timer window needs scanning

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
    bool readerPresent() override;
    void setTimerScanning(bool active) override { _timer_scanning = active; }
};

extern PN532 pn532;

#endif
#endif
