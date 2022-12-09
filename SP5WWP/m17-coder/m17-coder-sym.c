#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "../inc/m17.h"

struct LSF
{
	uint8_t dst[6];
	uint8_t src[6];
	uint8_t type[2];
	uint8_t meta[112/8];
	uint8_t crc[2];
} lsf;

uint8_t data[16];   //payload, packed bits
uint16_t fn=0;      //16-bit Frame Number (for the stream mode)
uint8_t got_lsf=0;  //have we filled the LSF struct yet?

void send_Preamble(const uint8_t type)
{
    float symb;

    if(type) //pre-BERT
    {
        for(uint16_t i=0; i<192/2; i++) //40ms * 4800 = 192
        {
            symb=-3.0;
            write(STDOUT_FILENO, (uint8_t*)&symb,  sizeof(float));
            symb=+3.0;
            write(STDOUT_FILENO, (uint8_t*)&symb,  sizeof(float));
        }
    }
    else //pre-LSF
    {
        for(uint16_t i=0; i<192/2; i++) //40ms * 4800 = 192
        {
            symb=+3.0;
            write(STDOUT_FILENO, (uint8_t*)&symb,  sizeof(float));
            symb=-3.0;
            write(STDOUT_FILENO, (uint8_t*)&symb,  sizeof(float));
        }
    }
}

void send_Syncword(const uint16_t sword)
{
    float symb;

    for(uint8_t i=0; i<16; i+=2)
    {
        symb=symbol_map[(sword>>(14-i))&3];
        write(STDOUT_FILENO, (uint8_t*)&symb,  sizeof(float));
    }
}

//main routine
int main(void)
{
    while(1)
    {
        if(got_lsf) //stream frames
        {
            //we could discard the data we already have
            while(read(STDIN_FILENO, &(lsf.dst), 6)<6);
            while(read(STDIN_FILENO, &(lsf.src), 6)<6);
            while(read(STDIN_FILENO, &(lsf.type), 2)<2);
            while(read(STDIN_FILENO, &(lsf.meta), 14)<14);
            while(read(STDIN_FILENO, data, 16)<16);

            //send stream frame syncword
            send_Syncword(SYNC_STR);

            //send dummy symbols (debug)
            float s=0.0;
            for(uint8_t i=0; i<184; i++) //40ms * 4800 - 8 (syncword)
                write(STDOUT_FILENO, (uint8_t*)&s, sizeof(float));

            /*printf("\tDATA: ");
            for(uint8_t i=0; i<16; i++)
                printf("%02X", data[i]);
            printf("\n");*/
        }
        else //LSF
        {
            while(read(STDIN_FILENO, &(lsf.dst), 6)<6);
            while(read(STDIN_FILENO, &(lsf.src), 6)<6);
            while(read(STDIN_FILENO, &(lsf.type), 2)<2);
            while(read(STDIN_FILENO, &(lsf.meta), 14)<14);
            while(read(STDIN_FILENO, data, 16)<16);

            got_lsf=1;

            //send out the preamble and LSF
            send_Preamble(0); //0 - LSF preamble, as opposed to 1 - BERT preamble

            //send LSF syncword
            send_Syncword(SYNC_LSF);

            //send dummy symbols (debug)
            float s=0.0;
            for(uint8_t i=0; i<184; i++) //40ms * 4800 - 8 (syncword)
                write(STDOUT_FILENO, (uint8_t*)&s, sizeof(float));

            /*printf("DST: ");
            for(uint8_t i=0; i<6; i++)
                printf("%02X", lsf.dst[i]);
            printf(" SRC: ");
            for(uint8_t i=0; i<6; i++)
                printf("%02X", lsf.src[i]);
            printf(" TYPE: ");
            for(uint8_t i=0; i<2; i++)
                printf("%02X", lsf.type[i]);
            printf(" META: ");
            for(uint8_t i=0; i<14; i++)
                printf("%02X", lsf.meta[i]);
            printf("\n");*/
        }
    }

	return 0;
}
