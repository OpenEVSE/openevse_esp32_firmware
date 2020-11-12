
#include <hardwareserial.h>
HardwareSerial &RAW_RAPI_PORT = RAPI_PORT;

#include <update_controller.h>
#include "debug.h"
#include "uCRC16BPBLib.h"
#include <Arduino.h>

enum ControlCharacters
{
    STX = 0x0F,
    ETX = 0x04,
    DLE = 0x05
};

enum BootloaderCommand
{
    GET_INFO = 0,
    READ_FLASH = 1,
    READ_FLASH_CRC = 2,
    ERASE_FLASH = 3,
    WRITE_FLASH = 4,
    READ_EEPROM = 5,
    WRITE_EEPROM = 6,
    WRITE_CONFIG = 7,
    RUN_APPLICATION = 8
};

enum
{
    DATA = 0,
    END_OF_FILE = 1,
    EXTENDED_LINEAR_ADDRESS = 4
};

static uint16_t crc_update(uint16_t crc, char data)
{
    unsigned int i;
    bool bit;
    unsigned char c = data;

    for (i = 0x80; i > 0; i >>= 1)
    {
        bit = crc & 0x8000;
        if (c & i)
        {
            bit = !bit;
        }
        crc <<= 1;
        if (bit)
        {
            crc ^= 0x1021;
        }
    }

    return crc;
}

UpdateControllerClass::UpdateControllerClass() : _error(0), _size(0), _progress_callback(0), _progress(0) {}

UpdateControllerClass &UpdateControllerClass::onProgress(THandlerFunction_Progress fn)
{
    // update progress on web page side
    _progress_callback = fn;
    return *this;
}

void UpdateControllerClass::_resetController()
{
#if defined(RAPI_PORT_RESET) && RAPI_PORT_RESET != -1
#ifdef RAPI_PORT_RESET_ACTIVELOW
    digitalWrite(RAPI_PORT_RESET, LOW);
#else
    digitalWrite(RAPI_PORT_RESET, HIGH);
#endif
    delay(10);
#ifdef RAPI_PORT_RESET_ACTIVELOW
    digitalWrite(RAPI_PORT_RESET, HIGH);
#else
    digitalWrite(RAPI_PORT_RESET, LOW);
#endif
#endif

    _receiveState = IDLE;
    _buffer_pos = 0;
}

static void _sendEscapedByte(char byte)
{
    if (byte == STX || byte == ETX || byte == DLE)
        RAW_RAPI_PORT.write(DLE);
    RAW_RAPI_PORT.write(byte);
}

uint16_t UpdateControllerClass::_sendPacketStart(char *buf, size_t len)
{
    uint16_t crc = 0;
    RAW_RAPI_PORT.write(STX);

    for (int i = 0; i < len; i++)
    {
        _sendEscapedByte(buf[i]);
        crc = crc_update(crc, buf[i]);
    }

    return crc;
}

void UpdateControllerClass::_sendPacketEnd(uint16_t crc)
{
    _sendEscapedByte(crc & 0xff);
    _sendEscapedByte(crc >> 8);

    RAW_RAPI_PORT.write(ETX);
}

void UpdateControllerClass::_sendPacket(char *buf, size_t len)
{
    uint16_t crc = _sendPacketStart(buf, len);
    _sendPacketEnd(crc);
}

int UpdateControllerClass::_receiveByte(char byte, boolean nocrc)
{
    switch (_receiveState)
    {
    case ESCAPE:
        if (_buffer_pos + 1 >= MAX_BUF_LEN)
        {
            DEBUG.printf("Buffer overrun in escape: %d %d", _buffer[0], _buffer[MAX_BUF_LEN - 1]);
            return -1;
        }

        _buffer[_buffer_pos++] = byte;
        _receiveState = STARTED;
        return 0;
    case IDLE:
        switch (byte)
        {
        case STX:
            _receiveState = STARTED;
            _buffer_pos = 0;
            return 0;
        case DLE:
            _receiveState = ESCAPE;
            return 0;
        case ETX:
        default:
            return 0;
        }
    case STARTED:
        switch (byte)
        {
        case STX:
            _receiveState = STARTED;
            _buffer_pos = 0;
            return 0;
        case ETX:
        {
            _receiveState = IDLE;
            if (nocrc)
            {
                return _buffer_pos;
            }
            else
            {
                if (_buffer_pos < 2)
                    return -1;
                else if (_buffer_pos == 2)
                    return 0;

                _buffer_pos -= 2;

                uint16_t crc_res = 0;

                for (int i = 0; i < _buffer_pos; i++)
                    crc_res = crc_update(crc_res, _buffer[i]);

                if ((_buffer[_buffer_pos] | (_buffer[_buffer_pos + 1] << 8)) != crc_res)
                {
                    DEBUG_PORT.printf("CRC mismatch: %d != %d\n", crc_res, (_buffer[_buffer_pos] | (_buffer[_buffer_pos + 1] << 8)));
                    return -1;
                }
                return _buffer_pos;
            }
        }
        case DLE:
            _receiveState = ESCAPE;
            return 0;
        default:
            if (_buffer_pos + 1 >= MAX_BUF_LEN)
            {
                DEBUG.printf("Buffer overrun: %d %d", _buffer[0], _buffer[MAX_BUF_LEN - 1]);
                return -1;
            }
            _buffer[_buffer_pos++] = byte;
            return 0;
        }
    default:
        DEBUG_PORT.printf("Illegal state: %d\n", _receiveState);
        _receiveState = IDLE;
        return -1;
    }
}

int UpdateControllerClass::_receivePacket(boolean nocrc)
{
    unsigned long start = millis();

    int res = 0;
    do
    {
        int byte = RAW_RAPI_PORT.read();
        if (byte >= 0)
            res = _receiveByte(byte, nocrc);
    } while (res == 0 && millis() - start < PACKET_TIMEOUT);

    if (res == 0)
    { // timed out
        DEBUG_PORT.printf("Timeout receiving packet\n");
        return -1;
    }
    else
        return res;
}

#define SEND_PACKET(...)               \
    {                                  \
        char tmp[] = {__VA_ARGS__};    \
        _sendPacket(tmp, sizeof(tmp)); \
    }

#define SEND_PACKET_DATA(data, len, cmd, ...)                  \
    {                                                          \
        char tmp[] = {cmd, __VA_ARGS__};                       \
        uint16_t crc = _sendPacketStart(tmp, sizeof(tmp));     \
        for (int i = 0; i < len; i++)                          \
        {                                                      \
            _sendEscapedByte(data[i]);                         \
            crc = crc_update(crc, data[i]);                    \
        }                                                      \
        _sendPacketEnd(crc);                                   \
                                                               \
        RECEIVE_PACKET(false, 1, -1, "Failed writing packet"); \
        if (_buffer[0] != cmd)                                 \
        {                                                      \
            DEBUG_PORT.println("Wrong acknowledgement ");      \
            return -1;                                         \
        }                                                      \
    }

#define RECEIVE_PACKET(nocrc, expected, retval, error)                  \
    {                                                                   \
        int len = _receivePacket(nocrc);                                \
        if (len != expected)                                            \
        {                                                               \
            DEBUG_PORT.printf("%s (%d != %d)\n", error, len, expected); \
            return retval;                                              \
        }                                                               \
    }

bool UpdateControllerClass::begin(size_t size, int ledPin, uint8_t ledOn)
{
    _size = size;

    DEBUG_PORT.println("Entering bootloader");
    unsigned long start;

    // Try exiting bootloader if accidentally stuck
    SEND_PACKET(RUN_APPLICATION);
    RAW_RAPI_PORT.flush();

    // Enter bootloader by causing framing error (switch to 300 baud and send 0s)
    RAW_RAPI_PORT.updateBaudRate(300);
    RAW_RAPI_PORT.write(0);
    _resetController();
    RAW_RAPI_PORT.flush();
    RAW_RAPI_PORT.updateBaudRate(115200);

    // Detect bootloader started by sending STX till STX is returned
    start = millis();
    char detectSTX = 0;
    while ((detectSTX = RAW_RAPI_PORT.read()) != STX && millis() - start < PACKET_TIMEOUT)
    {
        if (detectSTX != 255)
            DEBUG_PORT.printf("-: %d\n", detectSTX);
        RAW_RAPI_PORT.write(STX);
        yield();
    }

    if (detectSTX != STX)
    {
        DEBUG_PORT.printf("Timeout entering bootloader\n");
        return false;
    }

    // Get information
    SEND_PACKET(GET_INFO);
    RECEIVE_PACKET(false, 10, false, "Error receiving bootloader info");

    uint16_t bootloader_size = _buffer[0] | _buffer[1] << 8;
    _bootloader_address = _buffer[6] | _buffer[7] << 8 | _buffer[8] << 16;
    uint16_t bootloader_version_major = _buffer[2];
    uint16_t bootloader_version_minor = _buffer[3];
    uint16_t bootloader_device_type = _buffer[5] & 0xf;

    DEBUG_PORT.printf("Bootloader size: %4x, version: %d.%02d, %s, bootloader address: %04x\n",
                      bootloader_size,
                      bootloader_version_major, bootloader_version_minor,
                      bootloader_device_type == 4 ? "PIC18" : "PIC16",
                      _bootloader_address);

    if (bootloader_device_type != 4)
    {
        DEBUG_PORT.printf("Wrong device type (%d)\n", bootloader_device_type);
        return false;
    }

    if (bootloader_version_major != 1 && bootloader_version_minor != 6)
    {
        DEBUG_PORT.printf("Wrong bootloader version (%d.%02d != 1.06)\n", bootloader_version_major, bootloader_version_minor);
        return false;
    }

    SEND_PACKET(READ_FLASH, 0xfe, 0xff, 0x3f, 0x00, 0x2, 0x00);

    RECEIVE_PACKET(false, 2, false, "Error receiving device id");

    uint16_t device_id = _buffer[1] << 3 | ((_buffer[0] & 0xe0) >> 5);
    uint16_t device_revision = _buffer[1] & 0x1f;
    DEBUG_PORT.printf("Device id: %04x, Revision: %d\n", device_id, device_revision);

    if (device_id != 0x02a2)
    {
        DEBUG_PORT.printf("Wrong device id (%d)\n", device_id);
        return false;
    }

    memset(_flashData, -1, FLASH_BLOCK_SIZE);
    _flashStartAddress = 0;
    _baseAddress = 0;
    _data_char = 0;
    _hexInRecord = false;
    _flashBlocksWritten = 0;

    return true;
}

int UpdateControllerClass::_flashBlock()
{
    if (_flashStartAddress >= 0x200000 && _flashStartAddress < 0x300000)
    {
        SEND_PACKET(READ_EEPROM, (char)((_flashStartAddress - 0x200000) & 0xff), (char)(((_flashStartAddress - 0x200000) >> 8) & 0xff), (char)(((_flashStartAddress - 0x200000) >> 16) & 0xff), 0x00, FLASH_BLOCK_SIZE, 0x00);
        RECEIVE_PACKET(false, FLASH_BLOCK_SIZE, -1, "Failed reading eeprom");
    }
    else
    {
        SEND_PACKET(READ_FLASH, (char)(_flashStartAddress & 0xff), (char)((_flashStartAddress >> 8) & 0xff), (char)((_flashStartAddress >> 16) & 0xff), 0x00, FLASH_BLOCK_SIZE, 0x00);
        RECEIVE_PACKET(false, FLASH_BLOCK_SIZE, -1, "Failed reading flash");
    }

    // copy bootloader reset vector
    if (_flashStartAddress == 0x0)
    {
        for (int i = 0; i < 4; i++)
        {
            _app_reset_vector[i] = _flashData[i];
            _flashData[i] = _buffer[i];
        }
    }

    // copy original reset vector to just before bootloader
    if (_flashStartAddress <= _bootloader_address - 4 && _flashStartAddress + FLASH_BLOCK_SIZE >= _bootloader_address)
    {
        for (int i = 0; i < 4; i++)
            _flashData[_bootloader_address - 4 - _flashStartAddress + i] = _app_reset_vector[i];
    }

    bool matches = true;
    for (int i = 0; i < FLASH_BLOCK_SIZE; i++)
        if (_buffer[i] != _flashData[i])
            matches = false;

    if (!matches)
    {
        if (_flashStartAddress < 0x200000)
        {
            SEND_PACKET_DATA(_flashData, 0, ERASE_FLASH, (char)((_flashStartAddress + FLASH_BLOCK_SIZE - 1) & 0xff), (char)(((_flashStartAddress + FLASH_BLOCK_SIZE - 1) >> 8) & 0xff), (char)(((_flashStartAddress + FLASH_BLOCK_SIZE - 1) >> 16) & 0xff), 0x00, 1);
            SEND_PACKET_DATA(_flashData, FLASH_BLOCK_SIZE, WRITE_FLASH, (char)((_flashStartAddress)&0xff), (char)(((_flashStartAddress) >> 8) & 0xff), (char)(((_flashStartAddress) >> 16) & 0xff), 0x00, 1);
        }
        else if ((_flashStartAddress >= 0x200000 && _flashStartAddress < 0x300000))
        {
            // Skip EEPROM: keep existing values
            //   SEND_PACKET_DATA(_flashData, FLASH_BLOCK_SIZE, WRITE_EEPROM, (char)((_flashStartAddress - 0x200000) & 0xff), (char)(((_flashStartAddress - 0x200000) >> 8) & 0xff), (char)(((_flashStartAddress - 0x200000) >> 16) & 0xff), 0x00, FLASH_BLOCK_SIZE, 0x00);
        }
        else if ((_flashStartAddress >= 0x300000 && _flashStartAddress < 0x400000))
        {
            // SkipCOnfig: keep existing values
            // SEND_PACKET_DATA(_flashData, FLASH_BLOCK_SIZE, WRITE_CONFIG, (char)((_flashStartAddress - 0x300000) & 0xff), (char)(((_flashStartAddress - 0x300000) >> 8) & 0xff), (char)(((_flashStartAddress - 0x300000) >> 16) & 0xff), 0x00, FLASH_BLOCK_SIZE);
        }
        _flashBlocksWritten++;
    }

    return 0;
}

int UpdateControllerClass::_processRecord()
{
    // decode big-endian address
    _hexData.address = (_hexData.raw[1] << 8) | _hexData.raw[2];

    switch (_hexData.record_type)
    {
    case EXTENDED_LINEAR_ADDRESS:
        _baseAddress = (((uint32_t)_hexData.data[0]) << 24) | (((uint32_t)_hexData.data[1]) << 16);
        return 0;
    case END_OF_FILE:
        // flash last data
        if (_flashBlock() < 0)
            return -1;
        return 0;
    case DATA:
    {
        if (_hexData.byte_count == 0)
        {
            DEBUG_PORT.printf("Byte Count = 0\n");
            return -1;
        }
        uint32_t startAddress = _baseAddress | _hexData.address;

        uint32_t address = startAddress;

        while (address < startAddress + _hexData.byte_count)
        {
            if (address >= _flashStartAddress + FLASH_BLOCK_SIZE || address + _hexData.byte_count <= _flashStartAddress)
            { // no interaction of HEX data and current flash block
                if (_flashBlock() < 0)
                {
                    return -1;
                };

                memset(_flashData, -1, FLASH_BLOCK_SIZE);
                _flashStartAddress = address / FLASH_BLOCK_SIZE * FLASH_BLOCK_SIZE;
            }
            else if (address < _flashStartAddress && address + _hexData.byte_count > _flashStartAddress)
            { // current HEX data starts before and overlaps start of flash block
                DEBUG_PORT.println("HEX file with non-incremental addresses not supported");
                return -1;
            }
            else
            { // current HEX data overlaps other part of flash block
                size_t pos_in_buf = address - _flashStartAddress;
                uint32_t data_size = min((unsigned int)_hexData.byte_count, FLASH_BLOCK_SIZE - pos_in_buf);

                memcpy(_flashData + pos_in_buf, _hexData.data + address - startAddress, data_size);
                address += data_size;
            }
        }

        return 0;
    }
    default:
        DEBUG_PORT.printf("Unsupported record type: type: %d, length: %d, addr: %04x\n", (int)_hexData.record_type, (int)_hexData.byte_count, (int)_hexData.address);
        return -1;
    }
    return 0;
}

int UpdateControllerClass::_readHex(uint8_t *data, size_t len)
{
    for (int pos = 0; pos < len; pos++)
    {
        if (!_hexInRecord)
        {
            if (data[pos] == ':')
            {
                _hexInRecord = true;
                _data_char = 0;
            }
            else if (!isspace(data[pos]))
            {
                DEBUG_PORT.printf("Illegal character in input (%02x)\n", data[pos]);
                return -1;
            }
        }
        else
        {
            if (isxdigit(data[pos]))
            {
                if (_data_char % 2 == 0)
                    _hexData.raw[_data_char / 2] = ((data[pos] <= '9') ? data[pos] - '0' : (data[pos] & 0x7) + 9) << 4;
                else
                    _hexData.raw[_data_char / 2] |= (data[pos] <= '9') ? data[pos] - '0' : (data[pos] & 0x7) + 9;

                _data_char++;
            }
            else if (isspace(data[pos]))
            {
                if (_data_char < 10)
                {
                    DEBUG_PORT.printf("Record too short (%d<10)\n", _data_char);
                    return -1;
                }
                if (_hexData.byte_count * 2 + 10 != _data_char)
                {
                    DEBUG_PORT.printf("Record length does not match with byte count (%d != %dx2+10)\n", _data_char, _hexData.byte_count);
                    return -1;
                }
                unsigned char crc = 0;
                for (int i = 0; i < _data_char / 2; i++)
                    crc += _hexData.raw[i];
                if (crc != 0)
                {
                    DEBUG_PORT.printf("Record CRC does not match (%02x!=0)\n", crc);
                    return -1;
                }

                if (_processRecord() < 0)
                {
                    return -1;
                }

                _hexInRecord = false;
            }
            else
            {
                _hexInRecord = false;
                DEBUG_PORT.printf("Illegal character in input (%02x)\n", data[pos]);
                return -1;
            }
        }
    }
    return len;
}

/*
      Writes a buffer to the flash and increments the address
      Returns the amount written
*/
size_t UpdateControllerClass::write(uint8_t *data, size_t len)
{
    int res = _readHex(data, len);
    if (res < 0)
        res = 0;

    _progress += len;
    yield();
    feedLoopWDT();

    if (len != res)
    {
        DEBUG_PORT.println("Error: exiting bootloader");
        SEND_PACKET(RUN_APPLICATION);
        _resetController();
    }

    return res;
}

/*
    End the running update
*/
bool UpdateControllerClass::end(bool evenIfRemaining)
{
    DEBUG_PORT.printf("Success: Written %d bytes. Exiting bootloader\n", _flashBlocksWritten*FLASH_BLOCK_SIZE);
    SEND_PACKET(RUN_APPLICATION);
    _resetController();

    _size = 0;
    return true;
}

/*
      Aborts the running update
*/
void UpdateControllerClass::abort()
{
    DEBUG_PORT.println("Aborting: Exiting bootloader");
    SEND_PACKET(RUN_APPLICATION);
    _resetController();

    _size = 0;
}

/*
      Prints the last error to an output stream
*/
void UpdateControllerClass::printError(Stream &out) {}

UpdateControllerClass UpdateController;
