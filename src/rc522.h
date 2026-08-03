/*
 * Author: OpenEVSE contributors
 */

#if defined(ENABLE_RC522)

#ifndef RC522_H
#define RC522_H

#include "rfid.h"
#include <MFRC522.h>
#include <MicroTasks.h>
#include <SPI.h>

// Default chip-select (SS/SDA) and reset pins — override via build flags
// (e.g. -D RC522_SS_PIN=5 -D RC522_RST_PIN=22) for other boards/wiring.
#ifndef RC522_SS_PIN
#define RC522_SS_PIN 5
#endif

#ifndef RC522_RST_PIN
#define RC522_RST_PIN 22
#endif

class RC522Reader : public RfidReader, public MicroTasks::Task {
private:
    // MFRC522 talks over the platform default SPI peripheral. The optional
    // SPIClass* constructor argument only selects which bus gets SPI.begin()
    // pin remapping; MFRC522 library transfers always use the global SPI object.
    MFRC522 _mfrc522;
    SPIClass *_spi;
    uint8_t _ss_pin;
    uint8_t _rst_pin;

    std::function<void(String &uid)> onCardDetected = [] (String &) {};

    bool _initialized = false;
    bool _present = false;        // last probe result: reader seen on SPI bus
    bool _failure = false;        // reader was active but stopped responding
    bool _timer_scanning = false; // scheduler window wants scanning while rfid off
    bool _has_contact = false;    // card still in field — suppress repeat callbacks
    String _last_uid;

    ulong _last_response = 0;

    void initialize();
    void poll();
    static String uidToString(const MFRC522::Uid &uid);

protected:
    void setup() { }
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    RC522Reader();
    explicit RC522Reader(uint8_t ss_pin, uint8_t rst_pin, SPIClass *spi = &SPI);
    void begin();

    void setOnCardDetected(std::function<void(String &)> onCardDet) override { onCardDetected = onCardDet; }
    bool readerFailure() override;
    bool readerPresent() override;
    bool probeReader() override;
    void setTimerScanning(bool active) override {
        _timer_scanning = active;
        if (active) {
            MicroTask.wakeTask(this);
        }
    }
};

extern RC522Reader rc522;

#endif
#endif
