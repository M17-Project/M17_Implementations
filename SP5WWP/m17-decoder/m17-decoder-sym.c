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
#define SHOW_VITERBI_ERRS

float sample;                       //last raw sample from the stdin
float last[8];                      //look-back buffer for finding syncwords
float dist;                         //Euclidean distance for finding syncwords in the symbol stream
float pld[SYM_PER_PLD];             //raw frame symbols
uint16_t soft_bit[2*SYM_PER_PLD];   //raw frame soft bits
uint16_t d_soft_bit[2*SYM_PER_PLD]; //deinterleaved soft bits

uint8_t lsf[30+1];                  //complete LSF (one byte extra needed for the Viterbi decoder)
uint16_t lich_chunk[96];            //raw, soft LSF chunk extracted from the LICH
uint8_t lich_b[6];                  //48-bit decoded LICH
uint8_t lich_cnt;                   //LICH_CNT
uint8_t lich_chunks_rcvd=0;         //flags set for each LSF chunk received
uint16_t expected_next_fn=0;        //frame number of the next frame expected to arrive

uint16_t enc_data[272];             //raw frame data soft bits
uint8_t frame_data[19];             //decoded frame data, 144 bits (16+128), plus 4 flushing bits

uint8_t syncd=0;                    //syncword found?
uint8_t fl=0;                       //Frame=0 of LSF=1
uint8_t pushed;                     //counter for pushed symbols

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
        if(fread((uint8_t*)&sample, 4, 1, stdin)<1) break;

        if(!syncd)
        {
            //push new symbol
            for(uint8_t i=0; i<7; i++)
            {
                last[i]=last[i+1];
            }

            last[7]=sample;

            //calculate euclidean norm
            dist = eucl_norm(last, str_sync, 8);

            if(dist<DIST_THRESH) //frame syncword detected
            {
                //fprintf(stderr, "str_sync dist: %3.5f\n", dist);
                syncd=1;
                pushed=0;
                fl=0;
            }
            else
            {
                //calculate euclidean norm again, this time against LSF syncword
                dist = eucl_norm(last, lsf_sync, 8);

                if(dist<DIST_THRESH) //LSF syncword
                {
                    //fprintf(stderr, "lsf_sync dist: %3.5f\n", dist);
                    syncd=1;
                    pushed=0;
                    fl=1;
                }
            }
        }
        else
        {
            pld[pushed++]=sample;

            if(pushed==SYM_PER_PLD)
            {
                //common operations for all frame types
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

                //if it is a frame
                if(!fl)
                {
                    //extract data
                    for(uint16_t i=0; i<272; i++)
                    {
                        enc_data[i]=d_soft_bit[96+i];
                    }

                    //decode
                    uint32_t e=decodePunctured(frame_data, enc_data, P_2, 272, 12);

                    uint16_t fn = (frame_data[1] << 8) | frame_data[2];

                    //dump data - first byte is empty
                    printf("FN: %04X PLD: ", fn);
                    for(uint8_t i=3; i<19; i++)
                    {
                        printf("%02X", frame_data[i]);
                    }
                    #ifdef SHOW_VITERBI_ERRS
                    printf(" e=%1.1f\n", (float)e/0xFFFF);
                    #else
                    printf("\n");
                    #endif

                    //send codec2 stream to stdout
                    //fwrite(&frame_data[3], 16, 1, stdout);

                    //extract LICH
                    for(uint16_t i=0; i<96; i++)
                    {
                        lich_chunk[i]=d_soft_bit[i];
                    }

                    //Golay decoder
                    decode_LICH(lich_b, lich_chunk);
                    lich_cnt=lich_b[5]>>5;

                    //If we're at the start of a superframe, or we missed a frame, reset the LICH state
                    if((lich_cnt==0) || ((fn % 0x8000)!=expected_next_fn))
                        lich_chunks_rcvd=0;

                    lich_chunks_rcvd|=(1<<lich_cnt);
                    memcpy(&lsf[lich_cnt*5], lich_b, 5);

                    //debug - dump LICH
                    if(lich_chunks_rcvd==0x3F) //all 6 chunks received?
                    {
                        #ifdef DECODE_CALLSIGNS
                        uint8_t d_dst[12], d_src[12]; //decoded strings

                        decode_callsign(d_dst, &lsf[0]);
                        decode_callsign(d_src, &lsf[6]);

                        //DST
                        printf("DST: %-9s ", d_dst);

                        //SRC
                        printf("SRC: %-9s ", d_src);
                        #else
                        //DST
                        printf("DST: ");
                        for(uint8_t i=0; i<6; i++)
                            printf("%02X", lsf[i]);
                        printf(" ");

                        //SRC
                        printf("SRC: ");
                        for(uint8_t i=0; i<6; i++)
                            printf("%02X", lsf[6+i]);
                        printf(" ");
                        #endif

                        //TYPE
                        printf("TYPE: ");
                        for(uint8_t i=0; i<2; i++)
                            printf("%02X", lsf[12+i]);
                        printf(" ");

                        //META
                        printf("META: ");
                        for(uint8_t i=0; i<14; i++)
                            printf("%02X", lsf[14+i]);
                        //printf(" ");

                        //CRC
                        //printf("CRC: ");
                        //for(uint8_t i=0; i<2; i++)
                            //printf("%02X", lsf[28+i]);
                        if(CRC_M17(lsf, 30))
                            printf(" LSF_CRC_ERR");
                        else
                            printf(" LSF_CRC_OK ");
                        printf("\n");
                    }

                    expected_next_fn = (fn + 1) % 0x8000;
                }
                else //lsf
                {
                    printf("LSF\n");

                    //decode
                    uint32_t e=decodePunctured(lsf, d_soft_bit, P_1, 2*SYM_PER_PLD, 61);

                    //shift the buffer 1 position left - get rid of the encoded flushing bits
                    for(uint8_t i=0; i<30; i++)
                        lsf[i]=lsf[i+1];

                    //dump data
                    #ifdef DECODE_CALLSIGNS
                    uint8_t d_dst[12], d_src[12]; //decoded strings

                    decode_callsign(d_dst, &lsf[0]);
                    decode_callsign(d_src, &lsf[6]);

                    //DST
                    printf("DST: %-9s ", d_dst);

                    //SRC
                    printf("SRC: %-9s ", d_src);
                    #else
                    //DST
                    printf("DST: ");
                    for(uint8_t i=0; i<6; i++)
                        printf("%02X", lsf[i]);
                    printf(" ");

                    //SRC
                    printf("SRC: ");
                    for(uint8_t i=0; i<6; i++)
                        printf("%02X", lsf[6+i]);
                    printf(" ");
                    #endif

                    //TYPE
                    printf("TYPE: ");
                    for(uint8_t i=0; i<2; i++)
                        printf("%02X", lsf[12+i]);
                    printf(" ");

                    //META
                    printf("META: ");
                    for(uint8_t i=0; i<14; i++)
                        printf("%02X", lsf[14+i]);
                    printf(" ");

                    //CRC
                    //printf("CRC: ");
                    //for(uint8_t i=0; i<2; i++)
                        //printf("%02X", lsf[28+i]);
                    if(CRC_M17(lsf, 30))
                        printf("LSF_CRC_ERR");
                    else
                        printf("LSF_CRC_OK ");

                    //Viterbi decoder errors
                    #ifdef SHOW_VITERBI_ERRS
                    printf(" e=%1.1f\n", (float)e/0xFFFF);
                    #else
                    printf("\n");
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

    return 0;
}
