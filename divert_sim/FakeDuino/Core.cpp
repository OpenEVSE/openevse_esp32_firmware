/*
  Core.cpp - Linuxduino Digital, PWM and Arduino functions

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

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include "Core.h"
#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/bind.h>
    using namespace emscripten;
#endif



/////////////////////////////////////////////
//          Digital I/O                   //
////////////////////////////////////////////

// -- Global --
int g_pin_hdw[SOC_GPIO_PINS];               // To know if pin is hardware specific or not (e.g INPUT or RPI_INPUT) 
int g_gpio_pin_set[SOC_GPIO_PINS];          // To know which gpio pins are set (HIGH) or not set (LOW) 
int g_pwm_pin_set[SOC_GPIO_PINS];           // To know which pwm pins are set (HIGH) or not set (LOW)
int g_pwm_dutycycle_value[SOC_GPIO_PINS];   // Pwm duty cycle value of pwm pins

// -- ALL_HDW Digital I/O --
int g_pin_fd [SOC_GPIO_PINS];               // So we don't need to open a file in each read and write (+performance)


/* 
    ### BCM283x SoC Notes for GPIO and PWM ###
    - BCM283x SoCs have a maximum of 54 GPIOS
    - BCM283x SoCs have 2 independent PWM channels (0 and 1) that uses
    the same PWM clock as the base frequency. The following are the
    available PWM pins for this chip:
    GPIO PIN   RPi2 pin   PWM Channel  ALT FUN
    12         YES        0            0
    13         YES        1            0
    18         YES        0            5
    19         YES        1            5
    40                    0            0
    41                    1            0
    45                    1            0
    52                    0            1
    53                    1            1
*/

// -- RPI_HDW Digital I/O --
// BCM2708 Registers for GPIO (Do not put them in .h)
#define BCM2708_PERI_BASE   0x20000000
#define GPIO_BASE           (BCM2708_PERI_BASE + 0x200000)
#define OFFSET_FSEL         0   // 0x0000
#define OFFSET_SET          7   // 0x001c / 4
#define OFFSET_CLR          10  // 0x0028 / 4
#define OFFSET_PINLEVEL     13  // 0x0034 / 4
#define OFFSET_PULLUPDN     37  // 0x0094 / 4
#define OFFSET_PULLUPDNCLK  38  // 0x0098 / 4
#define GPIO_FSEL_INPUT     0   // Pin Input mode
#define GPIO_FSEL_OUTPUT    1   // Pin Output mode
#define BCM_BLOCK_SIZE (4*1024)
static uint32_t *gpio_map = NULL;
static bool g_open_gpiomem_flag = false;

// -- RPI_HDW Analog I/O --
#define BCM2708_PERI_BASE   0x20000000
#define PWM_BASE           (BCM2708_PERI_BASE + 0x20C000)
#define CLOCK_BASE         (BCM2708_PERI_BASE + 0x101000)
#define PWMCLK_CNTL         40
#define PWMCLK_DIV          41
#define PWM_CONTROL         0
#define PWM0_RANGE          4
#define PWM0_DATA           5
#define PWM1_RANGE          8
#define PWM1_DATA           9
#define GPIO_FSEL_ALT0      4  
#define GPIO_FSEL_ALT1      5  
#define GPIO_FSEL_ALT5      2  
static volatile uint32_t *pwm_map = NULL;
static volatile uint32_t *clk_map = NULL;
static bool g_open_pwmmem_flag = false;


// Sets pin (gpio) mode as INPUT,INTPUT_PULLUP,INTPUT_PULLDOWN,OUTPUT
void pinMode(uint8_t pin, uint8_t mode)
{
    // ALL_HDW varaibles
    FILE *fp;
    char *dir_path = NULL;
    char *value_path = NULL;
    // RPI_HDW variables
    int mem_fd, pwm_mem_fd;
    int clk_offset = OFFSET_PULLUPDNCLK + (pin/32);
    int shift_offset = (pin%32);
    int offset = OFFSET_FSEL + (pin/10);
    int shift = (pin%10)*3;
    int gpio_fsel_alt = 0;
    int pwm_channel = 0;

    // Check if the pin number is valid (Contact me if your board needs more pins)
    if (pin >= SOC_GPIO_PINS) {
        fprintf(stderr, "%s(): pin number should be less than "
            "%d, yours is %d \n", __func__, SOC_GPIO_PINS, pin);
        exit(1);
    }

    switch (mode) {
        case INPUT:
        case OUTPUT:
        case INPUT_PULLUP:
        case INPUT_PULLDOWN:

            // Export pin for interrupt
            asprintf(&dir_path, "/sys/class/gpio/gpio%d/direction", pin);
            if (access(dir_path, F_OK) == -1) {
                fp = fopen("/sys/class/gpio/export","w");
                if (fp == NULL) {
                    fprintf(stderr, "%s(): export gpio error msg: %s\n",__func__, strerror (errno));
                    exit(1);
                } else {
                    fprintf(fp,"%d",pin); 
                }
                fclose(fp);
            }

            //Set gpio direction
            fp = fopen(dir_path, "r+");
            if (fp == NULL) {
                // First time may fail because the file may not be ready.
                // if that the case then we wait two seconds and try again.
                sleep(2);
                fp = fopen(dir_path, "r+");
                if (fp == NULL) {
                    fprintf(stderr, "%s(): could not open path (%s) error: %s\n",
                        __func__, dir_path, strerror (errno));
                    exit(1);
                }
            }

            // Write direction mode
            // Note: PULLUP and PULLDOWN resistors is not a standard 
            // in linux or across all boards. INPUT_PULLUP and
            // INPUT_PULLDOWN modes are left as INPUT mode.  
            if ( mode == INPUT || mode == INPUT_PULLUP 
                || mode == INPUT_PULLDOWN ) {
                // INPUT
                fprintf(fp,"in");
            } else {
                // OUTPUT
                fprintf(fp,"out");
            }
            fclose(fp);

            // Open gpio value path and keep it open
            asprintf(&value_path, "/sys/class/gpio/gpio%d/value", pin);
            g_pin_fd[pin] = open(value_path, O_RDWR);
            if (g_pin_fd[pin] < 0) {
                    fprintf(stderr, "%s(): could not open path (%s) error: %s\n",
                        __func__, value_path, strerror (errno));
                    exit(1);
            }

            // Save pin hardware mode.
            g_pin_hdw[pin] = ALL_HDW;
            // Save gpio pin number so at program close we put it to default state
            // Also clear pwm pin to prevent any errro if different pin mode is set multiple times
            g_gpio_pin_set[pin] = HIGH; // INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN
            g_pwm_pin_set[pin] = HIGH;  // PWM_OUTPUT

            // Set default pwm dutycycle to 0%
            g_pwm_dutycycle_value[pin] = 0;
            break;

        // Raspberry PI bcm2078 legacy modes
        case RPI_INPUT:
        case RPI_OUTPUT:
        case RPI_INPUT_PULLUP:
        case RPI_INPUT_PULLDOWN:
        case RPI_PWM_OUTPUT:

            // Initialize gpiomem only once
            if (g_open_gpiomem_flag == false) {

                if ((mem_fd = open("/dev/gpiomem", O_RDWR|O_SYNC) ) < 0) {
                    fprintf(stderr, "%s(): gpio driver %s: %s\n",__func__, 
                        "/dev/gpiomem", strerror (errno));
                    exit(1);
                }

                gpio_map = (uint32_t *)mmap( NULL, BCM_BLOCK_SIZE, 
                    PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_LOCKED, mem_fd, GPIO_BASE);
                if (gpio_map == MAP_FAILED) {
                    fprintf(stderr, "%s(): gpio error: %s\n",__func__, strerror (errno));
                    exit(1);
                }

                // gpio memory mapping initialized correctly
                g_open_gpiomem_flag = true;
            }

            if (mode != RPI_PWM_OUTPUT) {
                // RPI_INPUT, RPI_OUTPUT, RPI_INPUT_PULLUP, RPI_INPUT_PULLDOWN

                // Set resistor mode PULLUP, PULLDOWN or PULLOFF resistor (OUTPUT always PULLOFF)
                if (mode == RPI_INPUT_PULLDOWN) {
                   *(gpio_map+OFFSET_PULLUPDN) = (*(gpio_map+OFFSET_PULLUPDN) & ~3) | 0x01;
                } else if (mode == RPI_INPUT_PULLUP) {
                   *(gpio_map+OFFSET_PULLUPDN) = (*(gpio_map+OFFSET_PULLUPDN) & ~3) | 0x02;
                } else { // mode == PULLOFF
                   *(gpio_map+OFFSET_PULLUPDN) &= ~3;
                }
                usleep(1);
                *(gpio_map+clk_offset) = 1 << shift_offset;
                usleep(1);
                *(gpio_map+OFFSET_PULLUPDN) &= ~3;
                *(gpio_map+clk_offset) = 0;

                // Set pin mode RPI_INPUT/RPI_OUTPUT
                if (mode == RPI_OUTPUT) {
                    *(gpio_map+offset) = (*(gpio_map+offset) & ~(7<<shift)) | (GPIO_FSEL_OUTPUT<<shift);
                } else { // mode == RPI_INPUT or RPI_INPUT_PULLUP or RPI_INPUT_PULLDOWN
                    *(gpio_map+offset) = (*(gpio_map+offset) & ~(7<<shift)) | (GPIO_FSEL_INPUT<<shift);
                }

                // Save pin hardware mode.
                g_pin_hdw[pin] = RPI_HDW;
                // Save gpio pin number so at program close we put it to default state
                // Also clear pwm pin to prevent any errro if different pin mode is set multiple times
                g_gpio_pin_set[pin] = HIGH;
                g_pwm_pin_set[pin] = LOW;

            } else {
                // RPI_PWM_OUTPUT   

                // Check if the pin is compatible for PWM and assign its channel
                switch (pin) {
                    case 12: pwm_channel = 0; gpio_fsel_alt = GPIO_FSEL_ALT0; break;
                    case 13: pwm_channel = 1; gpio_fsel_alt = GPIO_FSEL_ALT0; break;
                    case 18: pwm_channel = 0; gpio_fsel_alt = GPIO_FSEL_ALT5; break;
                    case 19: pwm_channel = 1; gpio_fsel_alt = GPIO_FSEL_ALT5; break;
                    case 40: pwm_channel = 0; gpio_fsel_alt = GPIO_FSEL_ALT0; break;
                    case 41: pwm_channel = 1; gpio_fsel_alt = GPIO_FSEL_ALT0; break;
                    case 45: pwm_channel = 1; gpio_fsel_alt = GPIO_FSEL_ALT0; break;
                    case 52: pwm_channel = 0; gpio_fsel_alt = GPIO_FSEL_ALT1; break;
                    case 53: pwm_channel = 1; gpio_fsel_alt = GPIO_FSEL_ALT1; break;
                    default:
                        fprintf(stderr, "%s(): pin %d can not be set as RPI_PWM_OUTPUT\n", __func__, pin);
                        exit(1);
                        break;
                }

                // Initialize mem only once (this requires sudo)
                if (g_open_pwmmem_flag == false) {

                    if ((pwm_mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
                        fprintf(stderr, "%s(): pwm driver %s: %s\n",__func__, 
                            "/dev/mem", strerror (errno));
                        exit(1);
                    }

                    pwm_map = (uint32_t *)mmap(NULL, BCM_BLOCK_SIZE, 
                        PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_LOCKED, pwm_mem_fd, PWM_BASE);
                    if (pwm_map == MAP_FAILED) {
                        fprintf(stderr, "%s(): pwm error: %s\n", __func__, strerror (errno));
                        exit(1);
                    }

                    clk_map = (uint32_t *)mmap(NULL, BCM_BLOCK_SIZE, 
                        PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_LOCKED, pwm_mem_fd, CLOCK_BASE);    
                    if (clk_map == MAP_FAILED) {
                        fprintf(stderr, "%s(): pwm error: %s\n", __func__, strerror (errno));
                        exit(1);
                    }

                    // pwm memory mapping initialized correctly
                    g_open_pwmmem_flag = true;
                }

                // Save pin hardware mode.
                g_pin_hdw[pin] = RPI_HDW;
                // Save pwm pin number so at program close we put it to default state
                // Also clear gpio pin to prevent any errro if different pin mode is set multiple times
                g_pwm_pin_set[pin] = HIGH;
                g_gpio_pin_set[pin] = LOW;

                // Set default pwm dutycycle to 0%
                g_pwm_dutycycle_value[pin] = 0;

                // Set pin to its corresponding ALT mode or (PWM MODE)
                *(gpio_map+offset) = 
                    (*(gpio_map+offset) & ~(7 << shift)) | ((gpio_fsel_alt << shift) & (7 << shift));

                // Set frequency to default Arduino frequency (490Hz) and duty cycle
                setPwmFrequency(pin, PWM_DEFAULT_FREQUENCY, g_pwm_dutycycle_value[pin]);

                // Ser PWM range to default of 256 bits of resolution
                if (pwm_channel == 1) {
                    *(pwm_map + PWM1_RANGE) = PWM_DUTYCYCLE_RESOLUTION;
                } else {
                    *(pwm_map + PWM0_RANGE) = PWM_DUTYCYCLE_RESOLUTION;
                }

                // Set PWM in MARKSPACE MODE and Enable PWM 
                if (pwm_channel == 1) {
                    *(pwm_map + PWM_CONTROL) |= ( 0x8000 | 0x0100 );  // (PWM1_MS_MODE | PWM1_ENABLE )
                } else {
                    *(pwm_map + PWM_CONTROL) |= ( 0x0080 | 0x0001 );  // (PWM0_MS_MODE | PWM0_ENABLE )
                }
            }
            break;

        default:
            fprintf(stderr, "%s(): pin mode %d is not an available mode \n", __func__, mode);
            exit(1);
            break;
    }
}

// Sets a pin (gpio) output to 1 or 0
void digitalWrite(uint8_t pin, uint8_t val)
{
    int offset;
    char buf;

    // Check if the pin number is valid
    if (pin >= SOC_GPIO_PINS) {
        fprintf(stderr, "%s(): pin number should be less than "
            "%d, yours is %d \n", __func__, SOC_GPIO_PINS, pin);
        exit(1);
    }

    // Check if pin has been initialized 
    if (g_gpio_pin_set[pin] != HIGH) {
        fprintf(stderr, "%s(): please initialize pin %d first "
            "using pinMode() function \n",__func__, pin);
        exit(1);
    }


    switch (g_pin_hdw[pin]) {
        case ALL_HDW:
            buf = (val)?'1':'0';
            write(g_pin_fd[pin], &buf, sizeof(buf));
            break;
        case RPI_HDW:
            if (val) { // value == HIGH
                offset = OFFSET_SET + (pin / 32);
            } else {   // value == LOW
                offset = OFFSET_CLR + (pin / 32);
            }
            *(gpio_map+offset) = 1 << pin % 32;
            break;
        default:
            fprintf(stderr, "%s(): Hardware mode %d for pin %d "
                "is not valid. \n",__func__, g_pin_hdw[pin], pin);
            exit(1);
            break;
    }

}

// Returns the value of a pin (gpio) input (1 or 0)
int digitalRead(uint8_t pin)
{
    int offset, value, mask;
    char buf;

    // Check if the pin number is valid
    if (pin >= SOC_GPIO_PINS) {
        fprintf(stderr, "%s(): pin number should be less than "
            "%d, yours is %d \n", __func__, SOC_GPIO_PINS, pin);
        exit(1);
    }

    // Check if pin has been initialized 
    if (g_gpio_pin_set[pin] != HIGH) {
        fprintf(stderr, "%s(): please initialize pin %d first "
            "using pinMode() function \n",__func__, pin);
        exit(1);
    }

    switch (g_pin_hdw[pin]) {
        case ALL_HDW:
            lseek(g_pin_fd[pin],0,SEEK_SET);
            read(g_pin_fd[pin], &buf, 1);
            value = (buf == '1')? 1 : 0;
            break;
        case RPI_HDW:
            offset = OFFSET_PINLEVEL + (pin/32);
            mask = (1 << pin%32);
            value = *(gpio_map+offset) & mask;
            break;
        default:
            fprintf(stderr, "%s(): Hardware mode %d for pin %d "
                "is not valid. \n",__func__, g_pin_hdw[pin], pin);
            exit(1);
            break;
    }

    return (value) ? HIGH : LOW;
}

/////////////////////////////////////////////
//          Analog I/O                    //
////////////////////////////////////////////

// Arguments for Soft PWM threads
struct ThreadSoftPwmArg {
    uint32_t frequency;
    uint32_t dutycycle;
    uint32_t pin;
};

pthread_t idSoftPwmThread[SOC_GPIO_PINS];

// This is function will be running in a thread if
// non-blocking tone() is called.
void * softPwmThreadFunction(void *args)
{
    ThreadSoftPwmArg *arguments = (ThreadSoftPwmArg *)args;
    uint32_t pin = arguments->pin;
    float frequency = arguments->frequency;
    float dutycycle = arguments->dutycycle;
    free(args);

    if (frequency == 0) return NULL;
    float period = 1.0/frequency; // in seconds
    float highTime = (period / ( ((PWM_DUTYCYCLE_RESOLUTION-1) / dutycycle) )) ;
    float lowTime = period - highTime;

    while (1) {
        digitalWrite(pin, LOW);
        delayMicroseconds((unsigned int)(lowTime*1000000));
        digitalWrite(pin, HIGH);
        delayMicroseconds((unsigned int)(highTime*1000000));
    }

    return NULL;
}


// Changes the duty Cycle of the PWM
// Default frequency is Arduino's default frequency (490Hz).
void analogWrite(uint8_t pin, uint32_t value) 
{
    pthread_t *threadId;
    struct ThreadSoftPwmArg *threadArgs;
    int pwm_channel = 0;

    // Check if pin has been initialized
    if (g_pwm_pin_set[pin] != HIGH) {
        fprintf(stderr, "%s(): please initialize pin %d first "
            "using pinMode() function \n",__func__, pin);
        exit(1);
    }

    // Check if duty cycle resolution match
    if (value >= PWM_DUTYCYCLE_RESOLUTION) {
        fprintf(stderr, "%s(): dutycycle %d should be less than the "
            "max pwm resolution = %d \n",
            __func__, value, PWM_DUTYCYCLE_RESOLUTION);
         exit(1);
    }

    // Save PWM0 Duty Cycle Value
    g_pwm_dutycycle_value[pin] = value;

    switch (g_pin_hdw[pin]) {
        case ALL_HDW:
            // Cancel any existent threads for the pwm pin
            threadId = &idSoftPwmThread[SOC_GPIO_PINS];
            if (*threadId != 0) {
                pthread_cancel(*threadId);
            }
            // Set Soft PWM
            if (value == 0) {
                digitalWrite(pin, LOW);
            } else if (value == (PWM_DUTYCYCLE_RESOLUTION-1)) {
                digitalWrite(pin, HIGH);
            } else {
                threadArgs = (ThreadSoftPwmArg *)malloc(sizeof(ThreadSoftPwmArg));
                threadArgs->pin = pin;
                threadArgs->frequency = PWM_DEFAULT_FREQUENCY; // Default Arduino frequency
                threadArgs->dutycycle = g_pwm_dutycycle_value[pin];

                // Start Soft PWM 
                pthread_create (threadId, NULL, softPwmThreadFunction, (void *)threadArgs);
            }
            break;
        case RPI_HDW:
            // Check if the pin is valid for PWM and assign its channel
            switch (pin) {
                case 12: pwm_channel = 0; break;
                case 13: pwm_channel = 1; break;
                case 18: pwm_channel = 0; break;
                case 19: pwm_channel = 1; break;
                case 40: pwm_channel = 0; break;
                case 41: pwm_channel = 1; break;
                case 45: pwm_channel = 1; break;
                case 52: pwm_channel = 0; break;
                case 53: pwm_channel = 1; break;
                default:
                    fprintf(stderr, "%s(): pin %d can not be assigned for "
                     "analogWrite() with PWM_OUTPUT mode\n",__func__, pin);
                    exit(1);
                    break;
            }

            // Set PWM0 Duty Cycle Value
            if (pwm_channel == 1) {
                *(pwm_map + PWM1_DATA) = g_pwm_dutycycle_value[pin];
            } else {
                *(pwm_map + PWM0_DATA) = g_pwm_dutycycle_value[pin];
            }
            break;
        default:
            fprintf(stderr, "%s(): Hardware mode %d for pin %d "
                "is not valid. \n",__func__, g_pin_hdw[pin], pin);
            exit(1);
            break;
    }
}

// Does the same as anaogWrite but the function name makes more sense.
void setPwmDutyCycle (uint8_t pin, uint32_t dutycycle)
{
    analogWrite(pin, dutycycle);
}

void setPwmPeriod (uint8_t pin, uint32_t microseconds) 
{
    setPwmFrequency(pin, (1000000 / microseconds), g_pwm_dutycycle_value[pin]);
}

void setPwmFrequency (uint8_t pin, uint32_t frequency) 
{
    setPwmFrequency(pin, frequency, g_pwm_dutycycle_value[pin]);
}

// Sets PWM frequency (in Hertz) and pwm duty cycle
void setPwmFrequency (uint8_t pin, uint32_t frequency, uint32_t dutycycle) 
{
    pthread_t *threadId;
    struct ThreadSoftPwmArg *threadArgs;
    int pwm_channel = 0;
    int divisor;
    double period;
    double countDuration;

    if (g_pwm_pin_set[pin] != HIGH) {
        fprintf(stderr, "%s(): please initialize pin %d first "
            "using pinMode() function \n",__func__, pin);
        exit(1);
    }

    // Check if duty cycle resolution match
    if (dutycycle >= PWM_DUTYCYCLE_RESOLUTION) {
        fprintf(stderr, "%s(): duty cycle %d should be less than the "
            "max pwm resolution = %d \n",
            __func__, dutycycle, PWM_DUTYCYCLE_RESOLUTION);
        exit(1);
    }

    // Save PWM0 Duty Cycle Value
    g_pwm_dutycycle_value[pin] = dutycycle;

    switch (g_pin_hdw[pin]) {
        case ALL_HDW:
            // Cancel any existent threads for the pwm pin
            threadId = &idSoftPwmThread[SOC_GPIO_PINS];
            if (*threadId != 0) {
                pthread_cancel(*threadId);
            }
            // Set Soft PWM
            if (dutycycle == 0) {
                digitalWrite(pin, LOW);
            } else if (dutycycle == (PWM_DUTYCYCLE_RESOLUTION-1)) {
                digitalWrite(pin, HIGH);
            } else {
                threadId = &idSoftPwmThread[SOC_GPIO_PINS];
                threadArgs = (ThreadSoftPwmArg *)malloc(sizeof(ThreadSoftPwmArg));
                threadArgs->pin = pin;
                threadArgs->frequency = frequency;
                threadArgs->dutycycle = g_pwm_dutycycle_value[pin];

                // Start Soft PWM 
                pthread_create (threadId, NULL, softPwmThreadFunction, (void *)threadArgs);
            }
            break;
        case RPI_HDW:
            // Check if the pin is valid for PWM and assign its channel
            switch (pin) {
                case 12: pwm_channel = 0; break;
                case 13: pwm_channel = 1; break;
                case 18: pwm_channel = 0; break;
                case 19: pwm_channel = 1; break;
                case 40: pwm_channel = 0; break;
                case 41: pwm_channel = 1; break;
                case 45: pwm_channel = 1; break;
                case 52: pwm_channel = 0; break;
                case 53: pwm_channel = 1; break;
                default:
                    fprintf(stderr, "%s(): pin %d can not be assigned for "
                        "this function with PWM_OUTPUT mode \n",__func__, pin);
                    exit(1);
                    break;
            }

            // -- Set frequency and duty cycle

            // stop clock and waiting for busy flag doesn't work, so kill clock
            *(clk_map + PWMCLK_CNTL) = 0x5A000000 | 0x01;
            usleep(10);

            // wait until busy flag is set 
            while ( (*(clk_map + PWMCLK_CNTL)) & 0x80);

            //calculate divisor value for PWM1 clock...base frequency is 19.2MHz
            period = 1.0/frequency; 
            countDuration = period/(PWM_DUTYCYCLE_RESOLUTION*1.0f);
            divisor = (int)(19200000.0f / (1.0/countDuration));

            if( divisor < 0 || divisor > 4095 ) {
                fprintf(stderr, "%s(): pwm frequency %d with pwm duty cycle "
                    "resolution/range of %d bits not supported \n",__func__,
                     frequency, PWM_DUTYCYCLE_RESOLUTION);
                exit(1);
            }

            // Set divisor
            *(clk_map + PWMCLK_DIV) = 0x5A000000 | (divisor << 12);

            // source=osc and enable clock
            *(clk_map + PWMCLK_CNTL) = 0x5A000011;

            // Set PWM0 Duty Cycle Value
            if (pwm_channel == 1) {
                *(pwm_map + PWM1_DATA) = g_pwm_dutycycle_value[pin];
            } else {
                *(pwm_map + PWM0_DATA) = g_pwm_dutycycle_value[pin];
            }
            break;
        default:
            fprintf(stderr, "%s(): Hardware mode %d for pin %d "
                "is not valid. \n",__func__, g_pin_hdw[pin], pin);
            exit(1);
            break;
    }
}


/////////////////////////////////////////////
//          Advanced I/O                  //
////////////////////////////////////////////

// Arguments for Tone threads
struct ThreadToneArg {
    int pin;
    unsigned long duration;
};

pthread_t idToneThread[SOC_GPIO_PINS];

// This is function will be running in a thread if
// non-blocking tone() is called.
void * toneThreadFunction(void *args)
{
    ThreadToneArg *arguments = (ThreadToneArg *)args;
    int pin = arguments->pin;
    unsigned long duration = arguments->duration;
    free(args);

    usleep(duration*1000);
    noTone(pin);

    return (NULL);
}


// Set tone frequency (in hertz) and duration (in milliseconds)
void tone(uint8_t pin, uint32_t frequency, unsigned long duration, uint32_t block)
{
    pthread_t *threadId;
    struct ThreadToneArg *threadArgs;

    // Set frequency at 50% duty cycle
    setPwmFrequency(pin, frequency, (PWM_DUTYCYCLE_RESOLUTION-1) / 2);

    // Tone duration: If duration == 0, don't stop the tone, 
    // else perform duration either blocking or non-blocking
    if (duration == 0) {
        return;
    } else {

        threadId = &idToneThread[SOC_GPIO_PINS];
        threadArgs = (ThreadToneArg *)malloc(sizeof(ThreadToneArg));
        threadArgs->pin = pin;
        threadArgs->duration = duration;

        // Cancel any existent threads for the pwm pin
        if (*threadId != 0) {
            pthread_cancel(*threadId);
        }

        // If block == true  stop the tone after a sleep delay
        // If block == false then start a thread that will stop the tone
        // after certain duration and parallely continue with the rest of the func. 
        if  (block) {
            usleep(duration*1000);
            noTone(pin);
        } else {
            pthread_create (threadId, NULL, toneThreadFunction, (void *)threadArgs);
        }
    }
}

void noTone(uint8_t pin) {
    analogWrite(pin, 0);
}

uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder) 
{
    uint8_t value = 0;
    uint8_t i;

    for (i = 0; i < 8; ++i) {
        digitalWrite(clockPin, HIGH);
        if (bitOrder == LSBFIRST)
            value |= digitalRead(dataPin) << i;
        else
            value |= digitalRead(dataPin) << (7 - i);
        digitalWrite(clockPin, LOW);
    }
    return value;
}


void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val)
{
    uint8_t i;

    for (i = 0; i < 8; i++)  {
        if (bitOrder == LSBFIRST)
            digitalWrite(dataPin, !!(val & (1 << i)));
        else    
            digitalWrite(dataPin, !!(val & (1 << (7 - i))));
            
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);        
    }
}

// Measures the length (in microseconds) of a pulse on the pin; state is HIGH
// or LOW, the type of pulse to measure. timeout is 1 second by default.
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);

    // wait for any previous pulse to end
    while (digitalRead(pin) == state) {
        clock_gettime(CLOCK_REALTIME, &end);
        // timeDiffmicros > timeout?
        if (((end.tv_sec - start.tv_sec) * 1e6  + 
            (end.tv_nsec - start.tv_nsec) * 1e-3) > timeout)
            return 0;
    }

    // wait for the pulse to start
    while (digitalRead(pin) != state) {
        clock_gettime(CLOCK_REALTIME, &end);
         // timeDiffmicros > timeout?
        if (((end.tv_sec - start.tv_sec) * 1e6  + 
            (end.tv_nsec - start.tv_nsec) * 1e-3) > timeout)
            return 0;
    }

    clock_gettime(CLOCK_REALTIME, &start);
    // wait for the pulse to stop
    while (digitalRead(pin) == state) {
        clock_gettime(CLOCK_REALTIME, &end);
         // timeDiffmicros > timeout?
        if (((end.tv_sec - start.tv_sec) * 1e6  + 
            (end.tv_nsec - start.tv_nsec) * 1e-3) > timeout)
            return 0;
    }

    // return microsecond elapsed
    return ((end.tv_sec - start.tv_sec) * 1e6  + 
            (end.tv_nsec - start.tv_nsec) * 1e-3);
}


/////////////////////////////////////////////
//          Time                          //
////////////////////////////////////////////

// Returns the time in milliseconds since the program started.
unsigned long millis(void) 
{
    struct timespec timenow, start, end;
    clock_gettime(CLOCK_REALTIME, &timenow);
    start = Arduino.timestamp;
    end = timenow;
    // timeDiffmillis:
    return ((end.tv_sec - start.tv_sec) * 1e3 + (end.tv_nsec - start.tv_nsec) * 1e-6);
}

// Returns the time in microseconds since the program started.
unsigned long micros(void)
{
    struct timespec timenow, start, end;
    clock_gettime(CLOCK_REALTIME, &timenow);
    start = Arduino.timestamp;
    end = timenow;
    // timeDiffmicros
    return ((end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) * 1e-3);
}

// Sleep the specified milliseconds
void delay(unsigned long millis)
{
    usleep(millis*1000);
}

// Sleep the specified microseconds
void delayMicroseconds(unsigned int us)
{
    usleep(us);
}

/////////////////////////////////////////////
//          Math                          //
////////////////////////////////////////////

double min(double a, double b) 
{ 
  return ((a)<(b) ? (a) : (b)); 
}

int min(int a, int b) 
{ 
  return ((a)<(b) ? (a) : (b)); 
}

double max(double a, double b)
{ 
  return ((a)>(b) ? (a) : (b)); 
}

int max(int a, int b)
{ 
  return ((a)>(b) ? (a) : (b)); 
}

double abs_js(double x)
{
  return abs(x);
}

double constrain(double amt, double low, double high) 
{ 
  return ((amt)<(low)?(low):((amt)>(high)?(high):(amt))); 
}

int constrain(int amt, int low, int high) 
{ 
  return ((amt)<(low)?(low):((amt)>(high)?(high):(amt))); 
}

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

double pow_js(double base, double exponent) 
{
  return pow(base, exponent);
}

double sqrt_js(double x) 
{
  return sqrt(x);
}

double round_js(double x) 
{ 
  return ((x)>=0?(long)((x)+0.5):(long)((x)-0.5)); 
}

double radians(double deg) 
{ 
  return ((deg)*DEG_TO_RAD); 
}

double degrees(double rad) 
{ 
  return ((rad)*RAD_TO_DEG); 
}

double sq(double x) 
{ 
  return ((x)*(x)); 
}

int sq(int x) 
{ 
  return ((x)*(x)); 
}

/////////////////////////////////////////////
//          Trigonometry                  //
////////////////////////////////////////////

double sin_js(double rad) 
{
  return sin(rad);
}

double cos_js(double rad) 
{
  return cos(rad);
}

double tan_js(double rad) 
{
  return tan(rad);
}

/////////////////////////////////////////////
//          Characters                    //
////////////////////////////////////////////

// Checks for an alphanumeric character. 
// It is equivalent to (isalpha(c) || isdigit(c)).
boolean isAlphaNumeric(int c) 
{
  return ( isalnum(c) == 0 ? false : true);
}

// Checks for an alphabetic character. 
// It is equivalent to (isupper(c) || islower(c)).
boolean isAlpha(int c)
{
  return ( isalpha(c) == 0 ? false : true);
}

// Checks whether c is a 7-bit unsigned char value 
// that fits into the ASCII character set.
boolean isAscii(int c)
{
  return ( isascii (c) == 0 ? false : true);
}

// Checks for a blank character, that is, a space or a tab.
boolean isWhitespace(int c)
{
  return ( isblank (c) == 0 ? false : true);
}

// Checks for a control character.
boolean isControl(int c)
{
  return ( iscntrl (c) == 0 ? false : true);
}

// Checks for a digit (0 through 9).
boolean isDigit(int c)
{
  return ( isdigit (c) == 0 ? false : true);
}

// Checks for any printable character except space.
boolean isGraph(int c)
{
  return ( isgraph (c) == 0 ? false : true);
}

// Checks for a lower-case character.
boolean isLowerCase(int c)
{
  return (islower (c) == 0 ? false : true);
}

// Checks for any printable character including space.
boolean isPrintable(int c)
{
  return ( isprint (c) == 0 ? false : true);
}

// Checks for any printable character which is not a space 
// or an alphanumeric character.
boolean isPunct(int c)
{
  return ( ispunct (c) == 0 ? false : true);
}

// Checks for white-space characters. For the avr-libc library, 
// these are: space, formfeed ('\f'), newline ('\n'), carriage 
// return ('\r'), horizontal tab ('\t'), and vertical tab ('\v').
boolean isSpace(int c)
{
  return ( isspace (c) == 0 ? false : true);
}

// Checks for an uppercase letter.
boolean isUpperCase(int c)
{
  return ( isupper (c) == 0 ? false : true);
}

// Checks for a hexadecimal digits, i.e. one of 0 1 2 3 4 5 6 7 
// 8 9 a b c d e f A B C D E F.
boolean isHexadecimalDigit(int c)
{
  return ( isxdigit (c) == 0 ? false : true);
}

// Converts c to a 7-bit unsigned char value that fits into the 
// ASCII character set, by clearing the high-order bits.
int toAscii(int c)
{
  return toascii (c);
}

// Converts the letter c to lower case, if possible.
int toLowerCase(int c)
{
  return tolower (c);
}

// Converts the letter c to upper case, if possible.
int toUpperCase(int c)
{
  return toupper (c);
}

/////////////////////////////////////////////
//          Random Functions              //
////////////////////////////////////////////

void randomSeed(unsigned long seed)
{
    if (seed != 0) {
        srandom(seed);
    }
}

long random(long howbig)
{
    if (howbig == 0) {
        return 0;
    }
    return random() % howbig;
}

long random(long howsmall, long howbig)
{
  if (howsmall >= howbig) {
    return howsmall;
  }
  long diff = howbig - howsmall;
  return random(diff) + howsmall;
}

/////////////////////////////////////////////
//          Bits and Bytes                //
////////////////////////////////////////////

uint8_t lowByte(uint16_t w) 
{ 
  return ((uint8_t) ((w) & 0xff)); 
}

uint8_t highByte(uint16_t w) 
{ 
  return ((uint8_t) ((w) >> 8)); 
}

uint32_t bitRead(uint32_t value, uint32_t bit) 
{ 
  return (((value) >> (bit)) & 0x01); 
}

uint32_t bitWrite(uint32_t value, uint32_t bit, uint32_t bitvalue)
{ 
  return (bitvalue ? bitSet(value, bit) : bitClear(value, bit)); 
}

uint32_t bitSet(uint32_t value, uint32_t bit) 
{ 
  return ((value) |= (1UL << (bit))); 
} 

uint32_t bitClear(uint32_t value, uint32_t bit) 
{ 
  return ((value) &= ~(1UL << (bit))); 
}

uint32_t bit(uint32_t b) 
{ 
  return (1UL << (b)); 
}

/////////////////////////////////////////////
//          External Interrupts           //
////////////////////////////////////////////

// Arguments for External Interrupt threads
struct ThreadExtArg {
    void (*func)();
    int pin;
};

pthread_t idExtThread[SOC_GPIO_PINS];

// This is the function that will be running in a thread if
// attachInterrupt() is called 
void * threadFunction(void *args)
{
    ThreadExtArg *arguments = (ThreadExtArg *)args;
    int pin = arguments->pin;
    void (*func)();
    func = arguments->func;
    free(args);
    
    int GPIO_FN_MAXLEN = 32;
    int RDBUF_LEN = 5;
    
    char fn[GPIO_FN_MAXLEN];
    int fd,ret;
    struct pollfd pfd;
    char rdbuf [RDBUF_LEN];
    
    memset(rdbuf, 0x00, RDBUF_LEN);
    memset(fn,0x00,GPIO_FN_MAXLEN);
    
    snprintf(fn, GPIO_FN_MAXLEN-1, "/sys/class/gpio/gpio%d/value",pin);
    fd=open(fn, O_RDONLY);
    if (fd<0) {
        fprintf(stderr, "%s(): gpio error: %s\n", __func__, strerror (errno));
        exit(1);
    }
    pfd.fd=fd;
    pfd.events=POLLPRI;
    
    ret=read(fd,rdbuf,RDBUF_LEN-1);
    if (ret<0) {
        fprintf(stderr, "%s(): gpio error: %s\n", __func__, strerror (errno));
        exit(1);
    }
    
    while(1) {
        memset(rdbuf, 0x00, RDBUF_LEN);
        lseek(fd, 0, SEEK_SET);
        ret=poll(&pfd, 1, -1);
        if (ret<0) {
            fprintf(stderr, "%s(): gpio error: %s\n", __func__, strerror (errno));
            close(fd);
            exit(1);
        }
        if (ret==0) {
            // Timeout
            continue;
        }
        ret=read(fd,rdbuf,RDBUF_LEN-1);
        if (ret<0) {
            fprintf(stderr, "%s(): gpio error: %s\n", __func__, strerror (errno));
            exit(1);
        }
        //Interrupt. We call user function.
        func();
    }
}

void attachInterrupt(uint8_t pin, void (*f)(void), int mode)
{
    pthread_t *threadId = &idExtThread[pin];
    struct ThreadExtArg *threadArgs = (ThreadExtArg *)malloc(sizeof(ThreadExtArg));
    threadArgs->func = f;
    threadArgs->pin = pin;
    FILE *fp;

    // Return if the interrupt pin number is out of range
    // NOT_AN_INTERRUPT is set when digitalPinToInterrupt(p) is used for an invalid pin
    if (pin == (uint8_t) NOT_AN_INTERRUPT) {
        fprintf(stderr, "%s(): interrupt pin number out of range\n",__func__);
        return;
    }

    // Check if the pin number is valid
    if (pin >= SOC_GPIO_PINS) {
        fprintf(stderr, "%s(): pin number should be less than "
            "%d, yours is %d \n", __func__, SOC_GPIO_PINS, pin);
        exit(1);
    }

    // Tell the system to create the file /sys/class/gpio/gpio<GPIO number>
    char * interruptFile = NULL;
    asprintf(&interruptFile, "/sys/class/gpio/gpio%d/edge",pin);

    if (access(interruptFile, F_OK) == -1) {
        // Export pin for interrupt
        fp = fopen("/sys/class/gpio/export","r+");
        if (fp == NULL) {
            fprintf(stderr, "%s(): export gpio error: %s\n",__func__, strerror (errno));
            exit(1);
        } else {
            fprintf(fp,"%d",pin); 
        }
        fclose(fp);
    }
    
    //Set detection edge condition
    fp = fopen(interruptFile,"r+");
    if (fp == NULL) {
        // First time may fail because the file may not be ready.
        // if that the case then we wait two seconds and try again.
        sleep(2);
        fp = fopen(interruptFile,"r+");
        if (fp == NULL) {
            fprintf(stderr, "%s(): set gpio edge interrupt of (%s) error: %s\n",
                __func__, interruptFile, strerror (errno));
            exit(1);
        }
    }
    switch(mode) {
        case RISING: fprintf(fp,"rising");break;
        case FALLING: fprintf(fp,"falling");break;
        default: fprintf(fp,"both");break;  // Change
    }
    fclose(fp);
    
    // Cancel any existent threads for the interrupt pin
    if (*threadId != 0) {
        pthread_cancel(*threadId);
    }

    // Create a thread passing the pin, function and mode
    pthread_create (threadId, NULL, threadFunction, (void *)threadArgs);
}

void detachInterrupt(uint8_t pin)
{
    pthread_t *threadId = &idExtThread[pin];

    // Return if the interrupt pin number is out of range
    // NOT_AN_INTERRUPT is set when digitalPinToInterrupt(p) is used for an invalid pin
    if (pin == (uint8_t) NOT_AN_INTERRUPT) {
        fprintf(stderr, "%s(): interrupt pin number out of range\n",__func__);
        return;
    }

    // Check if the pin number is valid
    if (pin >= SOC_GPIO_PINS) {
        fprintf(stderr, "%s(): pin number should be less than "
            "%d, yours is %d \n", __func__, SOC_GPIO_PINS, pin);
        exit(1);
    }

    // Cancel Thread
    pthread_cancel(*threadId);

    // Unexport gpio pin
    FILE *fp = fopen("/sys/class/gpio/unexport","r+");
    if (fp == NULL) {
        fprintf(stderr, "%s(): unexport gpio error: %s\n",__func__, strerror (errno));
        exit(1);
    } else {
        fprintf(fp,"%d",pin); 
    }
    fclose(fp);
    
}


/////////////////////////////////////////////
//    Extra Arduino Functions for Linux   //
////////////////////////////////////////////

void (*ARDUINO_EXIT_FUNC)(void) = NULL;

// Every time an arduino program is ran it executes the following functions.
ArduinoLinux::ArduinoLinux()
{
    // Gets a timestamp when the program starts
    clock_gettime(CLOCK_REALTIME, &timestamp);

    // Set a callback function to detect when program is closed.
    // This is to turn off any gpio and pwm running.
#ifdef __EMSCRIPTEN__

    EM_ASM(
        // Check if FS was included during compilation
        if (typeof FS.mkdir === "function") {
            // Include sys and devices to NODEFS FileSystem
            FS.mkdir('/sys');
            FS.mount(NODEFS, { root: '/sys' }, '/sys');
            // TODO: there is an error when adding /dev to
            // NODEFS FileSystem, for now adding this folder
            // as /devices
            FS.mkdir('/devices');
            FS.mount(NODEFS, { root: '/dev' }, '/devices');
        }
    );

    // TODO add SIGNALS to node
    // Currenlty node can't process signals when there are loops in node:
    // https://github.com/nodejs/node/issues/9050
    // node_signal();

#else

    if (signal(SIGINT, ArduinoLinux::onArduinoExit) == SIG_ERR)  // Ctrl^C
        fprintf(stderr, "%s(): can't catch signal SIGINT: %s\n",__func__, strerror (errno));
    if (signal(SIGHUP, ArduinoLinux::onArduinoExit) == SIG_ERR)  // Terminal closes
        fprintf(stderr, "%s(): can't catch signal SIGHUP: %s\n",__func__, strerror (errno));
    // if (signal(SIGTERM, ArduinoLinux::onArduinoExit) == SIG_ERR) // Kill command
    //     fprintf(stderr, "%s(): can't catch signal SIGKILL: %s\n",__func__, strerror (errno));

#endif

}


// Catch Ctrl^C (SIGINT) and kill (SIGKILL) signals to set gpio and pwm to default state
void ArduinoLinux::onArduinoExit(int signumber)
{
    int i;

    // Shut down 
    if (signumber == SIGINT || signumber == SIGTERM ||  signumber == SIGHUP) {

        // If user wants to call a function at the end, here he can call it. 
        // He can exit so the rest of the code don't take place.
        if (ARDUINO_EXIT_FUNC != NULL) {
            // Call User exit func
            (*ARDUINO_EXIT_FUNC)();
        } else {

            // At exit set PWM and GPIO set pins to default state (INPUT)
            for (i=0; i<SOC_GPIO_PINS; i++) {
                if (g_gpio_pin_set[i] == HIGH || g_pwm_pin_set[i] == HIGH) {
                    switch (g_pin_hdw[i]) {
                    case ALL_HDW:
                        pinMode(i, INPUT);
                        break;
                    case RPI_HDW:
                        pinMode(i, RPI_INPUT);
                        break;
                    default:
                        break;
                    }                      
                }
            }

            exit(0);
        }
    }
}

ArduinoLinux Arduino = ArduinoLinux();

/////////////////////////////////////////////
//    Embind Core functions               //
////////////////////////////////////////////
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_BINDINGS(Core) {
        constant("LINUXDUINO_VERSION", std::string(LINUXDUINO_VERSION));
        constant("SOC_GPIO_PINS", SOC_GPIO_PINS);
        // Digital I/O
        constant("HIGH", HIGH);
        constant("LOW", LOW);
        constant("INPUT", INPUT);
        constant("OUTPUT", OUTPUT);
        constant("INPUT_PULLUP", INPUT_PULLUP);
        constant("INPUT_PULLDOWN", INPUT_PULLDOWN);
            // TODO: RPI_... constants
        function("pinMode", &pinMode);
        function("digitalWrite", &digitalWrite);
        function("digitalRead", &digitalRead);
        // Analog I/O
        constant("PWM_DUTYCYCLE_RESOLUTION", PWM_DUTYCYCLE_RESOLUTION);
        constant("PWM_DEFAULT_FREQUENCY", PWM_DEFAULT_FREQUENCY);
        function("analogWrite", &analogWrite);
        function("setPwmDutyCycle", &setPwmDutyCycle);
        function("setPwmPeriod", &setPwmPeriod);
        function("setPwmFrequency", select_overload<void(uint8_t, uint32_t)>(&setPwmFrequency));
        function("setPwmFrequency", select_overload<void(uint8_t, uint32_t, uint32_t)>(&setPwmFrequency));
        // Advanced I/O
        constant("LSBFIRST", LSBFIRST);
        constant("MSBFIRST", MSBFIRST);
        function("tone", &tone);
        function("noTone", &noTone);
        function("shiftIn", &shiftIn);
        function("shiftOut", &shiftOut);
        function("pulseIn", &pulseIn);
        // Time
        function("millis", &millis);
        function("micros", &micros);
        function("delay", &delay);
        function("delayMicroseconds", &delayMicroseconds);
        // Math
        constant("PI", PI);
        constant("HALF_PI", HALF_PI);
        constant("TWO_PI", TWO_PI);
        constant("DEG_TO_RAD", DEG_TO_RAD);
        constant("RAD_TO_DEG", RAD_TO_DEG);
        constant("EULER", EULER);
        function("min", select_overload<double(double,double)>(&min));
        function("max", select_overload<double(double,double)>(&max));
        function("abs", &abs_js);
        function("constrain", select_overload<double(double,double,double)>(&constrain));
        function("map", &map);
        function("pow", &pow_js);
        function("sqrt", &sqrt_js);
        function("round", &round_js);
        function("radians", &radians);
        function("degrees", &degrees);
        function("sq", select_overload<double(double)>(&sq));
        // Trigonometry  
        function("sin", &sin_js);
        function("cos", &cos_js);
        function("tan", &tan_js);
        // Characters
        function("isAlphaNumeric", &isAlphaNumeric);
        function("isAlpha", &isAlpha);
        function("isAscii", &isAscii);
        function("isWhitespace", &isWhitespace);
        function("isControl", &isControl);
        function("isDigit", &isDigit);
        function("isGraph", &isGraph);
        function("isLowerCase", &isLowerCase);
        function("isPrintable", &isPrintable);
        function("isPunct", &isPunct);
        function("isSpace", &isSpace);
        function("isUpperCase", &isUpperCase);
        function("isHexadecimalDigit", &isHexadecimalDigit);
        function("toAscii", &toAscii);
        function("toLowerCase", &toLowerCase);
        function("toUpperCase", &toUpperCase);
        // Random Functions  
        function("randomSeed", &randomSeed);
        function("random",  select_overload<long(long)>(&random));
        function("random",  select_overload<long(long, long)>(&random));
        // Bits and Bytes
        function("lowByte", &lowByte);
        function("highByte", &highByte);
        function("bitRead", &bitRead);
        function("bitWrite", &bitWrite);
        function("bitSet", &bitSet);
        function("bitClear", &bitClear);
        function("bit", &bit);
        // External Interrupts 
            // TODO
        // Extra Arduino Functions for Linux 
            // TODO
    }

#endif


