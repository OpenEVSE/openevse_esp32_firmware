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
#define POLL_DELAY 250
#define RETRY_DELAY 5000UL
#define MAX_INVALID_VERSION_READS 3

// MFRC522 firmware version bytes (see NXP MFRC522 datasheet, version register).
// Firmware version 0.0 is supported by the MFRC522 1.4.12 library.
#define MFRC522_VERSION_0x90 0x90
#define MFRC522_VERSION_0x91 0x91
#define MFRC522_VERSION_0x92 0x92
// FM17522 and other MFRC522-compatible clones often report 0x88.
#define MFRC522_VERSION_0x88 0x88

static bool isSupportedVersion(byte version) {
    return version == MFRC522_VERSION_0x90 || version == MFRC522_VERSION_0x91 ||
           version == MFRC522_VERSION_0x92 || version == MFRC522_VERSION_0x88;
}

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
    if (_initialized) {
        // Avoid resetting a reader during an active RFID session.
        return _present;
    }

    // One initialization path for both the boot probe and scanning.
    if (_next_retry_at != 0 && (long)(millis() - _next_retry_at) < 0) {
        return _present;
    }
    initialize();

    if (_initialized && !config_rfid_enabled() && !_timer_scanning) {
        // Presence can be checked at boot while scanning remains disabled.
        _mfrc522.PCD_AntennaOff();
        _initialized = false;
    }
    return _present;
}

bool RC522Reader::readerPresent() {
    // Cached probe result from boot or the last probeReader() call, or currently
    // initialized and responding while RFID is active.
    return _present;
}

bool RC522Reader::readerFailure() {
    return (config_rfid_enabled() || _timer_scanning) && _failure;
}

void RC522Reader::initialize() {
#if defined(RC522_SPI_SCK) && defined(RC522_SPI_MISO) && defined(RC522_SPI_MOSI)
    _spi->begin(RC522_SPI_SCK, RC522_SPI_MISO, RC522_SPI_MOSI, _ss_pin);
#else
    _spi->begin();
#endif

    // PCD_Init may block for 50+ ms; retry only after RETRY_DELAY.
    _mfrc522.PCD_Init(_ss_pin, _rst_pin);
    byte version = _mfrc522.PCD_ReadRegister(MFRC522::VersionReg);

    _present = isSupportedVersion(version);
    _initialized = _present;
    _failure = !_present;
    _invalid_version_reads = 0;
    _has_contact = false;
    _last_uid.clear();

    if (_initialized) {
        _mfrc522.PCD_AntennaOn();
        _last_response = millis();
        _next_retry_at = 0;
        DBUGF("[rfid] RC522 connected, version=0x%02X", version);
    } else {
        _next_retry_at = millis() + RETRY_DELAY;
        DBUGF("[rfid] RC522 init failed, version=0x%02X", version);
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

    // PICC_IsNewCardPresent() returns false for both no-card and SPI timeout.
    // Verify the SPI VersionReg response independently to detect disconnection.
    byte version = _mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
    if (!isSupportedVersion(version)) {
        if (++_invalid_version_reads >= MAX_INVALID_VERSION_READS) {
            DBUGF("[rfid] RC522 disconnected, version=0x%02X", version);
            lcd.display("RFID chip not found", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
            _failure = true;
            _initialized = false;
            _present = false;
            _has_contact = false;
            _last_uid.clear();
            _next_retry_at = millis() + RETRY_DELAY;
        }
        return;
    }

    _invalid_version_reads = 0;
    _last_response = millis(); // Proven SPI register response, not absent card.
    _present = true;

    if (!_mfrc522.PICC_IsNewCardPresent()) {
        _has_contact = false;
        _last_uid.clear();
        return;
    }

    if (!_mfrc522.PICC_ReadCardSerial()) {
        _mfrc522.PICC_HaltA();
        _mfrc522.PCD_StopCrypto1();
        return;
    }

    String uid = uidToString(_mfrc522.uid);
    if (_has_contact && uid == _last_uid) {
        return;
    }

    DBUG(F("[rfid] found card! uid = "));
    DBUG(uid);
    DBUGLN(F(" end"));

    _has_contact = true;
    _last_uid = uid;
    onCardDetected(uid);

    _mfrc522.PICC_HaltA();
    _mfrc522.PCD_StopCrypto1();
}

unsigned long RC522Reader::loop(MicroTasks::WakeReason reason) {
    (void)reason;

    // A scheduled timer may require scanning with global RFID disabled.
    if (!config_rfid_enabled() && !_timer_scanning) {
        if (_initialized) {
            _mfrc522.PCD_AntennaOff();
        }
        _initialized = false;
        _has_contact = false;
        _last_uid.clear();
        _next_retry_at = 0; // An enable event gets an immediate retry.
        return SCAN_DELAY;
    }

    if (!_initialized) {
        // No repeated reset stalls in the cooperative main loop.
        if (_next_retry_at == 0 || (long)(millis() - _next_retry_at) >= 0) {
            initialize();
        }
        return SCAN_DELAY;
    }

    // Reduce worst-case blocking from no-card polls (~25 ms per call).
    poll();
    return POLL_DELAY;
}

RC522Reader rc522;

#endif
