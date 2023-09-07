#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>

#include "../inc/m17.h"
#include "crc.h"

struct LSF
{
	uint8_t dst[6];
	uint8_t src[6];
	uint8_t type[2];
	uint8_t meta[112/8];
	uint8_t crc[2];
} lsf;

uint8_t enc_bits[SYM_PER_PLD*2];                            //type-2 bits, unpacked
uint8_t rf_bits[SYM_PER_PLD*2];                             //type-4 bits, unpacked

uint8_t dst_raw[10]={'A', 'L', 'L', '\0'};                  //raw, unencoded destination address
uint8_t src_raw[10]={'N', '0', 'C', 'A', 'L', 'L', '\0'};   //raw, unencoded source address
uint8_t can=0;                                              //Channel Access Number, default: 0
uint16_t num_bytes=0;                                       //number of bytes in packet, max 800-2=798
uint8_t data[25];                                           //raw payload, packed bits
uint8_t fname[128]={'\0'};                                  //output file

FILE* fp;
float full_packet[6912+88];                                 //full packet, symbols as floats - (40+40+32*40+40+40)/1000*4800
                                                            //pream, LSF, 32 frames, ending frame, EOT plus RRC flushing

//type - 0 - preamble before LSF (standard)
//type - 1 - preamble before BERT transmission
void fill_Preamble(float* out, const uint8_t type)
{
    float symb;

    if(type) //pre-BERT
    {
        for(uint16_t i=0; i<SYM_PER_FRA/2; i++) //40ms * 4800 = 192
        {
            out[2*i]  =-3.0;
            out[2*i+1]=+3.0;
        }
    }
    else //pre-LSF
    {
        for(uint16_t i=0; i<SYM_PER_FRA/2; i++) //40ms * 4800 = 192
        {
            out[2*i]  =+3.0;
            out[2*i+1]=-3.0;
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

//send the data (can be used for both LSF and frames)
void send_data(uint8_t* in)
{
	float s=0.0;
	for(uint16_t i=0; i<SYM_PER_PLD; i++) //40ms * 4800 - 8 (syncword)
	{
		s=symbol_map[in[2*i]*2+in[2*i+1]];
		write(STDOUT_FILENO, (uint8_t*)&s, sizeof(float));
	}
}

//out - unpacked bits
//in - packed raw bits
//fn - frame number
void conv_Encode_Frame(uint8_t* out, uint8_t* in, uint16_t fn)
{
	uint8_t pp_len = sizeof(P_2);
	uint8_t p=0;			//puncturing pattern index
	uint16_t pb=0;			//pushed punctured bits
	uint8_t ud[144+4+4];	//unpacked data

	memset(ud, 0, 144+4+4);

	//unpack frame number
	for(uint8_t i=0; i<16; i++)
	{
		ud[4+i]=(fn>>(15-i))&1;
	}

	//unpack data
	for(uint8_t i=0; i<16; i++)
	{
		for(uint8_t j=0; j<8; j++)
		{
			ud[4+16+i*8+j]=(in[i]>>(7-j))&1;
		}
	}

	//encode
	for(uint8_t i=0; i<144+4; i++)
	{
		uint8_t G1=(ud[i+4]                +ud[i+1]+ud[i+0])%2;
        uint8_t G2=(ud[i+4]+ud[i+3]+ud[i+2]        +ud[i+0])%2;

		//printf("%d%d", G1, G2);

		if(P_2[p])
		{
			out[pb]=G1;
			pb++;
		}

		p++;
		p%=pp_len;

		if(P_2[p])
		{
			out[pb]=G2;
			pb++;
		}

		p++;
		p%=pp_len;
	}

	//printf("pb=%d\n", pb);
}

//out - unpacked bits
//in - packed raw bits (LSF struct)
void conv_Encode_LSF(uint8_t* out, struct LSF *in)
{
	uint8_t pp_len = sizeof(P_1);
	uint8_t p=0;			//puncturing pattern index
	uint16_t pb=0;			//pushed punctured bits
	uint8_t ud[240+4+4];	//unpacked data

	memset(ud, 0, 240+4+4);

	//unpack DST
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i]   =((in->dst[0])>>(7-i))&1;
		ud[4+i+8] =((in->dst[1])>>(7-i))&1;
		ud[4+i+16]=((in->dst[2])>>(7-i))&1;
		ud[4+i+24]=((in->dst[3])>>(7-i))&1;
		ud[4+i+32]=((in->dst[4])>>(7-i))&1;
		ud[4+i+40]=((in->dst[5])>>(7-i))&1;
	}

	//unpack SRC
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+48]=((in->src[0])>>(7-i))&1;
		ud[4+i+56]=((in->src[1])>>(7-i))&1;
		ud[4+i+64]=((in->src[2])>>(7-i))&1;
		ud[4+i+72]=((in->src[3])>>(7-i))&1;
		ud[4+i+80]=((in->src[4])>>(7-i))&1;
		ud[4+i+88]=((in->src[5])>>(7-i))&1;
	}

	//unpack TYPE
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+96] =((in->type[0])>>(7-i))&1;
		ud[4+i+104]=((in->type[1])>>(7-i))&1;
	}

	//unpack META
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+112]=((in->meta[0])>>(7-i))&1;
		ud[4+i+120]=((in->meta[1])>>(7-i))&1;
		ud[4+i+128]=((in->meta[2])>>(7-i))&1;
		ud[4+i+136]=((in->meta[3])>>(7-i))&1;
		ud[4+i+144]=((in->meta[4])>>(7-i))&1;
		ud[4+i+152]=((in->meta[5])>>(7-i))&1;
		ud[4+i+160]=((in->meta[6])>>(7-i))&1;
		ud[4+i+168]=((in->meta[7])>>(7-i))&1;
		ud[4+i+176]=((in->meta[8])>>(7-i))&1;
		ud[4+i+184]=((in->meta[9])>>(7-i))&1;
		ud[4+i+192]=((in->meta[10])>>(7-i))&1;
		ud[4+i+200]=((in->meta[11])>>(7-i))&1;
		ud[4+i+208]=((in->meta[12])>>(7-i))&1;
		ud[4+i+216]=((in->meta[13])>>(7-i))&1;
	}

	//unpack CRC
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+224]=((in->crc[0])>>(7-i))&1;
		ud[4+i+232]=((in->crc[1])>>(7-i))&1;
	}

	//encode
	for(uint8_t i=0; i<240+4; i++)
	{
		uint8_t G1=(ud[i+4]                +ud[i+1]+ud[i+0])%2;
        uint8_t G2=(ud[i+4]+ud[i+3]+ud[i+2]        +ud[i+0])%2;

		//printf("%d%d", G1, G2);

		if(P_1[p])
		{
			out[pb]=G1;
			pb++;
		}

		p++;
		p%=pp_len;

		if(P_1[p])
		{
			out[pb]=G2;
			pb++;
		}

		p++;
		p%=pp_len;
	}

	//printf("pb=%d\n", pb);
}

uint16_t LSF_CRC(struct LSF *in)
{
    uint8_t d[28];

    memcpy(&d[0], in->dst, 6);
    memcpy(&d[6], in->src, 6);
    memcpy(&d[12], in->type, 2);
    memcpy(&d[14], in->meta, 14);

    return CRC_M17(d, 28);
}

//encode callsign
uint8_t encode_callsign(uint64_t* out, const uint8_t* inp)
{
    //assert inp length
    if(strlen(inp)>9)
    {
        return -1;
    }

    const uint8_t charMap[40]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

    uint64_t tmp=0;

    if(strcmp(inp, "ALL")==0)
    {
        *out=0xFFFFFFFFFFFF;
        return 0;
    }

    for(int8_t i=strlen(inp)-1; i>=0; i--)
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

//main routine
int main(int argc, char* argv[])
{
    //scan command line options for input data
    //TODO: support for strings with spaces, the code below is NOT foolproof!
    //the user has to provide a minimum of 2 parameters: number of bytes and output filename
    if(argc>=4)
    {
        for(uint8_t i=1; i<argc-1; i++)
        {
            if(argv[i][0]=='-')
            {
                if(argv[i][1]=='d') //-d - destination
                {
                    if(strlen(argv[i+1])<=9)
                        memcpy(dst_raw, &argv[i+1][0], strlen(&argv[i+1][0]));
                    else
                    {
                        fprintf(stderr, "Too long destination callsign.\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='s') //-s - source
                {
                    if(strlen(argv[i+1])<=9)
                        memcpy(src_raw, &argv[i+1][0], strlen(&argv[i+1][0]));
                    else
                    {
                        fprintf(stderr, "Too long source callsign.\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='c') //-c - CAN
                {
                    if(atoi(argv[i+1])<=15)
                        can=atoi(&argv[i+1]);
                    else
                    {
                        fprintf(stderr, "CAN out of range: 0..15.\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='n') //-n - number of bytes in packet
                {
                    if(atoi(argv[i+1])>0 && atoi(argv[i+1])<=798)
                        num_bytes=atoi(argv[i+1]);
                    else
                    {
                        fprintf(stderr, "Number of bytes 0 or exceeding the maximum of 798. Exiting...\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='o') //-o - output filename
                {
                    if(strlen(argv[i+1])>0)
                        memcpy(fname, &argv[i+1][0], strlen(argv[i+1]));
                    else
                    {
                        fprintf(stderr, "Invalid filename. Exiting...\n");
                        return -1;
                    }
                }
                else
                {
                    fprintf(stderr, "Unknown param. Exiting...\n");
                    return -1; //unknown option
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Not enough params. Exiting...\n");
        return -1;
    }

    //assert number of bytes and filename
    if(num_bytes==0)
    {
        fprintf(stderr, "Number of bytes not set. Exiting...\n");
        return -1;
    }
    else if(strlen(fname)==0)
    {
        fprintf(stderr, "Filename not specified. Exiting...\n");
        return -1;
    }

    //encode dst, src for the lsf struct
    uint64_t dst_encoded=0, src_encoded=0;
    uint16_t type=0;
    encode_callsign(&dst_encoded, dst_raw);
    encode_callsign(&src_encoded, src_raw);
    for(int8_t i=5; i>=0; i--)
    {
        lsf.dst[5-i]=(dst_encoded>>(i*8))&0xFF;
        lsf.src[5-i]=(src_encoded>>(i*8))&0xFF;
    }
    printf("DST: %s\t%012lX\nSRC: %s\t%012lX\n", dst_raw, dst_encoded, src_raw, src_encoded);
    //printf("DST: %02X %02X %02X %02X %02X %02X\n", lsf.dst[0], lsf.dst[1], lsf.dst[2], lsf.dst[3], lsf.dst[4], lsf.dst[5]);
    //printf("SRC: %02X %02X %02X %02X %02X %02X\n", lsf.src[0], lsf.src[1], lsf.src[2], lsf.src[3], lsf.src[4], lsf.src[5]);
    type=((uint16_t)0b01<<1)|((uint16_t)can<<7); //packet mode, content: data
    lsf.type[0]=(uint16_t)type>>8;
    lsf.type[1]=(uint16_t)type&0xFF;
    memset(&lsf.meta, 0, 112/8);

    //encode LSF data
    conv_Encode_LSF(enc_bits, &lsf);

    //fill preamble
    memset((uint8_t*)full_packet, 0, sizeof(float)*(6912+88));
    fill_Preamble(full_packet, 0);

    /*
    //send LSF syncword
    send_Syncword(SYNC_LSF);

    //reorder bits
    for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
        rf_bits[i]=enc_bits[intrl_seq[i]];

    //randomize
    for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
    {
        if((rand_seq[i/8]>>(7-(i%8)))&1) //flip bit if '1'
        {
            if(rf_bits[i])
                rf_bits[i]=0;
            else
                rf_bits[i]=1;
        }
    }

    //send LSF
    send_data(rf_bits);

    //encode the packet frame
    conv_Encode_Frame();

    //send packet frame syncword
    send_Syncword(SYNC_PKT);

    //reorder bits
    for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
        rf_bits[i]=enc_bits[intrl_seq[i]];

    //randomize
    for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
    {
        if((rand_seq[i/8]>>(7-(i%8)))&1) //flip bit if '1'
        {
            if(rf_bits[i])
                rf_bits[i]=0;
            else
                rf_bits[i]=1;
        }
    }

	//send packet frame data
	send_data(rf_bits);
    */

    fp=fopen(fname, "wb");
    for(uint16_t i=0; i<SYM_PER_FRA; i++)
    {
        int16_t val=roundf(full_packet[i]*RRC_DEV);
        fwrite(&val, 2, 1, fp);
    }
    fclose(fp);

	return 0;
}
