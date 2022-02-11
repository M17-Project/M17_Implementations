#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define TYPE_PKT			0				//Packet type
#define TYPE_STR			(1<<0)			//Stream type
#define TYPE_RES			(0b00<<1)		//Reserved type
#define TYPE_V				(0b10<<1)		//Type: Voice
#define TYPE_D				(0b01<<1)		//Type: Data
#define TYPE_VD				(0b11<<1)		//Type: Voice+Data
#define TYPE_ENCR_NONE		(0b00<<3)		//Encryption: none
#define TYPE_ENCR_SUB_NONE	(0b00<<5)		//Encryption subtype: none
#define CAN					7				//Channel Access Number (bit location)
#define TYPE_RESERVED		(0b00000<<11)	//Reserved fields (zeroes)
#define EOS_BIT				(1<<16)			//End Of Stream indicator (1 bit)

const uint16_t crc_poly		=0x5935;
uint16_t CRC_LUT[256];

//Golay (24, 12) code generated with 0xC75 polynomial
const uint16_t golay_encode_matrix[12]={
	0x8eb,
	0x93e,
	0xa97,
	0xdc6,
	0x367,
	0x6cd,
	0xd99,
	0x3da,
	0x7b4,
	0xf68,
	0x63b,
	0xc75
};

const int16_t symbol_map[4]={+1, +3, -1, -3};

//#include "rrc_taps.h"

//syncwords
const uint16_t SYNC_LSF		=0x55F7;
const uint16_t SYNC_STR		=0xFF5D;
const uint16_t SYNC_PKT		=0x75FF;
const uint16_t SYNC_RES		=0xDF55;

//variables & structs
uint8_t src_ascii[10];	//9 chars + \0
uint8_t dst_ascii[10];	//9 chars + \0

struct LSF
{
	uint64_t dst;
	uint64_t src;
	uint16_t type;
	uint8_t meta[112/8];
	uint16_t crc;
};

uint16_t fn=0;
uint8_t payload[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};	//test only

//funcs
void ypcmem(uint8_t *dst, uint8_t *src, uint16_t nBytes)
{
	for(uint16_t i=0; i<nBytes; i++)
		dst[i]=src[nBytes-i-1];
}

uint64_t callsign_encode(const uint8_t *callsign)
{
	uint64_t encoded=0;

	//run from the end to the beginning of the callsign
	for(const uint8_t *p=(callsign+strlen(callsign)-1); p>=callsign; p--)
	{
		encoded *= 40;
		
		if (*p >= 'A' && *p <= 'Z')  // 1-26
			encoded += *p - 'A' + 1;
		else if (*p >= '0' && *p <= '9')  // 27-36
			encoded += *p - '0' + 27;
		else if (*p == '-')  // 37
			encoded += 37;
		else if (*p == '/')  // 38
			encoded += 38;
		else if (*p == '.')  // 39
			encoded += 39;
		else
			;
	}

	return encoded;
}

void CRC_init(uint16_t *crc_table, uint16_t poly)
{
	uint16_t remainder;

	for(uint16_t dividend=0; dividend<256; dividend++)
	{
		remainder=dividend<<8;

		for(uint8_t bit=8; bit>0; bit--)
		{
			if(remainder&(1<<15))
				remainder=(remainder<<1)^poly;
			else
				remainder=(remainder<<1);
		}

		crc_table[dividend]=remainder;
	}
}

uint16_t CRC_M17(uint16_t* crc_table, const uint8_t* message, uint16_t nBytes)
{
	uint8_t data;
	uint16_t remainder=0xFFFF;

	for(uint16_t byte=0; byte<nBytes; byte++)
	{
		data=message[byte]^(remainder>>8);
		remainder=crc_table[data]^(remainder<<8);
	}

	return(remainder);
}

uint32_t golay_coding(uint16_t m)
{
    uint32_t out=0;

    for(uint16_t i = 0; i<12; i++)
	{
		if(m & (1<<i))
	    	out ^= golay_encode_matrix[i];
    }

	out |= m<<12;
    
    return out;
}

void pack_LSF(uint8_t* dest, struct LSF *lsf_in, uint8_t crc_too)
{
	//TODO: the byte ordering scheme is NOT described in the spec!
	ypcmem(&dest[0], (uint8_t*)&(lsf_in->dst), 6);
	ypcmem(&dest[6], (uint8_t*)&(lsf_in->src), 6);
	ypcmem(&dest[12], (uint8_t*)&(lsf_in->type), 2);
	ypcmem(&dest[14], (uint8_t*)&(lsf_in->meta), 14);
	if(crc_too)
		ypcmem(&dest[28], (uint8_t*)&(lsf_in->crc), 2);
	//test
	/*printf("LSF: ");
	for(uint8_t i=0; i<30; i++)
		printf("%02X", dest[i]);
	printf("\n\n");*/
}

//pack Frame
//arg1: output array of packed bytes
//arg2: FN (frame number, 16-bit)
//arg3: LSF struct
//arg4: payload (packed array of bytes)
void pack_Frame(uint8_t* dest, uint16_t frame_cnt, struct LSF *lsf_in, uint8_t *payload)
{
	uint8_t lich_cnt=frame_cnt%6;
	uint8_t packed_LSF_chunk[6];
	uint8_t packed_LSF_chunk_golay[12];
	
	//pack a 40-bit chunk of LSF
	//we use memcpy instead of ypcmem here
	//because golay encoder expects little-endian data
	switch(lich_cnt)
	{
		case 0:
			memcpy(&packed_LSF_chunk[0], (uint8_t*)&(lsf_in->dst), 5);
		break;
		
		case 1:
			memcpy(&packed_LSF_chunk[0], (uint8_t*)&(lsf_in->dst)+5, 1);
			memcpy(&packed_LSF_chunk[1], (uint8_t*)&(lsf_in->src), 4);
		break;
		
		case 2:
			memcpy(&packed_LSF_chunk[0], (uint8_t*)&(lsf_in->src), 2);
			memcpy(&packed_LSF_chunk[2], (uint8_t*)&(lsf_in->type), 2);
			memcpy(&packed_LSF_chunk[4], (uint8_t*)&(lsf_in->meta), 1);
		break;
		
		case 3:
			memcpy(&packed_LSF_chunk[0], (uint8_t*)&(lsf_in->meta)+1, 5);
		break;
		
		case 4:
			memcpy(&packed_LSF_chunk[0], (uint8_t*)&(lsf_in->meta)+6, 5);
		break;
		
		case 5:
			memcpy(&packed_LSF_chunk[0], (uint8_t*)&(lsf_in->meta)+11, 3);
			memcpy(&packed_LSF_chunk[3], (uint8_t*)&(lsf_in->crc), 2);
		break;
		
		default:
			;
		break;
	}
	
	packed_LSF_chunk[5]=lich_cnt<<5; //5 LSBs are reserved (don't care)
	
	//time to Golay encode the LICH contents (48->96 bits)
	uint8_t golay_encoded_LICH[12]; //packet bytes, 96 total
	uint32_t g_enc[4]; //24-bit parts
	
	g_enc[0]=golay_coding(packed_LSF_chunk[0]|((packed_LSF_chunk[1]&0x0F)<<8));
	g_enc[1]=golay_coding(((packed_LSF_chunk[1]&0xF0)>>4)|(packed_LSF_chunk[2]<<4));
	g_enc[2]=golay_coding(packed_LSF_chunk[3]|((packed_LSF_chunk[4]&0x0F)<<8));
	g_enc[3]=golay_coding(((packed_LSF_chunk[4]&0xF0)>>4)|(packed_LSF_chunk[5]<<4));
	
	//the byte order is just my guess...
	golay_encoded_LICH[0]=g_enc[0]&0xFF;
	golay_encoded_LICH[1]=(g_enc[0]>>8)&0xFF;
	golay_encoded_LICH[2]=(g_enc[0]>>16)&0xFF;
	golay_encoded_LICH[3]=g_enc[1]&0xFF;
	golay_encoded_LICH[4]=(g_enc[1]>>8)&0xFF;
	golay_encoded_LICH[5]=(g_enc[1]>>16)&0xFF;
	golay_encoded_LICH[6]=g_enc[2]&0xFF;
	golay_encoded_LICH[7]=(g_enc[2]>>8)&0xFF;
	golay_encoded_LICH[8]=(g_enc[2]>>16)&0xFF;
	golay_encoded_LICH[9]=g_enc[3]&0xFF;
	golay_encoded_LICH[10]=(g_enc[3]>>8)&0xFF;
	golay_encoded_LICH[11]=(g_enc[3]>>16)&0xFF;

	//move to the destination
	memcpy(&dest[0], golay_encoded_LICH, 12);
	memcpy(&dest[12], (uint8_t*)&frame_cnt, 2);
	memcpy(&dest[14], payload, 16);
}

uint16_t CRC_LSF(struct LSF *lsf_in)
{
	uint8_t lsf_bytes[28];
	
	pack_LSF(lsf_bytes, lsf_in, 0);
	
	return CRC_M17(CRC_LUT, lsf_bytes, 28);
}

//unpack type-1 bits
void unpack_LSF(uint8_t *outp, struct LSF *lsf_in)
{
	uint8_t lsf_bytes[30];
	
	pack_LSF(lsf_bytes, lsf_in, 1);
	
	for(uint8_t i=0; i<240; i++)
		outp[i]=(lsf_bytes[i/8]>>(7-(i%8)))&1;
}

//convolve "num" unpacked type-1 bits into (num+4)*2 type-2 bits
//those 4 last bits are for shift register flushing
//arg1: convolutionally encoded type-2 bits, arg2: a pointer to unpacked type-1 bits
//arg3: number of bits to encode (without flushing)
void convolve(uint8_t *outp, uint8_t *inp, uint16_t num)
{
	//shift register for the convolutional encoder
	uint8_t sr=0;
	
	//"num" bits of data plus 4 "flushing" bits
	for(uint16_t i=0; i<num+4; i++)
	{
		sr>>=1;
		
		if(i<num)
		{
			sr|=inp[i]<<4;
		}
		
		outp[i*2]  =(((sr>>4)&1)+((sr>>1)&1)+((sr>>0)&1))&1;				//G_1
		outp[i*2+1]=(((sr>>4)&1)+((sr>>3)&1)+((sr>>2)&1)+((sr>>0)&1))&1;	//G_2
		//printf("%02X\t%d\t%d\n", sr, outp[i*2], outp[i*2+1]);
	}
}



//puncture LSF type-2 bits into type-3 bits using P_1 puncturer scheme
void puncture_LSF(uint8_t *outp, uint8_t *inp)
{
	const uint8_t punct[61]={	1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 
								1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 
								1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, };

	//to puncture the LSF type-3 bits properly, we have to use puncturing matrix above
	//it has 61 elements, so we have to use them going along from the beginning to the end
	//then again and again, repeating this for a total of eight times, giving 488 elements we need
	
	uint16_t n=0;	//how many bits actually went through the puncturer
	
	for(uint16_t i=0; i<488; i++)
	{
		if(punct[i%61])
		{
			outp[n]=inp[i];
			n++;
		}
	}

	//make sure we have 368
	//printf("n=%d\n", n);
}

//puncture FRAME type-2 bits into type-3 bits using P_2 puncturer scheme
void puncture_Frame(uint8_t *outp, uint8_t *inp)
{
	const uint8_t punct[12]={ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 };

	//to puncture the FRAME type-3 bits properly, we have to use puncturing matrix above
	//it has 12 elements, so we have to use them going along from the beginning to the end
	//then again and again, giving 272 elements we need
	
	uint16_t n=0;	//how many bits actually went through the puncturer
	
	for(uint16_t i=0; i<296; i++)
	{
		if(punct[i%12])
		{
			outp[n]=inp[i];
			n++;
		}
	}

	//make sure we have 272
	//printf("n=%d\n", n);
}

//interleaver and decorrelator - both are the same for all data frames
//interleave 
void interleave(uint8_t *outp, uint8_t *inp)
{
	for(uint16_t i=0; i<368; i++)
	{
		outp[i]=inp[(45UL*i+92UL*i*i)%368];
	}
}

//decorrelate
void decorrelate(uint8_t *outp, uint8_t *inp)
{
	const uint8_t pattern[46]= { 	0xD6, 0xB5, 0xE2, 0x30, 0x82, 0xFF, 0x84, 0x62, 0xBA, 0x4E, 0x96, 0x90, 0xD8, 0x98, 0xDD, 0x5D, 0x0C, 0xC8, 0x52, 0x43, 0x91, 0x1D, 0xF8,
									0x6E, 0x68, 0x2F, 0x35, 0xDA, 0x14, 0xEA, 0xCD, 0x76, 0x19, 0xBD, 0xD5, 0x80, 0xD1, 0x33, 0x87, 0x13, 0x57, 0x18, 0x2D, 0x29, 0x78, 0xC3 };

	for(uint16_t i=0; i<368; i++)
	{
		outp[i]=inp[i]^((pattern[i/8]>>(7-(i%8)))&1);
	}
}

//generate symbols from data
//prepend syncword
void symbols_LSF(int16_t *outp, uint8_t *inp)
{
	for(uint8_t i=0; i<8; i++)
	{
		outp[i]=symbol_map[(SYNC_LSF>>(14-(2*i)))&3];
	}
	for(uint16_t i=8; i<192; i++)
	{
		outp[i]=symbol_map[inp[2*(i-8)]*2+inp[2*(i-8)+1]];
	}
}

void symbols_Frame(int16_t *outp, uint8_t *inp)
{
	for(uint8_t i=0; i<8; i++)
	{
		outp[i]=symbol_map[(SYNC_STR>>(14-(2*i)))&3];
	}
	for(uint16_t i=8; i<192; i++)
	{
		outp[i]=symbol_map[inp[2*(i-8)]*2+inp[2*(i-8)+1]];
	}
}

//main routine
int main(int argc, uint8_t *argv[])
{
	struct LSF lsf;
	
	//init
	CRC_init(CRC_LUT, crc_poly);
	
	if(argc<3)
	{
		printf("Not enough params.\n\nUsage:\n./this DEST SRC\n\nExiting.");
		return 1;
	}
	
	memcpy(dst_ascii, &argv[1][0], strlen(argv[1]));
	memcpy(src_ascii, &argv[2][0], strlen(argv[2]));
	
	//encode callsigns
	lsf.dst=callsign_encode(dst_ascii);
	lsf.src=callsign_encode(src_ascii);
	
	//printf("LSF.DST: 0x%012X (%s)\n", lsf.dst, dst_ascii);
	//printf("LSF.SRC: 0x%012X (%s)\n", lsf.src, src_ascii);
	
	//set stream parameters
	lsf.type=TYPE_STR|TYPE_V|TYPE_ENCR_NONE|TYPE_ENCR_SUB_NONE|(0b0000<<CAN)|TYPE_RESERVED;
	
	//zero out the META field for now
	memset((uint8_t*)&(lsf.meta), 0, 14);
	
	//calculate CRC
	lsf.crc=CRC_LSF(&lsf);
	//printf("\nCRC: %04X\n", lsf.crc);
	
	//test CRC against test vector
	/*uint8_t str[9]="123456789";
	printf("STR CRC: %04X\n", CRC_M17(CRC_LUT, str, 9));*/
	
	//unpack LSF
	uint8_t unpacked_lsf[240];				//type-1 bits
	unpack_LSF(unpacked_lsf, &lsf);
	
	/*printf("\n\nLSF:\n");
	for(uint8_t i=0; i<240; i++)
	{
		if(!(i%8) && i>1)
			printf("\n");
		printf("%d", unpacked_lsf[i]);
	}*/
	
	//printf("\n\n");
	uint8_t unpacked_convolved_lsf[488];	//type-2 bits
	convolve(unpacked_convolved_lsf, unpacked_lsf, 240);
	
	/*printf("\n\nCONVOL:\n");
	for(uint16_t i=0; i<488; i++)
	{
		if(!(i%8) && i>1)
			printf("\n");
		printf("%d", unpacked_convolved_lsf[i]);
	}*/
	
	uint8_t unpacked_punctured_lsf[368]; 	//type-3 bits
	puncture_LSF(unpacked_punctured_lsf, unpacked_convolved_lsf);
	
	/*printf("\n\nPUNCT:\n");
	for(uint16_t i=0; i<368; i++)
	{
		if(!(i%8) && i>1)
			printf("\n");
		printf("%d", unpacked_punctured_lsf[i]);
	}*/
	
	uint8_t unpacked_interleaved_lsf[368];
	interleave(unpacked_interleaved_lsf, unpacked_punctured_lsf);
	
	uint8_t unpacked_decorrelated_lsf[368];	//type-4 bits
	decorrelate(unpacked_decorrelated_lsf, unpacked_interleaved_lsf);
	
	//time to generate 192 LSF symbols
	int16_t LSF_symbols[192];
	symbols_LSF(LSF_symbols, unpacked_decorrelated_lsf);
	
	//spit out 40ms preamble and the LSF at stdout
	//Little-Endian
	for(uint8_t i=0; i<192; i++)
	{
		int16_t symbol=0;
		
		(i%2)?(symbol=3*5461):(symbol=-3*5461);
		printf("%c%c", symbol&0xFF, (symbol>>8)&0xFF);
	}
	
	for(uint8_t i=0; i<192; i++)
	{
		LSF_symbols[i]*=5461;	//boost it up a little, make symbols +/-3 have an amplitude of 0.5 (32767/2/3=~5461)
		printf("%c%c", (LSF_symbols[i])&0xFF, (LSF_symbols[i]>>8)&0xFF);
	}
	
	//3 dummy symbols (a carrier, actually) for RRC flushing
	for(uint8_t i=0; i<3; i++)
		printf("%c%c", 0, 0);

	return 0;
}
