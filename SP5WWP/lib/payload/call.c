//--------------------------------------------------------------------
// M17 C library - payload/call.c
//
// This file contains:
// - callsign encoder and decoders
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "call.h"

//decodes a 6-byte long array to a callsign
void decode_callsign_bytes(uint8_t *outp, const uint8_t *inp)
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

//decodes a 48-bit value to a callsign
void decode_callsign_value(uint8_t *outp, const uint64_t inp)
{
    uint64_t encoded=inp;

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

//encode callsign into a 48-bit value
uint8_t encode_callsign(uint64_t* out, const uint8_t* inp)
{
    //assert inp length
    if(strlen((const char*)inp)>9)
    {
        return -1;
    }

    const uint8_t charMap[40]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

    uint64_t tmp=0;

    if(strcmp((const char*)inp, "ALL")==0)
    {
        *out=0xFFFFFFFFFFFF;
        return 0;
    }

    for(int8_t i=strlen((const char*)inp)-1; i>=0; i--)
    {
        for(uint8_t j=0; j<40; j++)
        {
            if(inp[i]==charMap[j])
            {
                tmp=tmp*40+j;
                break;
            }
        }
    }

    *out=tmp;
    return 0;
}
