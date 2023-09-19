#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "../inc/m17.h"
#include "golay.h"
#include "viterbi.h"
#include "crc.h"

#define DECODE_CALLSIGNS
//#define SHOW_VITERBI_ERRS

float sample;                       //last raw sample from the stdin
float last[8];                      //look-back buffer for finding syncwords
float dist;                         //Euclidean distance for finding syncwords in the symbol stream
float pld[SYM_PER_PLD];             //raw frame symbols
uint16_t soft_bit[2*SYM_PER_PLD];   //raw frame soft bits
uint16_t d_soft_bit[2*SYM_PER_PLD]; //deinterleaved soft bits

uint8_t lsf[30+1];                  //complete LSF (one byte extra needed for the Viterbi decoder)
uint8_t frame_data[26+1];           //decoded frame data, 206 bits, plus 4 flushing bits
uint8_t packet_data[33*25];         //whole packet data

uint8_t syncd=0;                    //syncword found?
uint8_t fl=0;                       //Frame=0 of LSF=1
int8_t last_fn;                     //last received frame number (-1 when idle)
uint8_t pushed;                     //counter for pushed symbols

//decodes a 6-byte long array to a callsign
void decode_callsign(uint8_t *outp, const uint8_t *inp)
{
	uint64_t encoded=0;

	//repack the data to a uint64_t
	for(uint8_t i=0; i<6; i++)
		encoded|=(uint64_t)inp[5-i]<<(8*i);

	//check if the value is reserved (not a callsign)
	if(encoded>=262144000000000ULL)
	{
        if(encoded==0xFFFFFFFFFFFF) //broadcast
        {
            sprintf((char*)outp, "#BCAST");
        }
        else
        {
            outp[0]=0;
        }

        return;
	}

	//decode the callsign
	uint8_t i=0;
	while(encoded>0)
	{
		outp[i]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."[encoded%40];
		encoded/=40;
		i++;
	}
	outp[i]=0;
}

float eucl_norm(const float* in1, const int8_t* in2, uint8_t len)
{
    float tmp = 0.0f;

    for(uint8_t i=0; i<len; i++)
    {
        tmp += powf(in1[i]-(float)in2[i], 2.0f);
    }

    return sqrt(tmp);
}

int main(void)
{
    while(1)
    {
        //wait for another symbol
        while(fread((uint8_t*)&sample, 4, 1, stdin)<1);

        if(!syncd)
        {
            //push new symbol
            for(uint8_t i=0; i<7; i++)
            {
                last[i]=last[i+1];
            }

            last[7]=sample;

            //calculate euclidean norm
            dist = eucl_norm(last, pkt_sync, 8);

            //fprintf(stderr, "pkt_sync dist: %3.5f\n", dist);
            if(dist<DIST_THRESH) //frame syncword detected
            {
                //fprintf(stderr, "pkt_sync\n");
                syncd=1;
                pushed=0;
                fl=0;
            }
            else
            {
                //calculate euclidean norm again, this time against LSF syncword
                dist = eucl_norm(last, lsf_sync, 8);

                //fprintf(stderr, "lsf_sync dist: %3.5f\n", dist);
                if(dist<DIST_THRESH) //LSF syncword
                {
                    //fprintf(stderr, "lsf_sync\n");
                    syncd=1;
                    pushed=0;
                    last_fn=-1;
                    memset(packet_data, 0, 33*25);
                    fl=1;
                }
            }
        }
        else
        {
            pld[pushed++]=sample;

            if(pushed==SYM_PER_PLD)
            {
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

                //if it is a frame
                if(!fl)
                {
                    //decode
                    #ifdef SHOW_VITERBI_ERRS
                    uint32_t e=decodePunctured(frame_data, d_soft_bit, P_3, SYM_PER_PLD*2, 8);
                    #else
                    decodePunctured(frame_data, d_soft_bit, P_3, SYM_PER_PLD*2, 8);
                    #endif

                    //dump FN
                    uint8_t rx_fn=(frame_data[26]>>2)&0x1F;
                    uint8_t rx_last=frame_data[26]>>7;
                    //fprintf(stderr, "FN%d, (%d)\n", rx_fn, rx_last);

                    //copy data - might require some fixing
                    if(rx_fn<=31 && rx_fn==last_fn+1 && !rx_last)
                    {
                        memcpy(&packet_data[rx_fn*25], &frame_data[1], 25);
                        last_fn++;
                    }
                    else if(rx_last)
                    {
                        memcpy(&packet_data[last_fn*25], &frame_data[1], rx_fn);

                        //dump data
                        if(packet_data[0]==0x05) //if a text message
                        {
                            fprintf(stderr, "%s", &packet_data[1]);
                        }
                        else
                        {
                            fprintf(stderr, "PKT: ");
                            for(uint16_t i=0; i<last_fn*25+rx_fn; i++)
                            {
                                fprintf(stderr, "%02X", packet_data[i]);
                            }
                        }

                        fprintf(stderr, "\n");
                    }

                    #ifdef SHOW_VITERBI_ERRS
                    fprintf(stderr, " e=%1.1f\n", (float)e/0xFFFF);
                    #else
                    //fprintf(stderr, "\n");
                    #endif

                    //send codec2 stream to stdout
                    //write(STDOUT_FILENO, &frame_data[3], 16);
                }
                else //if it is LSF
                {
                    //fprintf(stderr, "LSF\n");

                    //decode
                    #ifdef SHOW_VITERBI_ERRS
                    uint32_t e=decodePunctured(lsf, d_soft_bit, P_1, 2*SYM_PER_PLD, 61);
                    #else
                    decodePunctured(lsf, d_soft_bit, P_1, 2*SYM_PER_PLD, 61);
                    #endif

                    //shift the buffer 1 position left - get rid of the encoded flushing bits
                    for(uint8_t i=0; i<30; i++)
                        lsf[i]=lsf[i+1];

                    //dump data
                    #ifdef DECODE_CALLSIGNS
                    uint8_t d_dst[12], d_src[12]; //decoded strings

                    decode_callsign(d_dst, &lsf[0]);
                    decode_callsign(d_src, &lsf[6]);

                    //DST
                    fprintf(stderr, "DST: %-9s ", d_dst);

                    //SRC
                    fprintf(stderr, "SRC: %-9s ", d_src);
                    #else
                    //DST
                    fprintf(stderr, "DST: ");
                    for(uint8_t i=0; i<6; i++)
                        fprintf(stderr, "%02X", lsf[i]);
                    fprintf(stderr, " ");

                    //SRC
                    fprintf(stderr, "SRC: ");
                    for(uint8_t i=0; i<6; i++)
                        fprintf(stderr, "%02X", lsf[6+i]);
                    fprintf(stderr, " ");
                    #endif

                    //TYPE
                    fprintf(stderr, "TYPE: ");
                    for(uint8_t i=0; i<2; i++)
                        fprintf(stderr, "%02X", lsf[12+i]);
                    fprintf(stderr, " ");

                    //META
                    fprintf(stderr, "META: ");
                    for(uint8_t i=0; i<14; i++)
                        fprintf(stderr, "%02X", lsf[14+i]);
                    fprintf(stderr, " ");

                    //CRC
                    //fprintf(stderr, "CRC: ");
                    //for(uint8_t i=0; i<2; i++)
                        //fprintf(stderr, "%02X", lsf[28+i]);
                    if(CRC_M17(lsf, 30))
                        fprintf(stderr, "LSF_CRC_ERR");
                    else
                        fprintf(stderr, "LSF_CRC_OK ");

                    //Viterbi decoder errors
                    #ifdef SHOW_VITERBI_ERRS
                    fprintf(stderr, " e=%1.1f\n", (float)e/0xFFFF);
                    #else
                    fprintf(stderr, "\n");
                    #endif
                }

                //job done
                syncd=0;
                pushed=0;

                for(uint8_t i=0; i<8; i++)
                    last[i]=0.0;
            }
        }
    }
}
