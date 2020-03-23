/*
  SPI.cpp - Linuxduino Serial (UART) library header

  Copyright (c) 2016 Jorge Garza <jgarzagu@ucsd.edu>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef SerialLinux_h
#define SerialLinux_h

#include <stdint.h>
#include <string>
#include "WString.h"

/////////////////////////////////////////////
//          Set/Get Default (UART)        //
////////////////////////////////////////////

extern char SERIAL_DRIVER_NAME[];

void setSerial(std::string defaultSerial);
void setSerial(const char * defaultSerial);
std::string getSerial_std(void);
char * getSerial(void);

/////////////////////////////////////////////
//          SerialLinux class (UART)      //
////////////////////////////////////////////

// Printing format options
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// Defines for setting data, parity, and stop bits
// e.g SERIAL_ABC
// A= data (5 bits, 6 bits, 7 bits, 8 bits)
// B= parity (None, Even, Odd)
// C= stop bits (1 bit, 2 bits) 
#define SERIAL_5N1 0x00
#define SERIAL_6N1 0x02
#define SERIAL_7N1 0x04
#define SERIAL_8N1 0x06 // default
#define SERIAL_5N2 0x08
#define SERIAL_6N2 0x0A
#define SERIAL_7N2 0x0C
#define SERIAL_8N2 0x0E
#define SERIAL_5E1 0x20
#define SERIAL_6E1 0x22
#define SERIAL_7E1 0x24
#define SERIAL_8E1 0x26
#define SERIAL_5E2 0x28
#define SERIAL_6E2 0x2A
#define SERIAL_7E2 0x2C
#define SERIAL_8E2 0x2E
#define SERIAL_5O1 0x30
#define SERIAL_6O1 0x32
#define SERIAL_7O1 0x34
#define SERIAL_8O1 0x36
#define SERIAL_5O2 0x38
#define SERIAL_6O2 0x3A
#define SERIAL_7O2 0x3C
#define SERIAL_8O2 0x3E

// A char not found in a valid ASCII numeric field
#define NO_IGNORE_CHAR  '\x01' 


class SerialLinux {

private:
	int fd;
	FILE * fd_file;
	long timeOut;
	timespec timeDiff(timespec start, timespec end);
	int timedPeek();
	int peekNextDigit(bool detectDecimal);
	long timeDiffmillis(timespec start, timespec end);
	char * int2bin(int n);

public:
	SerialLinux();
	void begin(int baud, unsigned char config);
	void begin(int baud);
	void begin(std::string serialPort, int baud, unsigned char config);
	void begin(std::string serialPort, int baud);
	void begin(const char *serialPort, int baud, unsigned char config = SERIAL_8N1);
	void end();
	int available();
	int availableForWrite();
	bool find(std::string target);
	bool find(const char *target);
	bool findUntil(std::string target, std::string terminator);
	bool findUntil(const char *target, const char *terminator);
	void flush();
	long parseInt() { return parseInt(NO_IGNORE_CHAR); };
	long parseInt(char ignore);
	long parseInt_js(std::string ignore);
	float parseFloat();
	int peek();
	size_t print(const String &s);
	size_t print(std::string str);
	size_t print(const char str[]);
	size_t print(char c);
	size_t print(unsigned char b, int base);
	size_t print(int n, int base);
	size_t print(unsigned int n, int base);
	size_t println(void);
	size_t println(const String &s);
	size_t println(std::string c);
	size_t println(const char c[]);
	size_t println(char c);
	size_t println(unsigned char b, int base);
	size_t println(int num, int base);
	size_t println(unsigned int num, int base);
	size_t printf(const char *fmt, ... );
	int read();
	size_t readBytes(char buffer[], size_t length);
	std::string readBytes_js(std::string buffer, size_t length);
	size_t readBytesUntil(char terminator, char buffer[], size_t length);
	std::string readBytesUntil_js(std::string terminator, std::string buffer, size_t length);
	String readString();
	std::string readString_std();
	String readStringUntil(char terminator);
	std::string readStringUntil_std(char terminator);
	std::string readStringUntil_js(std::string terminator);
	size_t readStringCommand(char terminator, char buffer[], size_t length);
	std::string readStringCommand_js(std::string terminator, std::string buffer, size_t length);
	void setTimeout(long millis);
	size_t write(uint8_t c);
	size_t write(std::string str);
	size_t write(const char *str);
	size_t write(char *buffer, size_t size);
	size_t write_js(std::string buffer, size_t size);
	operator bool() { return (fd == -1) ? false : true; }

};

extern SerialLinux Serial;


#endif