#ifndef UPDATE_SMART_EVSE_H
#define UPDATE_SMART_EVSE_H

#include <Arduino.h>
#include <MD5Builder.h>
#include <functional>
#include "esp_partition.h"

#define UPDATE_ERROR_OK (0)
#define UPDATE_ERROR_WRITE (1)
#define UPDATE_ERROR_ERASE (2)
#define UPDATE_ERROR_READ (3)
#define UPDATE_ERROR_SPACE (4)
#define UPDATE_ERROR_SIZE (5)
#define UPDATE_ERROR_STREAM (6)
#define UPDATE_ERROR_MD5 (7)
#define UPDATE_ERROR_MAGIC_BYTE (8)
#define UPDATE_ERROR_ACTIVATE (9)
#define UPDATE_ERROR_NO_PARTITION (10)
#define UPDATE_ERROR_BAD_ARGUMENT (11)
#define UPDATE_ERROR_ABORT (12)

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

class UpdateControllerClass
{
public:
    typedef std::function<void(size_t, size_t)> THandlerFunction_Progress;

    UpdateControllerClass();

    /*
      This callback will be called when Update is receiving data
    */
    UpdateControllerClass &onProgress(THandlerFunction_Progress fn);

    /*
      Call this to check the space needed for the update
      Will return false if there is not enough space
    */
    bool begin(size_t size = UPDATE_SIZE_UNKNOWN, int ledPin = -1, uint8_t ledOn = LOW);

    /*
      Writes a buffer to the flash and increments the address
      Returns the amount written
    */
    size_t write(uint8_t *data, size_t len);

    /*
      If all bytes are written
      this call will write the config to eboot
      and return true
      If there is already an update running but is not finished and !evenIfRemaining
      or there is an error
      this will clear everything and return false
      the last error is available through getError()
      evenIfRemaining is helpfull when you update without knowing the final size first
    */
    bool end(bool evenIfRemaining = false);

    /*
      Aborts the running update
    */
    void abort();

    /*
      Prints the last error to an output stream
    */
    void printError(Stream &out);

    const char *errorString();

    //Helpers
    uint8_t getError() { return _error; }
    void clearError() { _error = UPDATE_ERROR_OK; }
    bool hasError() { return _error != UPDATE_ERROR_OK; }
    bool isRunning() { return _size > 0; }
    bool isFinished() { return _progress == _size; }
    size_t size() { return _size; }
    size_t progress() { return _progress; }
    size_t remaining() { return _size - _progress; }

private:
    void _reset();
    void _abort(uint8_t err);
    bool _writeBuffer();
    bool _verifyHeader(uint8_t data);
    bool _verifyEnd();

    void _resetController();
    int _receivePacket(bool nocrc=false);
    int _receiveByte(char byte, bool nocrc);

    uint16_t _sendPacketStart(char *buf, size_t len);
    void _sendPacketEnd(uint16_t crc);
    void _sendPacket(char *buf, size_t len);

    int _processRecord();
    int _readHex(uint8_t *data, size_t len);

    int _flashBlock();

    uint8_t _error;
    size_t _size;

    static const size_t MAX_BUF_LEN = 256;
    uint8_t _buffer[MAX_BUF_LEN + 2];
    size_t _buffer_pos = 0;
    
    enum
    {
      IDLE,
      STARTED,
      ESCAPE
    } _receiveState = IDLE;

    THandlerFunction_Progress _progress_callback;
    uint32_t _progress;

    static const size_t MAX_HEX_RECORD_SIZE = 256 + 8 + 1;
    static const size_t FLASH_BLOCK_SIZE = 64;
    static const int PACKET_TIMEOUT = 1000;

    union
    {
      char raw[MAX_HEX_RECORD_SIZE];

#pragma pack(push, 1)
      struct
      {
        uint8_t byte_count;
        uint16_t address;
        uint8_t record_type;
        char data[];
      };
#pragma pack(pop)
    } _hexData;

    unsigned int _data_char;

    bool _hexInRecord = false;
    uint32_t _baseAddress = 0;

    uint8_t _flashData[FLASH_BLOCK_SIZE];
    uint32_t _flashStartAddress;

    uint32_t _bootloader_address;
    uint8_t _app_reset_vector[4];

    uint32_t _flashBlocksWritten;


};

extern UpdateControllerClass UpdateController;

#endif
