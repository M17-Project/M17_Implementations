//--------------------------------------------------------------------
// M17 C library - lib.c
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#include <stdio.h>
#include <encode/symbols.h>
#include "lib.h"

//misc
void send_preamble(const uint8_t type)
{
    float symb;

    if(type) //pre-BERT
    {
        for(uint16_t i=0; i<192/2; i++) //40ms * 4800 = 192
        {
            symb=-3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
            symb=+3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
        }
    }
    else //pre-LSF
    {
        for(uint16_t i=0; i<192/2; i++) //40ms * 4800 = 192
        {
            symb=+3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
            symb=-3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
        }
    }
}

void send_syncword(const uint16_t syncword)
{
    float symb;

    for(uint8_t i=0; i<16; i+=2)
    {
        symb=symbol_map[(syncword>>(14-i))&3];
        fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
    }
}

//send the data (can be used for both LSF and frames)
void send_data(const uint8_t* in)
{
	float s=0.0;
	for(uint16_t i=0; i<SYM_PER_PLD; i++) //40ms * 4800 - 8 (syncword)
	{
		s=symbol_map[in[2*i]*2+in[2*i+1]];
		fwrite((uint8_t*)&s, sizeof(float), 1, stdout);
	}
}

void send_eot(void)
{
    float symb=+3.0;
    for(uint16_t i=0; i<192; i++) //40ms * 4800 = 192
    {
        fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
    }
}
