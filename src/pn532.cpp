/*
 * Author: Matthias Akstaller
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_NFCREADER)
#undef ENABLE_DEBUG
#endif

#include "pn532.h"
#include <Wire.h>
#include "app_config.h"
#include "debug.h"
#include "lcd.h"

#ifndef I2C_SDA
#define I2C_SDA 21
#endif

#ifndef I2C_SCL
#define I2C_SCL 22
#endif

#define  SCAN_FREQ            1000

#define MAXIMUM_UNRESPONSIVE_TIME  60000UL //after this period the pn532 is considered offline
#define AUTO_REFRESH_CONNECTION         30 //after this number of polls, the connection to the PN532 will be refreshed


/*
 * Documentation of the I2C messages:
 * NXP PN532 manual, https://www.nxp.com/docs/en/user-guide/141520.pdf
 * (accessed: 2022-01-14)
 */

#define PN532_I2C_ADDRESS  (0x48 >> 1)
#define PN532_RDY 0x01
#define PN532_FRAME_HEADER_LEN 4
#define PN532_FRAME_IDENTIFIER_LEN 1
#define PN532_CMD_SAMCONFIGURATION_RESPONSE 0x15
#define PN532_CMD_INAUTOPOLL_RESPONSE 0x61

#define I2C_READ_BUFFSIZE 35 //might increase later

PN532::PN532() : MicroTasks::Task() {
    
}

void PN532::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    MicroTask.startTask(this);
}

unsigned long PN532::loop(MicroTasks::WakeReason reason){

    read();

    if (!config_rfid_enabled()) {
        status = DeviceStatus::NOT_ACTIVE;
        return SCAN_FREQ;
    }

    if (status == DeviceStatus::ACTIVE && millis() - lastResponse > MAXIMUM_UNRESPONSIVE_TIME) {
        DBUGLN(F("[rfid] connection to PN532 lost"));
        lcd.display("RFID chip not found", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
        status = DeviceStatus::FAILED;
    }
    
    if (status != DeviceStatus::ACTIVE) {
        initialize();
        return SCAN_FREQ;
    }

    if (pollCount >= AUTO_REFRESH_CONNECTION) {
        initialize();
        pollCount = 0;
    } else {
        poll();
        pollCount++;
    }

    return SCAN_FREQ;
}

bool PN532::readerFailure() {
    return config_rfid_enabled() && status == DeviceStatus::FAILED;
}

void PN532::initialize() {

    /*
     * Command hardcoded according to NXP manual, page 89
     */
    uint8_t SAMConfiguration [] = {0x00, 0xFF, 0x05, 0xFB, 
                                   0xD4, 0x14, 0x01, 0x00, 0x00,
                                   0x17};
    Wire.beginTransmission(PN532_I2C_ADDRESS);
    Wire.write(SAMConfiguration, sizeof(SAMConfiguration) / sizeof(*SAMConfiguration));
    Wire.endTransmission();

    listen = true;
    //"flush" ACK
    delay(30);
    read();
}

void PN532::poll() {
    /*
     * Command hardcoded according to NXP manual, page 144
     */
    uint8_t InAutoPoll [] = {0x00, 0xFF, 0x06, 0xFA, 
                             0xD4, 0x60, 0x01, 0x01, 0x10, 0x20,
                             0x9A};
    Wire.beginTransmission(PN532_I2C_ADDRESS);
    Wire.write(InAutoPoll, sizeof(InAutoPoll) / sizeof(*InAutoPoll));
    Wire.endTransmission();

    listen = true;
    //"flush" ACK
    delay(30);
    read();
}

void PN532::read() {
    if (!listen)
        return;

    Wire.requestFrom(PN532_I2C_ADDRESS, I2C_READ_BUFFSIZE + 1, 1);
    if (Wire.read() != PN532_RDY) {
        //no msg available
        return;
    }

    uint8_t response [I2C_READ_BUFFSIZE] = {0};
    uint8_t frame_len = 0;
    while (Wire.available() && frame_len < I2C_READ_BUFFSIZE) {
        response[frame_len] = Wire.read();
        frame_len++;
    }
    Wire.flush();

    /*
     * For frame layout, see NXP manual, page 28
     */

    if (frame_len < PN532_FRAME_HEADER_LEN) {
        //frame corrupt
        listen = false;
        return;
    }

    uint8_t *header = nullptr;
    uint8_t header_offs = 0;
    while (header_offs < frame_len - PN532_FRAME_HEADER_LEN) {
        if (response[header_offs] == 0x00 && response[header_offs + 1] == 0xFF) { //find frame preamble
            header = response + header_offs; //header preamble found
            break;
        }
        header_offs++;
    }

    if (!header) {
        //invalid packet
        listen = false;
        return;
    }

    //check for ack frame
    if (header[2] == 0x00 && header[3] == 0xFF) {
        //ack detected
        return;
    }

    if (header[2] == 0x00 || header[2] == 0xFF) {
        //ignore frame type
        listen = false;
        return;
    }

    uint8_t data_len = header[2];
    uint8_t data_lcs = header[3];
    data_lcs += data_len;

    if (data_lcs) {
        //checksum wrong
        listen = false;
        return;
    }

    if (data_len <= PN532_FRAME_IDENTIFIER_LEN) {
        //ingore frame type
        listen = false;
        return;
    }

    //frame layout: <header><frame_identifier><data><data_checksum>
    uint8_t *frame_identifier = header + PN532_FRAME_HEADER_LEN;
    uint8_t *data = frame_identifier + PN532_FRAME_IDENTIFIER_LEN;
    uint8_t *data_dcs = frame_identifier + data_len;

    if (data_dcs - response >= frame_len) {
        DBUGLN(F("[rfid] Did not read sufficient bytes. Abort"));
        listen = false;
        return;
    }

    uint8_t data_checksum = *data_dcs;
    for (uint8_t *i = frame_identifier; i != data_dcs; i++) {
        data_checksum += *i;
    }

    if (data_checksum) {
        //wrong checksum
        listen = false;
        return;
    }

    if (frame_identifier[0] != 0xD5) {
        //ignore frame type
        listen = false;
        return;
    }

    uint8_t cmd_code = data[0];

    if (cmd_code == PN532_CMD_SAMCONFIGURATION_RESPONSE) {
        //success
        DBUGLN(F("[rfid] connection to PN532 active"));
        status = DeviceStatus::ACTIVE;
        lastResponse = millis();

    } else if (cmd_code == PN532_CMD_INAUTOPOLL_RESPONSE) {

        /*
         * see NXP manual, page 145
         */

        if (data_len < 3 || data[1] == 0) {
            hasContact = false;
            listen = false;
            return;
        }

        if (data_len < 10) {
            listen = false;
            return;
        }

        uint8_t card_type = data[2];
        uint8_t targetDataLen = data[3];
        uint8_t uidLen = data[8];

        if (data_len - 5 < targetDataLen || targetDataLen - 5 < uidLen) {
            DBUGLN(F("[rfid] INAUTOPOLL format err"));
            listen = false;
            return;
        }

        lastResponse = millis(); //successfully read

        if (hasContact) {
            //valid card already scanned the last time; nothing to do
            listen = false;
            return;
        }

        String uid = String('\0');
        uid.reserve(2*uidLen);

        for (uint8_t i = 0; i < uidLen; i++) {
            uint8_t uid_i = data[9 + i];
            uint8_t lhw = uid_i / 0x10;
            uint8_t rhw = uid_i % 0x10;
            uid += (char) (lhw <= 9 ? lhw + '0' : lhw%10 + 'a');
            uid += (char) (rhw <= 9 ? rhw + '0' : rhw%10 + 'a');
        }

        DBUG(F("[rfid] found card! type = "));
        DBUG(card_type);
        DBUG(F(", uid = "));
        DBUG(uid);
        DBUGLN(F(" end"));
        (void) card_type;

        onCardDetected(uid);
        hasContact = true;

    } else {
        DBUG(F("[rfid] unknown response; cmd_code = "));
        DBUGLN(cmd_code);
    }

    listen = false;
    return;
}

PN532 pn532;
