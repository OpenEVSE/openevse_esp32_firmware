/*
 * Author: Ammar Panjwani
 * Adapted from PN532 to work with Access Cards 
 * Weigand Format: https://www.pagemac.com/projects/rfid/hid_data_formats
 * 
 * If you have questions you can reach me at ammar.panj@gmail.com or 832-654-1839
 */

#if defined(ENABLE_AccessCard)

#ifndef AccessCard_H
#define AccessCard_H

#include "rfid.h"
#include <MicroTasks.h>

class AccessCard : public RfidReader, public MicroTasks::Task {
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

    void initialize();
    void poll();
    void read();
    void readFromBridge();

    ulong lastResponse = 0;
    uint pollCount = 0;
protected:
    void setup() { }
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    AccessCard();
    void begin();

    void setOnCardDetected(std::function<void(String&)> onCardDet) override {onCardDetected = onCardDet;}
    bool readerFailure() override;
};

extern AccessCard accessCard;

#endif
#endif
