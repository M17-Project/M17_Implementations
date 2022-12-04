#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>

#include "m17.h"

float sample;
float last[8];
float xcorr;
float pld[SYM_PER_PLD];
uint16_t soft_bit[2*SYM_PER_PLD];
uint16_t d_soft_bit[2*SYM_PER_PLD]; //deinterleaved soft bits
uint16_t lich_chunk[96];

uint8_t syncd=0;
uint8_t pushed; //pushed symbols

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

            if(xcorr>8.0)
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

                //debug - dump LICH
                uint8_t tmp;
                for(uint16_t i=0; i<96; i++)
                {
                    if(!(i%8))
                        tmp=0;
                    if(lich_chunk[i]>0x7FFF)
                        tmp|=(1<<(7-(i%8)));
                    if(!((i+1)%8))
                        write(STDOUT_FILENO, &tmp, 1);
                }
                tmp=0;
                for(uint8_t i=0; i<4; i++)
                    write(STDOUT_FILENO, &tmp, 1);

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
