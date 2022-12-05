#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "m17.h"
#include "golay.h"

float sample;
float last[8];
float xcorr;
float pld[SYM_PER_PLD];
uint16_t soft_bit[2*SYM_PER_PLD];
uint16_t d_soft_bit[2*SYM_PER_PLD]; //deinterleaved soft bits

uint8_t lsf[30];                    //complete LSF
uint16_t lich_chunk[96];            //raw, soft LSF chunk extracted from the LICH
uint8_t lich_b[6];                  //48-bit decoded LICH
uint8_t lich_cnt;                   //LICH_CNT
uint8_t lich_chunks_rcvd=0;         //flags set for each LSF chunk received

uint8_t syncd=0;
uint8_t pushed; //pushed symbols

//soft decodes LICH into a 6-byte array
//input - soft bits
//output - an array of packed bits
void decode_LICH(uint8_t* outp, const uint16_t* inp)
{
    uint16_t tmp;

    memset(outp, 0, 5);

    tmp=golay24_sdecode(&inp[0]);
    outp[0]=(tmp>>4)&0xFF;
    outp[1]|=(tmp&0xF)<<4;
    tmp=golay24_sdecode(&inp[1*24]);
    outp[1]|=(tmp>>8)&0xF;
    outp[2]=tmp&0xFF;
    tmp=golay24_sdecode(&inp[2*24]);
    outp[3]=(tmp>>4)&0xFF;
    outp[4]|=(tmp&0xF)<<4;
    tmp=golay24_sdecode(&inp[3*24]);
    outp[4]|=(tmp>>8)&0xF;
    outp[5]=tmp&0xFF;
}

int main(void)
{
    while(1)
    {
        //wait for another symbol
        while(read(STDIN_FILENO, (uint8_t*)&sample, 4)<4);

        if(!syncd)
        {
            //push new symbol
            for(uint8_t i=0; i<7; i++)
            {
                last[i]=last[i+1];
            }

            last[7]=sample;

            //calculate cross-correlation
            xcorr=0;
            for(uint8_t i=0; i<8; i++)
            {
                xcorr+=last[i]*str_sync[i];
            }

            //printf("%f\n", xcorr);

            if(xcorr>62.0)
            {
                syncd=1;
                pushed=0;
            }
        }
        else
        {
            pld[pushed++]=sample;

            if(pushed==SYM_PER_PLD)
            {
                //decode symbols to soft dibits
                for(uint8_t i=0; i<SYM_PER_PLD; i++)
                {
                    //bit 0
                    if(pld[i]>=symbs[3])
                    {
                        soft_bit[i*2+1]=0xFFFF;
                    }
                    else if(pld[i]>=symbs[2])
                    {
                        soft_bit[i*2+1]=-(float)0xFFFF/(symbs[3]-symbs[2])*symbs[2]+pld[i]*(float)0xFFFF/(symbs[3]-symbs[2]);
                    }
                    else if(pld[i]>=symbs[1])
                    {
                        soft_bit[i*2+1]=0x0000;
                    }
                    else if(pld[i]>=symbs[0])
                    {
                        soft_bit[i*2+1]=(float)0xFFFF/(symbs[1]-symbs[0])*symbs[1]-pld[i]*(float)0xFFFF/(symbs[1]-symbs[0]);
                    }
                    else
                    {
                        soft_bit[i*2+1]=0xFFFF;
                    }

                    //bit 1
                    if(pld[i]>=symbs[2])
                    {
                        soft_bit[i*2]=0x0000;
                    }
                    else if(pld[i]>=symbs[1])
                    {
                        soft_bit[i*2]=0x7FFF-pld[i]*(float)0xFFFF/(symbs[2]-symbs[1]);
                    }
                    else
                    {
                        soft_bit[i*2]=0xFFFF;
                    }
                }

                //derandomize
                for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
                {
                    if((rand_seq[i/8]>>(7-(i%8)))&1) //soft XOR. flip soft bit if "1"
                        soft_bit[i]=0xFFFF-soft_bit[i];
                }

                //deinterleave
                for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
                {
                    d_soft_bit[i]=soft_bit[intrl_seq[i]];
                }

                //extract LICH
                for(uint16_t i=0; i<96; i++)
                {
                    lich_chunk[i]=d_soft_bit[i];
                }

                //Golay decoder goes here
                decode_LICH(lich_b, lich_chunk);

                for(uint8_t i=0; i<6; i++)
                    printf("%02X ", lich_b[i]);
                printf("\n");

                //debug - dump LICH
                /*uint8_t tmp;
                for(uint16_t i=0; i<96; i++)
                {
                    if(!(i%8))
                        tmp=0;
                    if(lich_chunk[i]>0x7FFF)
                        tmp|=(1<<(7-(i%8)));
                    if(!((i+1)%8))
                        write(STDOUT_FILENO, &tmp, 1);
                }
                tmp=0; //padding
                for(uint8_t i=0; i<4; i++)
                    write(STDOUT_FILENO, &tmp, 1);*/

                //job done
                syncd=0;
                pushed=0;

                for(uint8_t i=0; i<8; i++)
                    last[i]=0.0;
            }
        }
    }

    return 0;
}
