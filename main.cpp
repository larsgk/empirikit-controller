/*
* Copyright 2017 Lars Gunder Knudsen
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
#include "WebUSBCDC.h"
#include "MMA8451Q.h"

#if defined(TARGET_KL46Z)
#include "SLCD.h"
#endif

#define MMA8451_I2C_ADDRESS (0x1d<<1)

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

#if defined(TARGET_KL46Z)
SLCD lcd;
char lcdMessage[40];
#endif


bool detached = false;
void onDetachRequested() {
    detached = true;
}

uint8_t* rbuf;
uint32_t rbuf_len = 0;
uint32_t read_size;

int params[10];

void handleCMD(uint8_t* cmd_buf, uint32_t size) {
    // very crude "json" parsing. We should put e.g. picoJSON in place here
    if (strncmp((char*)(&cmd_buf[2]),"SETRGB",6) == 0){
        sscanf((char*)(&cmd_buf[11]),"%d,%d,%d",&params[0], &params[1], &params[2]);
#if defined(TARGET_KL25Z)
        setColor(params[0], params[1], params[2]);
#elif defined(TARGET_KL46Z)
        lcd.printf("%04d",params[0]);
#endif
    }
}

MMA8451Q acc(PTE25, PTE24);
int16_t* accData;

WebUSBCDC webUSB(0x1209, 0x0001, 0x0001, true);

uint8_t* rbuf_cdc;
uint32_t rbuf_len_cdc = 0;
uint32_t read_size_cdc;

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
    accData = new int16_t[3];

    char* buf = new char[200];

    while (true) {
        // try to read from endpoint
        if(webUSB.read(&rbuf[rbuf_len], &read_size)) {
            sprintf(buf, "{\"msg\":\"Read %d bytes\"}",(int)read_size);
            webUSB.write((uint8_t *)buf, strlen(buf));

            rbuf_len += read_size;
            uint32_t buf_pos = 0;
            while(rbuf_len && buf_pos < rbuf_len) {
                // crude "find the '}'"
                if(rbuf[buf_pos] == '}') {
                    sprintf(buf, "{\"msg\":\"Found end bracket at pos: %d\"}",(int)buf_pos);
                    webUSB.write((uint8_t *)buf, strlen(buf));

                    handleCMD(rbuf, buf_pos+1);
                    memmove(rbuf, &rbuf[buf_pos+1], rbuf_len-(buf_pos+1));
                    rbuf_len-=buf_pos+1;
                }
                buf_pos++;
            }
        }

        if(webUSB.read(rbuf_cdc, &read_size_cdc, true)) {
            rbuf_cdc[read_size_cdc] = 0;
            webUSB.write(rbuf_cdc, read_size_cdc);
        }

        wait_ms(20); // change to "wait padding"
        acc.getAccAllAxis(accData);
        sprintf(buf, "{\"tick\":%d,\"x\":%d,\"y\":%d,\"z\":%d}\n", i,
            (int)accData[0],(int)accData[1],(int)accData[2]);
        webUSB.write((uint8_t *)buf, strlen(buf));

        i++;
    }
}
