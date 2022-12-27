#include <string.h>
#include <stdint.h>
#include "crc.h"

const uint16_t M17_CRC_POLY = 0x5935;

uint16_t CRC_M17(const uint8_t *in, const uint16_t len)
{
	uint32_t crc=0xFFFF; //init val

	for(uint16_t i=0; i<len; i++)
	{
		crc^=in[i]<<8;
		for(uint8_t j=0; j<8; j++)
		{
			crc<<=1;
			if(crc&0x10000)
				crc=(crc^M17_CRC_POLY)&0xFFFF;
		}
	}

	return crc&(0xFFFF);
}
