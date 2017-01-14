//////////////////////////////////////////////////////////////////////////
// empiriKit.h for Freescale FRDM-KL25Z board
//
// Author:  Ingemar Larsson
// Date:    2013-08-19
// Version: 0.25
// License: TODO decide on license
//
//////////////////////////////////////////////////////////////////////////
#include "USBSerial.h"  // Virtual serial port
#include "TSISensor.h"  // Touch sensor
#include "MMA8451Q.h"   // Accelerometer

#if !defined(MIN)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#if !defined(MAX)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// Accelerometer
// Move the rest of the accel extra code (see patch) to this file
// (or the app at least)
#if defined(TARGET_KL25Z) | defined(TARGET_KL46Z)
#define MMA8451_I2C_ADDRESS (0x1d<<1)
MMA8451Q acc(PTE25, PTE24);
int16_t *accLog = 0;
int16_t *accLogPtr;
int16_t accXYZ[3];
int accLoggedDataLength = 0;
int _accelerometerRange = 8;
int accelerometerStreaming = 0;
#endif

// Touch sensor
int16_t touchValue = 0;
int touchStreaming = 0;
TSISensor tsi;

// Communication
int sendNotifications = 0;

#define DEFAULT_SAMPLING_RATE 50 // Sampling rate in Hz
#define ACC_LOG_LENGTH (DEFAULT_SAMPLING_RATE*20) // Allow 20s sampling log for 50 Hz
#define ACC_LOG_SIZE (DEFAULT_SAMPLING_RATE*20*3) // Allow 20s sampling log for 50 Hz
#define SAMPLING_WAIT (1000/DEFAULT_SAMPLING_RATE)
#define SAMPLING_WAIT_US (1000*SAMPLING_WAIT)

int _stream_sampling_rate = DEFAULT_SAMPLING_RATE;
int _stream_sampling_wait_us = SAMPLING_WAIT_US;

enum STATE_TYPE
{
    IDLE_STATE,
    LOG_ACC_STATE,
    ACC_READY_STATE,
    STREAM_TOUCH_STATE,
    STREAM_ACC_STATE,
    GET_INFO_STATE,
    GET_HELP_STATE,
    GET_LOG_STATE,
};

const char versionString[] = "14.06.001";

char helpString[] =
    "{\"message\":["
    "\"CMD => Description\","
    "\"GETINF => Get hardware and firmware information, ({'GETINF':1})\","
#if defined(TARGET_KL25Z)
    "\"SETRGB => Set LED RGB color, e.g. send {'SETRGB':[255,0,0]}\","
#elif defined(TARGET_KL46Z)
    "\"SETLCD => Set LCD string, e.g. send {'SETLCD':'1234'}\","
#endif
    "\"NOTIFY => Send state change notifications ({'NOTIFY':x}, x = 0(off) or 1(on))\","
    "\"SETRTE => Set sampling rate ({'SETRTE':x}, 1 <= x <= 100)\","
    "\"STRTCH => Stream touch values ({'STRTCH':x}, x = 0(off) or 1(on))\","
    "\"STRACC => Stream accelerometer values ({'STRTCH':x}, x = 0(off) or 1(on))\","
    "\"LOGACC => Start logging accelerometer data ({'LOGACC':1})\","
    "\"GETLOG => Get logged accelerometer data, ({'GETLOG':1})\","
    "\"Visit www.empirikit.com for more information.\"]}";


STATE_TYPE currentState;

