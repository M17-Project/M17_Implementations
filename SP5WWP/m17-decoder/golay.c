#include <string.h>
#include <stdint.h>
#include "golay.h"

static const uint16_t encode_matrix[12]=
{
    0x8eb, 0x93e, 0xa97, 0xdc6, 0x367, 0x6cd,
    0xd99, 0x3da, 0x7b4, 0xf68, 0x63b, 0xc75
};

const uint16_t decode_matrix[12]=
{
    0xc75, 0x49f, 0x93e, 0x6e3, 0xdc6, 0xf13,
    0xab9, 0x1ed, 0x3da, 0x7b4, 0xf68, 0xa4f
};

//0 index - LSB
void IntToSoft(uint16_t* out, const uint16_t in, uint8_t len)
{
	for(uint8_t i=0; i<len; i++)
	{
		(in>>i)&1 ? (out[i]=0xFFFF) : (out[i]=0);
	}
}

uint16_t SoftToInt(const uint16_t* in, uint8_t len)
{
	uint16_t tmp=0;

	for(uint8_t i=0; i<len; i++)
	{
		if(in[i]>0x7FFF)
			tmp|=(1<<i);
	}

	return tmp;
}

//Quadrant I fixed point division with saturation
//result=a/b
uint16_t Div16(uint16_t a, uint16_t b)
{
	uint32_t aa=(uint32_t)a<<16;
	uint32_t r=aa/b;

	if(r<=0xFFFF)
		return r;
	else
		return 0xFFFF;
}

//Quadrant I fixed point multiplication
//result=a/b
uint16_t Mul16(uint16_t a, uint16_t b)
{
	return (uint16_t)(((uint32_t)a*b)>>16);
}

//use bilinear interpolation for XOR
uint16_t SoftBitXOR(const uint16_t a, const uint16_t b)
{
	return Mul16(Div16(0xFFFF-b, 0xFFFF), Div16(a, 0xFFFF)) + Mul16(Div16(b, 0xFFFF), Div16(0xFFFF-a, 0xFFFF));
}

//soft XOR
void SoftXOR(uint16_t* out, const uint16_t* a, const uint16_t* b, uint8_t len)
{
	for(uint8_t i=0; i<len; i++)
		out[i]=SoftBitXOR(a[i], b[i]);
}

//soft equivalent of popcount
uint32_t spopcount(const uint16_t* in, uint8_t siz)
{
	uint32_t tmp=0;

	for(uint8_t i=0; i<siz; i++)
		tmp+=in[i];

	return tmp;
}

void calcChecksumS(uint16_t* out, const uint16_t* value)
{
    uint16_t checksum[12];
    uint16_t soft_em[12]; //soft valued encoded matrix entry

    for(uint8_t i=0; i<12; i++)
    	checksum[i]=0;

    for(uint8_t i=0; i<12; i++)
    {
    	IntToSoft(soft_em, encode_matrix[i], 12);

        if(value[i]>0x7FFF)
        {
            SoftXOR(checksum, checksum, soft_em, 12);
        }
    }

    memcpy((uint8_t*)out, (uint8_t*)checksum, 12*2);
}

uint32_t SdetectErrors(const uint16_t* codeword)
{
    uint16_t data[12];
    uint16_t parity[12];
    uint16_t cksum[12];
    uint16_t syndrome[12];
    uint32_t weight; //for soft popcount

	memcpy((uint8_t*)data, (uint8_t*)&codeword[12], 2*12);
	memcpy((uint8_t*)parity, (uint8_t*)&codeword[0], 2*12);

	calcChecksumS(cksum, data);
	SoftXOR(syndrome, parity, cksum, 12);

	weight=spopcount(syndrome, 12);

	//all (less than 4) errors in the parity part
    if(weight < 4*0xFFFE)
    {
    	//printf("1: %1.2f\n", (float)weight/0xFFFF);
        return SoftToInt(syndrome, 12);
    }

    //one of the errors in data part, up to 3 in parity
    for(uint8_t i = 0; i<12; i++)
    {
        uint16_t e = 1<<i;
        uint16_t coded_error = encode_matrix[i];
        uint16_t scoded_error[12]; //soft coded_error
        uint16_t sc[12]; //syndrome^coded_error

        IntToSoft(scoded_error, coded_error, 12);
        SoftXOR(sc, syndrome, scoded_error, 12);
        weight=spopcount(sc, 12);

        if(weight < 3*0xFFFE)
        {
        	//printf("2: %1.2f\n", (float)weight/0xFFFF+1);
        	uint16_t s=SoftToInt(syndrome, 12);
            return (e << 12) | (s ^ coded_error);
        }
    }

    //two of the errors in data part and up to 2 in parity
    for(uint8_t i = 0; i<11; i++)
    {
    	for(uint8_t j = i+1; j<12; j++)
    	{
    		uint16_t e = (1<<i) | (1<<j);
        	uint16_t coded_error = encode_matrix[i]^encode_matrix[j];
        	uint16_t scoded_error[12]; //soft coded_error
	        uint16_t sc[12]; //syndrome^coded_error

	        IntToSoft(scoded_error, coded_error, 12);
	        SoftXOR(sc, syndrome, scoded_error, 12);
	        weight=spopcount(sc, 12);

	        if(weight < 2*0xFFFF)
	        {
	        	//printf("3: %1.2f\n", (float)weight/0xFFFF+2);
	        	uint16_t s=SoftToInt(syndrome, 12);
	            return (e << 12) | (s ^ coded_error);
	        }
		}
    }

	//algebraic decoding magic
    uint16_t inv_syndrome[12]={0,0,0,0,0,0,0,0,0,0,0,0};
    uint16_t dm[12]; //soft decode matrix

    for(uint8_t i=0; i<12; i++)
    {
        if(syndrome[i] > 0x7FFF)
        {
        	IntToSoft(dm, decode_matrix[i], 12);
        	SoftXOR(inv_syndrome, inv_syndrome, dm, 12);
        }
    }

	//all (less than 4) errors in the data part
	weight=spopcount(inv_syndrome, 12);
    if(weight < 4*0xFFFF)
    {
    	//printf("4: %1.2f\n", (float)weight/0xFFFF);
        return SoftToInt(inv_syndrome, 12) << 12;
    }

	//one error in parity bits, up to 3 in data - this part has some quirks, the reason remains unknown
    for(uint8_t i=0; i<12; i++)
    {
        uint16_t e = 1<<i;
        uint16_t coding_error = decode_matrix[i];

        uint16_t ce[12]; //soft coding error
        uint16_t tmp[12];

        IntToSoft(ce, coding_error, 12);
        SoftXOR(tmp, inv_syndrome, ce, 12);
        weight=spopcount(tmp, 12);

        if(weight < 3*(0xFFFF+2))
        {
        	//printf("5: %1.2f\n", (float)weight/0xFFFF+1);
            return ((SoftToInt(inv_syndrome, 12) ^ coding_error) << 12) | e;
        }
    }

    return 0xFFFFFFFF;
}

//soft decode
uint16_t golay24_sdecode(const uint16_t* codeword)
{
    //match the bit order in M17
    uint16_t cw[24];
    for(uint8_t i=0; i<24; i++)
        cw[i]=codeword[23-i];

    uint32_t errors = SdetectErrors(cw);

    if(errors == 0xFFFFFFFF)
		return 0xFFFF;

    return (((SoftToInt(&cw[0], 16) | (SoftToInt(&cw[16], 8) << 16)) ^ errors) >> 12) & 0x0FFF;
}
