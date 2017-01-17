/*
* Copyright 2017 Ingemar Larsson & Lars Gunder Knudsen / empiriKit
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/


#include "mbed.h"

#include "empirikit.h"
#include "WebUSBCDC.h"

#if defined(TARGET_KL46Z)
#include "SLCD.h"

SLCD lcd;
char lcdMessage[40];
#endif

#if defined(TARGET_KL25Z)
PwmOut rled(LED_RED);
PwmOut gled(LED_GREEN);
PwmOut bled(LED_BLUE);

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
    rled = 1.0 - ((float)r/255.0f);
    gled = 1.0 - ((float)g/255.0f);
    bled = 1.0 - ((float)b/255.0f);
}
#endif

void setStreamSamplingRate(int rate) {
    if (rate < 1 || rate > 100)
        return;

    _stream_sampling_rate = rate;
    _stream_sampling_wait_us = (1000000 / rate);
}

// Communication
WebUSBCDC webUSB(0x1209, 0x0001, 0x0001, true);

uint8_t* rbuf;
uint32_t rbuf_len = 0;
uint32_t read_size;

uint8_t* rbuf_cdc;
uint32_t rbuf_len_cdc = 0;
uint32_t read_size_cdc;

char* sbuf;


void sendString(const char* str, bool isCDC=false) {
    int len = strlen(str);
    uint32_t byte_count;
    uint8_t* byte_ptr = (uint8_t*)str;

    while(len>0) {
        byte_count = MIN(MAX_PACKET_SIZE_EPBULK, len);
        webUSB.write(byte_ptr, byte_count);
        len-=MAX_PACKET_SIZE_EPBULK;
        byte_ptr+=byte_count;
    }
}

void sendHardwareInformation() {

    sendString("{\"datatype\":\"HardwareInfo\",\n");
#if defined(TARGET_KL25Z)
    sendString("\"devicetype\":\"empiriKit|MOTION\",\n");
#elif defined(TARGET_KL46Z)
    sendString("\"devicetype\":\"empiriKit|KL46Z\",\n");
#endif
    sprintf(sbuf,"\"version\":\"%s\",\n\"uid\":\"0x%04X%08X%08X\",\n",
        versionString,
        *((unsigned int *)0x40048058),
        *((unsigned int *)0x4004805C),
        *((unsigned int *)0x40048060));
    sendString(sbuf);
    sendString("\"capabilities\":[\n");
    sendString("\"accelerometer\",\n");
#if defined(TARGET_KL25Z)
    sendString("\"rgbled\",\n");
#elif defined(TARGET_KL46Z)
    sendString("\"magnetometer\",\n");
    sendString("\"lightsensor\",\n");
#endif
    sendString("\"touchsensor\"\n");
    sendString("]}");
}

int params[10];

void handleCMD(uint8_t* cmd_buf, uint32_t size) {
    // very crude "json" parsing. We should put e.g. picoJSON in place here

    char *cmdPtr = (char*)(&cmd_buf[2]);
    char *valPtr = (char*)(&cmd_buf[10]);

    if (strncmp(cmdPtr,"SETIDL",6) == 0){
        accelerometerStreaming = 0;
        touchStreaming = 0;
        setStreamSamplingRate(DEFAULT_SAMPLING_RATE);
        currentState = IDLE_STATE;
    } else if (strncmp(cmdPtr,"LOGACC",6) == 0){
        currentState = LOG_ACC_STATE;
    } else if (strncmp(cmdPtr,"NOTIFY",6) == 0){
        sscanf(valPtr,"%i",&sendNotifications);
#if defined(TARGET_KL25Z)
    } else if (strncmp(cmdPtr,"SETRGB",6) == 0){
        sscanf(valPtr,"[%d,%d,%d]",&params[0], &params[1], &params[2]);
        setRGB(params[0], params[1], params[2]);
#elif defined(TARGET_KL46Z)
    } else if (strncmp(cmdPtr,"SETLCD",6) == 0){
        // TODO:  Set LCD string...
#endif
    } else if (strncmp(cmdPtr,"SETRTE",6) == 0){
        sscanf(valPtr,"%i",&params[0]);
        setStreamSamplingRate(params[0]);
    } else if (strncmp(cmdPtr,"STRTCH",6) == 0){
        sscanf(valPtr,"%i",&touchStreaming);
    } else if (strncmp(cmdPtr,"STRACC",6) == 0){
        sscanf(valPtr,"%i",&accelerometerStreaming);
    } else if (strncmp(cmdPtr,"GETINF",6) == 0){
        sendHardwareInformation();
    } else if (strncmp(cmdPtr,"GETLOG",6) == 0){
        currentState = GET_LOG_STATE;
    } else {
        // send help string
        sendString(helpString);
    }
}


#define MAX_BUF_SIZE 1024

int count = 0;

int main()
{
#if defined(TARGET_KL25Z)
    rled.period(0.001);
    gled.period(0.001);
    bled.period(0.001);
    setRGB(255,0,0);
#endif


    rbuf = new uint8_t[MAX_BUF_SIZE];
    rbuf_cdc = new uint8_t[MAX_BUF_SIZE];

    sbuf = new char[200];

    accLog = new int16_t[ACC_LOG_SIZE];

    currentState = IDLE_STATE;

    // Indicate power on with green LED
    if (accLog)
        setRGB(0,255,0);
    else {
        setRGB(255,0,0);
        while(1);
    }


    // Start the timer - used for precision sampling rate
    loopTimer.reset();
    loopTimer.start();

    while (true) {
        // try to read from endpoint
        if(webUSB.read(&rbuf[rbuf_len], &read_size)) {
            sprintf(sbuf, "{\"msg\":\"Read %d bytes\"}",(int)read_size);
            sendString(sbuf);

            rbuf_len += read_size;
            if(rbuf_len+MAX_PACKET_SIZE_EPBULK >= MAX_BUF_SIZE) {
                // we are too close to the buffer limit (crude handling)
                rbuf_len = 0;
            }
            uint32_t buf_pos = 0;
            while(rbuf_len && buf_pos < rbuf_len) {
                // crude "find the '}'"
                if(rbuf[buf_pos] == '}') {
                    sprintf(sbuf, "{\"msg\":\"Found end bracket at pos: %d\"}",(int)buf_pos);
                    sendString(sbuf);

                    handleCMD(rbuf, buf_pos+1);
                    memmove(rbuf, &rbuf[buf_pos+1], rbuf_len-(buf_pos+1));
                    rbuf_len-=buf_pos+1;
                }
                buf_pos++;
            }
        }

        // Handle state
        switch (currentState) {
            case IDLE_STATE:
                // TODO add battery status monitoring, USB connected?
#if defined(XXTARGET_KL46Z)
                    sprintf(lcdMessage, "%04d", tsi.readDistance());
                    lcd.printf(lcdMessage);
#endif
                break;
            case LOG_ACC_STATE:
#if defined(TARGET_KL46Z)
                    lcd.printf("LACC");
#endif
                count = (count<3)?count+1:0;
                if (tsi.readDistance() > 20)  // Should do:  Proper swipe detection.
                    currentState = ACC_READY_STATE;
                else if (tsi.readDistance() > 0) {
#if defined(TARGET_KL25Z)
                    setRGB(0,0,tsi.readDistance() * 12);
#elif defined(TARGET_KL46Z)
                    sprintf(lcdMessage, "%04d", tsi.readDistance());
                    lcd.printf(lcdMessage);
#endif
                } else if (count == 0)
                    setRGB(0,255,0);
                else
                    setRGB(0,0,0);
                break;
            case ACC_READY_STATE:
#if defined(TARGET_KL46Z)
                    lcd.printf("ACCR");
#endif
                setRGB(255,0,0);
                // Blink red LED for 5s to indicate logging will start
                for (int i=0; i<10; i++) {
                    wait_ms(500);
#if defined(TARGET_KL25Z)
                    setRGB((i&1?0:255),0,0);
#endif
#if defined(TARGET_KL46Z)
                    sprintf(lcdMessage, "-%2ds", (10-i)>>1);
                    lcd.printf(lcdMessage);
#endif
                }
                // Constant red LED to indicate recording
                // The following line is commented out (for now) as we get a crash if we send data and are not connected.
                if (sendNotifications)
                    sendString("{\"datatype\":\"Notification\",\"data\":\"LoggingStarted\"}\n");
                setRGB(255,0,0);
                accLoggedDataLength = ACC_LOG_LENGTH*3;
                timer.reset();
                timer.start();
                accLogPtr = accLog; // Point at the beginning
#if defined(TARGET_KL46Z)
                lcd.DP2(1);
#endif
                for (int i=0; i<ACC_LOG_LENGTH; i++) {
                    acc.getAccAllAxis(accLogPtr);
                    accLogPtr += 3; // Check that this works... might need ++ three times.
#if defined(TARGET_KL46Z)
                    sprintf(lcdMessage, "%3ds", i/5);
                    lcd.printf(lcdMessage);
#endif
                    while( timer.read_us() < _stream_sampling_wait_us );
                    timer.reset();

                    // Check if user swiped to stop logging (TODO: actual swipe detection ;))
                    if (tsi.readDistance() > 20){
                        accLoggedDataLength = i*3;
                        break;
                    }
                }
                timer.stop();
#if defined(TARGET_KL46Z)
                lcd.DP2(0);
                lcd.printf("DONE");
#endif
                // The following line is commented out (for now) as we get a crash if we send data and are not connected.
                if (sendNotifications)
                    sendString("{\"datatype\":\"Notification\",\"data\":\"LoggingEnded\"}\n");
                // Set green LED to indicate logging is done
                setRGB(0,255,0);
                currentState = IDLE_STATE;  // Done, switch back
                break;
            case GET_LOG_STATE:
                sprintf(sbuf, "{\"datatype\":\"AccelerometerLog\",\n" \
                             "\"accelrange\":%d,\n" \
                             "\"accelfactor\":%d,\n" \
                             "\"samplingrate\":%d,\n" \
                             "\"data\":[\n",  _accelerometerRange, 8192 / _accelerometerRange, _stream_sampling_rate);
                sendString(sbuf);
                for (int i=0; i<accLoggedDataLength; i=i+3) {
                    if (i<(accLoggedDataLength-3))
                        sprintf(sbuf,"[%d,%d,%d],\n",accLog[i],accLog[i+1],accLog[i+2]);
                    else
                        sprintf(sbuf,"[%d,%d,%d]\n",accLog[i],accLog[i+1],accLog[i+2]);
                    sendString(sbuf);
                }
                sendString("]}\n");
                currentState = IDLE_STATE;  // Done, switch back
                break;
            default:
                sendString("{\"datatype\":\"StatusMessage\",\"data\":\"Unexpected state.\"}\n");
        }

        if (touchStreaming || accelerometerStreaming) {
            // Use the high precision timer
            while( loopTimer.read_us() < _stream_sampling_wait_us );
            loopTimer.reset();
            if (touchStreaming)
                touchValue = tsi.readDistance();
            if (accelerometerStreaming)
                acc.getAccAllAxis(accXYZ);
            // Separate reading and printing for better precision
            sprintf(sbuf, "{\"datatype\":\"StreamData\",\n\"samplingrate\":%d", _stream_sampling_rate);
            sendString(sbuf);
            if (touchStreaming) {
                sprintf(sbuf, ",\n\"touchsensordata\":%d", touchValue);
                sendString(sbuf);
            }
            if (accelerometerStreaming) {
                sprintf(sbuf, ",\n\"accelerometerdata\":[%d,%d,%d]",accXYZ[0],accXYZ[1],accXYZ[2]);
                sendString(sbuf);
            }
            sendString("\n}");
        } else {
            wait_ms(100);
            loopTimer.reset();  // keep it ready
        }


        // if(webUSB.read(rbuf_cdc, &read_size_cdc, true)) {
        //     rbuf_cdc[read_size_cdc] = 0;
        //     webUSB.write(rbuf_cdc, read_size_cdc);
        // }
    }
}
