/*
 * Author: OpenEVSE contributors
 */

#if defined(ENABLE_RC522)

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_NFCREADER)
#undef ENABLE_DEBUG
#endif

#include "rc522.h"
#include "app_config.h"
#include "debug.h"
#include "lcd.h"

#define SCAN_DELAY 1000
#define POLL_DELAY 50

// After this period without a successful SPI exchange the reader is offline.
#define MAXIMUM_UNRESPONSIVE_TIME 60000UL

// MFRC522 firmware version bytes (see NXP MFRC522 datasheet, version register).
#define MFRC522_VERSION_0x91 0x91
#define MFRC522_VERSION_0x92 0x92

RC522Reader::RC522Reader()
    : _mfrc522(RC522_SS_PIN, RC522_RST_PIN),
      _spi(&SPI),
      _ss_pin(RC522_SS_PIN),
      _rst_pin(RC522_RST_PIN),
      MicroTasks::Task() {
}

RC522Reader::RC522Reader(uint8_t ss_pin, uint8_t rst_pin, SPIClass *spi)
    : _mfrc522(ss_pin, rst_pin),
      _spi(spi ? spi : &SPI),
      _ss_pin(ss_pin),
      _rst_pin(rst_pin),
      MicroTasks::Task() {
}

void RC522Reader::begin() {
    // SPI presence probe at boot so the UI can report reader connected/disconnected
    // even when RFID is disabled in config.
    probeReader();
    MicroTask.startTask(this);
}

bool RC522Reader::probeReader() {
    if (_initialized && !_failure) {
        // Already communicating — avoid resetting the chip mid-session.
        return true;
    }

    // Re-run the version-register read and refresh the cached result. The boot-time
    // one-shot can false-negative (reader still powering up) and would otherwise
    // report absent until reboot. readerPresent() returns this cached flag; probeReader()
    // actively refreshes it over SPI.
#if defined(RC522_SPI_SCK) && defined(RC522_SPI_MISO) && defined(RC522_SPI_MOSI)
    _spi->begin(RC522_SPI_SCK, RC522_SPI_MISO, RC522_SPI_MOSI, _ss_pin);
#else
    _spi->begin();
#endif

    _mfrc522.PCD_Init(_ss_pin, _rst_pin);
    byte version = _mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
    _present = (version == MFRC522_VERSION_0x91 || version == MFRC522_VERSION_0x92);

    if (_present) {
        DBUGF("[rfid] RC522 probe OK, version=0x%02X", version);
    } else {
        DBUGF("[rfid] RC522 probe failed, version=0x%02X", version);
    }

    return _present;
}

bool RC522Reader::readerPresent() {
    // Cached probe result from boot or the last probeReader() call, or currently
    // initialized and responding while RFID is active.
    return _present || (_initialized && !_failure);
}

bool RC522Reader::readerFailure() {
    return config_rfid_enabled() && _failure;
}

void RC522Reader::initialize() {
#if defined(RC522_SPI_SCK) && defined(RC522_SPI_MISO) && defined(RC522_SPI_MOSI)
    _spi->begin(RC522_SPI_SCK, RC522_SPI_MISO, RC522_SPI_MOSI, _ss_pin);
#else
    _spi->begin();
#endif

    // PCD_Init drives RST, configures SPI chip-select, and verifies the MFRC522
    // version register over the shared SPI bus.
    _mfrc522.PCD_Init(_ss_pin, _rst_pin);
    _mfrc522.PCD_AntennaOn();

    byte version = _mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
    if (version == MFRC522_VERSION_0x91 || version == MFRC522_VERSION_0x92) {
        DBUGLN(F("[rfid] connection to RC522 active"));
        _initialized = true;
        _failure = false;
        _present = true;
        _last_response = millis();
    } else {
        DBUGF("[rfid] RC522 init failed, version=0x%02X", version);
        _initialized = false;
        _present = false;
    }
}

String RC522Reader::uidToString(const MFRC522::Uid &uid) {
    // Match PN532 UID formatting: lowercase hex pairs with no separator.
    String out = String('\0');
    out.reserve(2 * uid.size);

    for (byte i = 0; i < uid.size; i++) {
        uint8_t b = uid.uidByte[i];
        uint8_t hi = b / 0x10;
        uint8_t lo = b % 0x10;
        out += (char)(hi <= 9 ? hi + '0' : hi % 10 + 'a');
        out += (char)(lo <= 9 ? lo + '0' : lo % 10 + 'a');
    }

    return out;
}

void RC522Reader::poll() {
    if (!_initialized) {
        return;
    }

    // PICC_IsNewCardPresent() performs a short SPI exchange to detect a tag in
    // the RF field. When the tag leaves, clear contact so the same card can be
    // presented again later.
    if (!_mfrc522.PICC_IsNewCardPresent()) {
        _has_contact = false;
        _last_uid.clear();
        return;
    }

    if (!_mfrc522.PICC_ReadCardSerial()) {
        return;
    }

    _last_response = millis();

    String uid = uidToString(_mfrc522.uid);

    if (_has_contact && uid == _last_uid) {
        // Valid card already reported while still in the field — nothing to do.
        return;
    }

    DBUG(F("[rfid] found card! uid = "));
    DBUG(uid);
    DBUGLN(F(" end"));

    _has_contact = true;
    _last_uid = uid;
    onCardDetected(uid);

    // Halt the PICC so removal/re-tap can be detected on the next poll cycle.
    _mfrc522.PICC_HaltA();
    _mfrc522.PCD_StopCrypto1();
}

unsigned long RC522Reader::loop(MicroTasks::WakeReason reason) {
    (void)reason;

    // Allow scanning when a scheduler timer window requires RFID even if the
    // global rfid_enabled setting is off.
    if (!config_rfid_enabled() && !_timer_scanning) {
        _initialized = false;
        _has_contact = false;
        return SCAN_DELAY;
    }

    if (_initialized && millis() - _last_response > MAXIMUM_UNRESPONSIVE_TIME) {
        DBUGLN(F("[rfid] connection to RC522 lost"));
        lcd.display("RFID chip not found", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
        _failure = true;
        _initialized = false;
    }

    if (!_initialized || _failure) {
        initialize();
        return POLL_DELAY;
    }

    poll();
    return POLL_DELAY;
}

RC522Reader rc522;

#endif
