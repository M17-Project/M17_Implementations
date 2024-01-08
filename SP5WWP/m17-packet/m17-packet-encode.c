#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

//libm17
#include <m17/m17.h>

#define FLT_LEN         (BSB_SPS*FLT_SPAN+1)                //for 48kHz sample rate this is 81

struct LSF lsf;

uint8_t enc_bits[SYM_PER_PLD*2];                            //type-2 bits, unpacked
uint8_t rf_bits[SYM_PER_PLD*2];                             //type-4 bits, unpacked

uint8_t dst_raw[10]={'A', 'L', 'L', '\0'};                  //raw, unencoded destination address
uint8_t src_raw[10]={'N', '0', 'C', 'A', 'L', 'L', '\0'};   //raw, unencoded source address
uint8_t can=0;                                              //Channel Access Number, default: 0
uint16_t num_bytes=0;                                       //number of bytes in packet, max 800-2=798
uint8_t fname[128]={'\0'};                                  //output file

FILE* fp;
float full_packet[6912+88];                                 //full packet, symbols as floats - (40+40+32*40+40+40)/1000*4800
                                                            //pream, LSF, 32 frames, ending frame, EOT plus RRC flushing
uint16_t pkt_sym_cnt=0;                                     //packet symbol counter, used to fill the packet
uint8_t pkt_cnt=0;                                          //packet frame counter (1..32) init'd at 0
uint8_t pkt_chunk[25+1];                                    //chunk of Packet Data, up to 25 bytes plus 6 bits of Packet Metadata
uint8_t full_packet_data[32*25];                            //full packet data, bytes
uint8_t out_type=0;                                         //output file type - 0 - raw int16 filtered samples (.rrc) - default
                                                            //                   1 - int16 symbol stream
                                                            //                   2 - binary stream (TODO)

//type - 0 - preamble before LSF (standard)
//type - 1 - preamble before BERT transmission
void fill_preamble(float* out, const uint8_t type)
{
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

void fill_syncword(float* out, uint16_t* cnt, const uint16_t syncword)
{
    float symb=0.0f;

    for(uint8_t i=0; i<16; i+=2)
    {
        symb=symbol_map[(syncword>>(14-i))&3];
        out[*cnt]=symb;
        (*cnt)++;
    }
}

//fill packet symbols array with data (can be used for both LSF and frames)
void fill_data(float* out, uint16_t* cnt, const uint8_t* in)
{
	float symb=0.0f;

	for(uint16_t i=0; i<SYM_PER_PLD; i++) //40ms * 4800 - 8 (syncword)
	{
		symb=symbol_map[in[2*i]*2+in[2*i+1]];
		out[*cnt]=symb;
		(*cnt)++;
	}
}

//main routine
int main(int argc, char* argv[])
{
    //scan command line options for input data
    //TODO: support for strings with spaces, the code below is NOT foolproof!
    //the user has to provide a minimum of 2 parameters: number of bytes and output filename
    if(argc>=4)
    {
        for(uint8_t i=1; i<argc; i++)
        {
            if(argv[i][0]=='-')
            {
                if(argv[i][1]=='D') //-D - destination
                {
                    if(strlen(argv[i+1])<=9)
                        strcpy((char*)dst_raw, argv[i+1]);
                    else
                    {
                        fprintf(stderr, "Too long destination callsign.\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='S') //-S - source
                {
                    if(strlen(argv[i+1])<=9)
                        strcpy((char*)src_raw, argv[i+1]);
                    else
                    {
                        fprintf(stderr, "Too long source callsign.\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='C') //-C - CAN
                {
                    if(atoi(&argv[i+1][0])<=15)
                        can=atoi(&argv[i+1][0]);
                    else
                    {
                        fprintf(stderr, "CAN out of range: 0..15.\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='n') //-n - number of bytes in packet
                {
                    if(atoi(argv[i+1])>0 && atoi(argv[i+1])<=(800-2))
                        num_bytes=atoi(&argv[i+1][0]);
                    else
                    {
                        fprintf(stderr, "Number of bytes 0 or exceeding the maximum of 798. Exiting...\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='o') //-o - output filename
                {
                    if(strlen(&argv[i+1][0])>0)
                        memcpy(fname, &argv[i+1][0], strlen(argv[i+1]));
                    else
                    {
                        fprintf(stderr, "Invalid filename. Exiting...\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='r') //-r - raw filtered output
                {
                    out_type=0; //default
                }
                else if(argv[i][1]=='s') //-s - symbols output
                {
                    out_type=1;
                }
                else if(argv[i][1]=='x') //-x - binary output
                {
                    out_type=2;
                }
                else
                {
                    fprintf(stderr, "Unknown param detected. Exiting...\n");
                    return -1; //unknown option
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Not enough params. Usage:\n");
        fprintf(stderr, "-S - source callsign (uppercase alphanumeric string) max. 9 characters,\n");
        fprintf(stderr, "-D - destination callsign (uppercase alphanumeric string) or ALL for broadcast,\n");
        fprintf(stderr, "-C - Channel Access Number (0..15, default - 0),\n");
        fprintf(stderr, "-n - number of bytes (1 to 798),\n");
        fprintf(stderr, "-o - output file path/name,\n");
        fprintf(stderr, "Output formats:\n");
        //fprintf(stderr, "-x - binary output (M17 baseband as a packed bitstream),\n");
        fprintf(stderr, "-r - raw audio output - default (single channel, signed 16-bit LE, +7168 for the +1.0 symbol, 10 samples per symbol),\n");
        fprintf(stderr, "-s - signed 16-bit LE symbols output\n");
        return -1;
    }

    //assert number of bytes and filename
    if(num_bytes==0)
    {
        fprintf(stderr, "Number of bytes not set. Exiting...\n");
        return -1;
    }
    else if(strlen((const char*)fname)==0)
    {
        fprintf(stderr, "Filename not specified. Exiting...\n");
        return -1;
    }
    else if(out_type==2)
    {
        fprintf(stderr, "Binary output file type not supported yet. Exiting...\n");
        return -1;
    }

    //obtain data and append with CRC
    memset(full_packet_data, 0, 32*25);
    if(fread(full_packet_data, num_bytes, 1, stdin)<1)
    {
        fprintf(stderr, "Packet data too short. Exiting...\n");
        return -1;
    }
    uint16_t packet_crc=CRC_M17(full_packet_data, num_bytes);
    full_packet_data[num_bytes]  =packet_crc>>8;
    full_packet_data[num_bytes+1]=packet_crc&0xFF;
    num_bytes+=2; //count 2-byte CRC too

    //encode dst, src for the lsf struct
    uint64_t dst_encoded=0, src_encoded=0;
    uint16_t type=0;
    encode_callsign_value(&dst_encoded, dst_raw);
    encode_callsign_value(&src_encoded, src_raw);
    for(int8_t i=5; i>=0; i--)
    {
        lsf.dst[5-i]=(dst_encoded>>(i*8))&0xFF;
        lsf.src[5-i]=(src_encoded>>(i*8))&0xFF;
    }
    #ifdef __ARM_ARCH_6__
    fprintf(stderr, "DST: %s\t%012llX\nSRC: %s\t%012llX\n", dst_raw, dst_encoded, src_raw, src_encoded);
    #else
    fprintf(stderr, "DST: %s\t%012lX\nSRC: %s\t%012lX\n", dst_raw, dst_encoded, src_raw, src_encoded);
    #endif
    //fprintf(stderr, "DST: %02X %02X %02X %02X %02X %02X\n", lsf.dst[0], lsf.dst[1], lsf.dst[2], lsf.dst[3], lsf.dst[4], lsf.dst[5]);
    //fprintf(stderr, "SRC: %02X %02X %02X %02X %02X %02X\n", lsf.src[0], lsf.src[1], lsf.src[2], lsf.src[3], lsf.src[4], lsf.src[5]);
    fprintf(stderr, "Data CRC:\t%04hX\n", packet_crc);
    type=((uint16_t)0x01<<1)|((uint16_t)can<<7); //packet mode, content: data
    lsf.type[0]=(uint16_t)type>>8;
    lsf.type[1]=(uint16_t)type&0xFF;
    memset(&lsf.meta, 0, 112/8);

    //calculate LSF CRC
    uint16_t lsf_crc=LSF_CRC(&lsf);
    lsf.crc[0]=lsf_crc>>8;
    lsf.crc[1]=lsf_crc&0xFF;
    fprintf(stderr, "LSF CRC:\t%04hX\n", lsf_crc);

    //encode LSF data
    conv_encode_LSF(enc_bits, &lsf);

    //fill preamble
    memset((uint8_t*)full_packet, 0, sizeof(float)*(6912+88));
    fill_preamble(full_packet, 0);
    pkt_sym_cnt=SYM_PER_FRA;

    //send LSF syncword
    fill_syncword(full_packet, &pkt_sym_cnt, SYNC_LSF);

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

    //fill packet with LSF
    fill_data(full_packet, &pkt_sym_cnt, rf_bits);

    //read Packet Data from variable
    pkt_cnt=0;
    uint16_t tmp=num_bytes;
    while(num_bytes)
    {
        //send packet frame syncword
        fill_syncword(full_packet, &pkt_sym_cnt, SYNC_PKT);

        if(num_bytes>=25)
        {
            memcpy(pkt_chunk, &full_packet_data[pkt_cnt*25], 25);
            pkt_chunk[25]=pkt_cnt<<2;
            fprintf(stderr, "FN:%02d (full frame)\n", pkt_cnt);

            //encode the packet frame
            conv_encode_packet_frame(enc_bits, pkt_chunk);

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

            //fill packet with frame data
            fill_data(full_packet, &pkt_sym_cnt, rf_bits);

            num_bytes-=25;
        }
        else
        {
            memcpy(pkt_chunk, &full_packet_data[pkt_cnt*25], num_bytes);
            memset(&pkt_chunk[num_bytes], 0, 25-num_bytes); //zero-padding
            pkt_chunk[25]=(1<<7)|(((num_bytes%25==0)?25:num_bytes%25)<<2); //EOT bit set to 1, set counter to the amount of bytes in this (the last) frame

            fprintf(stderr, "FN:-- (ending frame)\n");

            //encode the packet frame
            conv_encode_packet_frame(enc_bits, pkt_chunk);

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

            //fill packet with frame data
            fill_data(full_packet, &pkt_sym_cnt, rf_bits);

            num_bytes=0;
        }

        //debug dump
        //for(uint8_t i=0; i<26; i++)
            //fprintf(stderr, "%02X", pkt_chunk[i]);
        //fprintf(stderr, "\n");

        pkt_cnt++;
    }

    num_bytes=tmp; //bring back the num_bytes value

    //fprintf(stderr, "DATA: %s\n", full_packet_data);

    //send EOT
    for(uint8_t i=0; i<SYM_PER_FRA/SYM_PER_SWD; i++) //192/8=24
        fill_syncword(full_packet, &pkt_sym_cnt, EOT_MRKR);

    //dump baseband to a file
    fp=fopen((const char*)fname, "wb");

    //debug mode - symbols multiplied by 7168 scaling factor
    /*for(uint16_t i=0; i<pkt_sym_cnt; i++)
    {
        int16_t val=roundf(full_packet[i]*RRC_DEV);
        fwrite(&val, 2, 1, fp);
    }*/

    //standard mode - filtered baseband
    if(out_type==0)
    {
        float mem[FLT_LEN];
        float mac=0.0f;
        memset((uint8_t*)mem, 0, FLT_LEN*sizeof(float));
        for(uint16_t i=0; i<pkt_sym_cnt; i++)
        {
            //push new sample
            mem[0]=full_packet[i]*RRC_DEV;

            for(uint8_t j=0; j<10; j++)
            {
                mac=0.0f;

                //calc the sum of products
                for(uint16_t k=0; k<FLT_LEN; k++)
                    mac+=mem[k]*rrc_taps_10[k]*sqrtf(10.0); //temporary fix for the interpolation gain error

                //shift the delay line right by 1
                for(int16_t k=FLT_LEN-1; k>0; k--)
                {
                    mem[k]=mem[k-1];
                }
                mem[0]=0.0f;

                //write to file
                int16_t tmp=mac;
                fwrite((uint8_t*)&tmp, 2, 1, fp);
            }
        }
    }
    //standard mode - int16 symbol stream
    else if(out_type==1)
    {
        for(uint16_t i=0; i<pkt_sym_cnt; i++)
        {
            int16_t val=full_packet[i];
            fwrite(&val, 2, 1, fp);
        }
    }

    fclose(fp);

	return 0;
}
