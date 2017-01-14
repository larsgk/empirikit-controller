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

void setColor(uint8_t r, uint8_t g, uint8_t b) {
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
        *((uint32_t *)0x40048058),
        *((uint32_t *)0x4004805C),
        *((uint32_t *)0x40048060));
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
        setColor(params[0], params[1], params[2]);
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


#define MAX_BUF_SIZE 2048

int main()
{
    int i = 0;

#if defined(TARGET_KL25Z)
    rled.period(0.001);
    gled.period(0.001);
    bled.period(0.001);
    setColor(255,0,0);
#endif


    rbuf = new uint8_t[MAX_BUF_SIZE];
    rbuf_cdc = new uint8_t[MAX_BUF_SIZE];

    sbuf = new char[200];

    while (true) {
        // try to read from endpoint
        if(webUSB.read(&rbuf[rbuf_len], &read_size)) {
            sprintf(sbuf, "{\"msg\":\"Read %d bytes\"}",(int)read_size);
            sendString(sbuf);

            rbuf_len += read_size;
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

        // if(webUSB.read(rbuf_cdc, &read_size_cdc, true)) {
        //     rbuf_cdc[read_size_cdc] = 0;
        //     webUSB.write(rbuf_cdc, read_size_cdc);
        // }

        wait_ms(20); // change to "wait padding"
        acc.getAccAllAxis(accXYZ);
        sprintf(sbuf, "{\"tick\":%d,\"x\":%d,\"y\":%d,\"z\":%d}\n", i,
            (int)accXYZ[0],(int)accXYZ[1],(int)accXYZ[2]);
        sendString(sbuf);

        i++;
    }
}
