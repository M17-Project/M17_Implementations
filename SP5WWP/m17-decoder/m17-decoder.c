#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "../inc/m17.h"

int16_t sample;                 //raw S16_LE baseband sample
int16_t flt_buff[FLT_LEN];      //root-nyquist filter buffer
int16_t sw_buff[SW_LEN];        //syncword detection buffer
int16_t xc_buff[XC_LEN];        //cross-correlation buffer
int16_t mac;                    //multiply-accumulate

//states
uint8_t pre_syncd=0;
uint8_t syncd=0;
int8_t phase=0;

int main(void)
{
    while(1)
    {
        //wait for another baseband sample
        if(fread((uint8_t*)&sample, 2, 1, stdin)<1) break;

        //push the root-nyquist filter's buffer
        for(uint8_t i=0; i<FLT_LEN-1; i++)
        {
            flt_buff[i]=flt_buff[i+1];
        }

        flt_buff[FLT_LEN-1]=sample;

        //calculate the filter's output
		mac=0;
		for(uint8_t i=0; i<FLT_LEN; i++)
			mac+=flt_buff[i]*taps[i];

        for(uint8_t i=0; i<FLT_LEN-2; i++)
        {
            sw_buff[i]=sw_buff[i+1];
        }

        sw_buff[FLT_LEN-2]=mac;

        //detect syncword using cross-correlation
        int32_t xcorr=0;

        for(uint8_t i=0; i<XC_LEN; i+=10)
        {
            xcorr+=sw_buff[i]*str_sync[i/10];
        }

        //push the xcorr value to the buffer
        for(uint8_t i=0; i<XC_LEN-1; i++)
        {
            xc_buff[i]=xc_buff[i+1];
        }

        xc_buff[XC_LEN-1]=xcorr/24;

        //detect peak
        if(xc_buff[XC_LEN-1]>0.4*INT16_MAX)
        {
            uint8_t msg[64];
            uint8_t len=sprintf(msg, "SYNC\n");
            write(STDERR_FILENO, msg, len);
        }
    }

   return 0;
}
