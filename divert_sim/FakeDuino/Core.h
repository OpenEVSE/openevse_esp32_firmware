/*
  Core.h - Linuxduino Digital, PWM and Arduino types header

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

#ifndef Core_h
#define Core_h

#include <stdint.h>
#include <math.h>
#include <time.h>


// General Library Version 
#define LINUXDUINO_VERSION "0.2.5"

// Max number of GPIO Pins available (Contact me if you need more pins)
#ifndef SOC_GPIO_PINS
  #define SOC_GPIO_PINS 255
#endif

// Remove some PROGMEM space macros if possible
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#define pgm_read_float(addr) (*(const float *)(addr))
#define pgm_read_ptr(addr) (*(const void *)(addr))


// Arduino extra types
typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;
#define B00000000 0
#define B00000001 1
#define B00000010 2
#define B00000011 3
#define B00000100 4
#define B00000101 5
#define B00000110 6
#define B00000111 7
#define B00001000 8
#define B00001001 9
#define B00001010 10
#define B00001011 11
#define B00001100 12
#define B00001101 13
#define B00001110 14
#define B00001111 15
#define B00010000 16
#define B00010001 17
#define B00010010 18
#define B00010011 19
#define B00010100 20
#define B00010101 21
#define B00010110 22
#define B00010111 23
#define B00011000 24
#define B00011001 25
#define B00011010 26
#define B00011011 27
#define B00011100 28
#define B00011101 29
#define B00011110 30
#define B00011111 31
#define B00100000 32
#define B00100001 33
#define B00100010 34
#define B00100011 35
#define B00100100 36
#define B00100101 37
#define B00100110 38
#define B00100111 39
#define B00101000 40
#define B00101001 41
#define B00101010 42
#define B00101011 43
#define B00101100 44
#define B00101101 45
#define B00101110 46
#define B00101111 47
#define B00110000 48
#define B00110001 49
#define B00110010 50
#define B00110011 51
#define B00110100 52
#define B00110101 53
#define B00110110 54
#define B00110111 55
#define B00111000 56
#define B00111001 57
#define B00111010 58
#define B00111011 59
#define B00111100 60
#define B00111101 61
#define B00111110 62
#define B00111111 63
#define B01000000 64
#define B01000001 65
#define B01000010 66
#define B01000011 67
#define B01000100 68
#define B01000101 69
#define B01000110 70
#define B01000111 71
#define B01001000 72
#define B01001001 73
#define B01001010 74
#define B01001011 75
#define B01001100 76
#define B01001101 77
#define B01001110 78
#define B01001111 79
#define B01010000 80
#define B01010001 81
#define B01010010 82
#define B01010011 83
#define B01010100 84
#define B01010101 85
#define B01010110 86
#define B01010111 87
#define B01011000 88
#define B01011001 89
#define B01011010 90
#define B01011011 91
#define B01011100 92
#define B01011101 93
#define B01011110 94
#define B01011111 95
#define B01100000 96
#define B01100001 97
#define B01100010 98
#define B01100011 99
#define B01100100 100
#define B01100101 101
#define B01100110 102
#define B01100111 103
#define B01101000 104
#define B01101001 105
#define B01101010 106
#define B01101011 107
#define B01101100 108
#define B01101101 109
#define B01101110 110
#define B01101111 111
#define B01110000 112
#define B01110001 113
#define B01110010 114
#define B01110011 115
#define B01110100 116
#define B01110101 117
#define B01110110 118
#define B01110111 119
#define B01111000 120
#define B01111001 121
#define B01111010 122
#define B01111011 123
#define B01111100 124
#define B01111101 125
#define B01111110 126
#define B01111111 127
#define B10000000 128
#define B10000001 129
#define B10000010 130
#define B10000011 131
#define B10000100 132
#define B10000101 133
#define B10000110 134
#define B10000111 135
#define B10001000 136
#define B10001001 137
#define B10001010 138
#define B10001011 139
#define B10001100 140
#define B10001101 141
#define B10001110 142
#define B10001111 143
#define B10010000 144
#define B10010001 145
#define B10010010 146
#define B10010011 147
#define B10010100 148
#define B10010101 149
#define B10010110 150
#define B10010111 151
#define B10011000 152
#define B10011001 153
#define B10011010 154
#define B10011011 155
#define B10011100 156
#define B10011101 157
#define B10011110 158
#define B10011111 159
#define B10100000 160
#define B10100001 161
#define B10100010 162
#define B10100011 163
#define B10100100 164
#define B10100101 165
#define B10100110 166
#define B10100111 167
#define B10101000 168
#define B10101001 169
#define B10101010 170
#define B10101011 171
#define B10101100 172
#define B10101101 173
#define B10101110 174
#define B10101111 175
#define B10110000 176
#define B10110001 177
#define B10110010 178
#define B10110011 179
#define B10110100 180
#define B10110101 181
#define B10110110 182
#define B10110111 183
#define B10111000 184
#define B10111001 185
#define B10111010 186
#define B10111011 187
#define B10111100 188
#define B10111101 189
#define B10111110 190
#define B10111111 191
#define B11000000 192
#define B11000001 193
#define B11000010 194
#define B11000011 195
#define B11000100 196
#define B11000101 197
#define B11000110 198
#define B11000111 199
#define B11001000 200
#define B11001001 201
#define B11001010 202
#define B11001011 203
#define B11001100 204
#define B11001101 205
#define B11001110 206
#define B11001111 207
#define B11010000 208
#define B11010001 209
#define B11010010 210
#define B11010011 211
#define B11010100 212
#define B11010101 213
#define B11010110 214
#define B11010111 215
#define B11011000 216
#define B11011001 217
#define B11011010 218
#define B11011011 219
#define B11011100 220
#define B11011101 221
#define B11011110 222
#define B11011111 223
#define B11100000 224
#define B11100001 225
#define B11100010 226
#define B11100011 227
#define B11100100 228
#define B11100101 229
#define B11100110 230
#define B11100111 231
#define B11101000 232
#define B11101001 233
#define B11101010 234
#define B11101011 235
#define B11101100 236
#define B11101101 237
#define B11101110 238
#define B11101111 239
#define B11110000 240
#define B11110001 241
#define B11110010 242
#define B11110011 243
#define B11110100 244
#define B11110101 245
#define B11110110 246
#define B11110111 247
#define B11111000 248
#define B11111001 249
#define B11111010 250
#define B11111011 251
#define B11111100 252
#define B11111101 253
#define B11111110 254
#define B11111111 255


/////////////////////////////////////////////
//          Digital I/O           		    //
////////////////////////////////////////////
// Pin logic states
#define HIGH 0x1
#define LOW  0x0

// ALL Hardware Pin modes
#define ALL_HDW 0x00
// ---------
#define INPUT 0x00
#define OUTPUT 0x01
#define INPUT_PULLUP 0x02
#define INPUT_PULLDOWN 0x03

// RPI Hardware Pin modes
#define RPI_HDW 0x01
// ---------
#define RPI_INPUT 0x10
#define RPI_OUTPUT 0x11
#define RPI_INPUT_PULLUP 0x12
#define RPI_INPUT_PULLDOWN 0x13
#define RPI_PWM_OUTPUT 0x14

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);

/////////////////////////////////////////////
//          Analog I/O           		      //
////////////////////////////////////////////
#ifndef PWM_DUTYCYCLE_RESOLUTION
  // Set pwm duty cycle resolution between 0 and 255 bits
  #define PWM_DUTYCYCLE_RESOLUTION 256        
#endif
#ifndef PWM_DEFAULT_FREQUENCY
  // Set default pwm frequency to 490 Hz (Arduino default pwm freq)
  #define PWM_DEFAULT_FREQUENCY 490           
#endif

//int analogRead (int pin); // Not implemented
//int analogReference(int type) // Not implemented
void analogWrite(uint8_t pin, uint32_t value);
void setPwmDutyCycle (uint8_t pin, uint32_t dutycycle);
void setPwmPeriod (uint8_t pin, uint32_t microseconds);
void setPwmFrequency (uint8_t pin, uint32_t frequency);
void setPwmFrequency (uint8_t pin, uint32_t frequency, uint32_t dutycycle);

/////////////////////////////////////////////
//          Advanced I/O           		    //
////////////////////////////////////////////
#ifndef LSBFIRST
  #define LSBFIRST 1
#endif
#ifndef MSBFIRST
  #define MSBFIRST 0
#endif

void tone(uint8_t pin, uint32_t frequency, unsigned long duration = 0, uint32_t block = false);
void noTone(uint8_t pin);
uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder);
void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val);
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout = 1000000L);

/////////////////////////////////////////////
//          Time      		     		        //
////////////////////////////////////////////

unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long millis);
void delayMicroseconds(unsigned int us);

/////////////////////////////////////////////
//          Math           				        //
////////////////////////////////////////////

#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER 2.718281828459045235360287471352


// The following functions were implemented as functions
// instead of #define to: 
// - prevent std::min and std::max conflicts with <string> header
// - make them able to be ported to javascript
// Note: double can handle all types for javascript

double min(double a, double b);
int min(int a, int b);

double max(double a, double b);
int max(int a, int b);

// abs() already in <stdlib.h>. abs_js() implemented for javascript
double abs_js(double x);

double constrain(double amt, double low, double high);
int constrain(int amt, int low, int high);

long map(long x, long in_min, long in_max, long out_min, long out_max);

// pow() already in <math.h>. pow_js() implemented for javascript
double pow_js(double base, double exponent);

// sqrt() already in <math.h>. sqrt_js() implemented for javascript
double sqrt_js(double x);

// round() already in <math.h>. round_js() implemented for javascript
double round_js(double x);

double radians(double deg);

double degrees(double rad);

double sq(double x);
int sq(int x);

/////////////////////////////////////////////
//          Trigonometry          		    //
////////////////////////////////////////////

// sin(rad) already in <math.h>. sin_js() implemented for javascript
double sin_js(double rad);
// cos(rad) already in <math.h>. cos_js() implemented for javascript
double cos_js(double rad);
// tan(rad) already in <math.h>. tan_js() implemented for javascript
double tan_js(double rad);

/////////////////////////////////////////////
//          Characters           		      //
////////////////////////////////////////////

boolean isAlphaNumeric(int c);
boolean isAlpha(int c);
boolean isAscii(int c);
boolean isWhitespace(int c);
boolean isControl(int c);
boolean isDigit(int c);
boolean isGraph(int c);
boolean isLowerCase(int c);
boolean isPrintable(int c);
boolean isPunct(int c);
boolean isSpace(int c);
boolean isUpperCase(int c);
boolean isHexadecimalDigit(int c);
int toAscii(int c);
int toLowerCase(int c);
int toUpperCase(int c);

/////////////////////////////////////////////
//          Random Functions       		    //
////////////////////////////////////////////

void randomSeed(unsigned long);
long random(long);
long random(long, long);

/////////////////////////////////////////////
//          Bits and Bytes         		    //
////////////////////////////////////////////

// The following functions were implemented as functions
// instead of #define to make them able to be ported to javascript
uint8_t lowByte(uint16_t w);
uint8_t highByte(uint16_t w);
uint32_t bitRead(uint32_t value, uint32_t bit);
uint32_t bitWrite(uint32_t value, uint32_t bit, uint32_t bitvalue);
uint32_t bitSet(uint32_t value, uint32_t bit);
uint32_t bitClear(uint32_t value, uint32_t bit);
uint32_t bit(uint32_t b);


/////////////////////////////////////////////
//          External Interrupts    		    //
////////////////////////////////////////////

// Interrupt modes
#define CHANGE 1
#define FALLING 2
#define RISING 3

#define NOT_AN_INTERRUPT -1
#define digitalPinToInterrupt(p) ((p) >= 0 && (p) <= SOC_GPIO_PINS ? (p) : NOT_AN_INTERRUPT)

void attachInterrupt(uint8_t pin, void (*f)(void), int mode);
void detachInterrupt(uint8_t pin);


/////////////////////////////////////////////
//   Extra Arduino Functions for Linux    //
////////////////////////////////////////////
extern void (*ARDUINO_EXIT_FUNC)(void);


class ArduinoLinux {                                 
    public:
        struct timespec timestamp;
        ArduinoLinux();
        static void onArduinoExit(int signumber);
};


extern ArduinoLinux Arduino;


#endif
