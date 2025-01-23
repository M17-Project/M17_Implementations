#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sndfile.h>

//libm17
#include <m17.h>

#define FLT_LEN         (BSB_SPS*FLT_SPAN+1)                //for 48kHz sample rate this is 81

lsf_t lsf;

char wav_name[1024];                                        //name of wav file to output to
SNDFILE *wav;                                               //sndfile wav file
SF_INFO info;                                               //sndfile parameter struct
int len=3;                                                  //number of blocks produced via counter +3 for pre,lsf, and eot marker

uint8_t enc_bits[SYM_PER_PLD*2];                            //type-2 bits, unpacked
uint8_t rf_bits[SYM_PER_PLD*2];                             //type-4 bits, unpacked

uint8_t dst_raw[10]={'A', 'L', 'L', '\0'};                  //raw, unencoded destination address
uint8_t src_raw[10]={'N', '0', 'C', 'A', 'L', 'L', '\0'};   //raw, unencoded source address
uint8_t can=0;                                              //Channel Access Number, default: 0
uint16_t num_bytes=0;                                       //number of bytes in packet, max (33 frames * 25 bytes) - 2 (CRC) = 823
                                                            //Note: This value is 823 when using echo -en pre-encoded data or -R raw data as that already includes the protocol and 0x00 terminator
                                                            //When reading this string in from arg -T, the value is 821 as the 0x05 and 0x00 is added after the string conversion to octets
uint8_t fname[128]={'\0'};                                  //output file

FILE* fp;
float full_packet[36*192*10];                               //full packet, symbols as floats - 36 "frames" max (incl. preamble, LSF, EoT), 192 symbols each, sps=10:
                                                            //pream, LSF, 32 frames, ending frame, EOT plus RRC flushing
uint32_t pkt_sym_cnt=0;                                     //packet symbol counter, used to fill the packet
uint8_t pkt_cnt=0;                                          //packet frame counter (1..32) init'd at 0
uint8_t pkt_chunk[25+1];                                    //chunk of Packet Data, up to 25 bytes plus 6 bits of Packet Metadata
uint8_t full_packet_data[33*25];                            //full packet data, bytes

typedef enum                                                //output file type
{
    OUT_TYPE_S16_RAW,                                       //0 - raw int16 filtered samples (.rrc) - default
    OUT_TYPE_S16_SYMB,                                      //1 - int16 symbol stream
    OUT_TYPE_BIN,                                           //2 - binary stream (TODO)
    OUT_TYPE_UPS_NO_FLT,                                    //3 - simple 10x upsample no filter
    OUT_TYPE_S16_RRC,                                       //4 - S16-LE RRC filtered wav file
    OUT_TYPE_FLOAT                                          //5 - float symbol output for m17-packet-decode
} out_type_t;
out_type_t out_type=OUT_TYPE_S16_RAW;                       //output file type - raw int16 filtered samples (.rrc) - default

uint8_t std_encode = 1;                                     //User Data is pre-encoded and read in over stdin, and not a switch string
uint8_t sms_encode = 0;                                     //User Supplied Data is an SMS Text message, encode as such
uint8_t raw_encode = 0;                                     //User Supplied Data is a string of hex octets, encode as such

char   text[825] = "Default SMS Text message";              //SMS Text to Encode, default string.
uint8_t raw[825];                                           //raw data that is converted from a string comprised of hex octets

//convert a user string (as hex octets) into a uint8_t array
uint16_t parse_raw_hex_string(uint8_t* dest, const char* inp)
{
    uint16_t len = strlen(inp);

    if(len==0) return 0; //invalid length

    //memset(dest, 0, len/2); //one character represents half of a byte

    if(!(len%2)) //length even?
    {
        for(uint8_t i=0; i<len; i+=2)
        {
            if(inp[i]>='a')
                dest[i/2]|=(inp[i]-'a'+10)*0x10;
            else if(inp[i]>='A')
                dest[i/2]|=(inp[i]-'A'+10)*0x10;
            else if(inp[i]>='0')
                dest[i/2]|=(inp[i]-'0')*0x10;
            
            if(inp[i+1]>='a')
                dest[i/2]|=inp[i+1]-'a'+10;
            else if(inp[i+1]>='A')
                dest[i/2]|=inp[i+1]-'A'+10;
            else if(inp[i+1]>='0')
                dest[i/2]|=inp[i+1]-'0';
        }
    }
    else
    {
        if(inp[0]>='a')
            dest[0]|=inp[0]-'a'+10;
        else if(inp[0]>='A')
            dest[0]|=inp[0]-'A'+10;
        else if(inp[0]>='0')
            dest[0]|=inp[0]-'0';

        for(uint8_t i=1; i<len-1; i+=2)
        {
            if(inp[i]>='a')
                dest[i/2+1]|=(inp[i]-'a'+10)*0x10;
            else if(inp[i]>='A')
                dest[i/2+1]|=(inp[i]-'A'+10)*0x10;
            else if(inp[i]>='0')
                dest[i/2+1]|=(inp[i]-'0')*0x10;
            
            if(inp[i+1]>='a')
                dest[i/2+1]|=inp[i+1]-'a'+10;
            else if(inp[i+1]>='A')
                dest[i/2+1]|=inp[i+1]-'A'+10;
            else if(inp[i+1]>='0')
                dest[i/2+1]|=inp[i+1]-'0';
        }
    }

    return len%2 ? (len+1)/2 : len/2; //return how many bytes are used
}

//main routine
int main(int argc, char* argv[])
{
    //scan command line options for input data
    //WIP: support for text strings with spaces and raw hex octet strings (still NOT foolproof)
    //the user has to provide a minimum of 2 parameters: input string or num_bytes, output type, and output filename
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
                    if(atoi(argv[i+1])>0 && atoi(argv[i+1])<=(825-2))
                        num_bytes=atoi(&argv[i+1][0]);
                    else
                    {
                        fprintf(stderr, "Number of bytes 0 or exceeding the maximum of 823. Exiting...\n");
                        return -1;
                    }
                }
                else if(argv[i][1]=='T') //-T - User Text String
                {
                    if(strlen(&argv[i+1][0])>0)
                    {
                        memset(text, 0, 825*sizeof(char));
                        memcpy(text, argv[i+1], strlen(argv[i+1])<=821 ? strlen(argv[i+1]) : 821);
                        std_encode = 0;
                        sms_encode = 1;
                        raw_encode = 0;
                    }
                }
                else if(argv[i][1]=='R') //-R - Raw Octets
                {
                    if(strlen(&argv[i+1][0])>0)
                    {
                        memset(raw, 0, sizeof(raw));
                        num_bytes=parse_raw_hex_string(raw, argv[i+1]);
                        //fprintf(stderr, "num_bytes=%d\n", num_bytes);
                        std_encode = 0;
                        sms_encode = 0;
                        raw_encode = 1;
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
                    out_type=OUT_TYPE_S16_RAW; //default
                }
                else if(argv[i][1]=='s') //-s - symbols output
                {
                    out_type=OUT_TYPE_S16_SYMB;
                }
                else if(argv[i][1]=='x') //-x - binary output
                {
                    out_type=OUT_TYPE_BIN;
                }
                else if(argv[i][1]=='d') //-d - raw unfiltered output to wav file
                {
                    out_type=OUT_TYPE_UPS_NO_FLT;
                }
                else if(argv[i][1]=='w') //-w - rrc filtered output to wav file
                {
                    out_type=OUT_TYPE_S16_RRC;
                }
                else if(argv[i][1]=='f') //-f - float symbol output
                {
                    out_type=OUT_TYPE_FLOAT;
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
        fprintf(stderr, "-T - SMS Text Message (example: -T \"Hello World! This is a text message\"),\n");
        fprintf(stderr, "-R - Raw Hex Octets   (example: -R 010203040506070809),\n");
        fprintf(stderr, "-n - number of bytes, only when pre-encoded data passed over stdin (1 to 823),\n");
        fprintf(stderr, "-o - output file path/name,\n");
        fprintf(stderr, "Output formats:\n");
        //fprintf(stderr, "-x - binary output (M17 baseband as a packed bitstream),\n");
        fprintf(stderr, "-r - raw audio output - default (single channel, signed 16-bit LE, +7168 for the +1.0 symbol, 10 samples per symbol),\n");
        fprintf(stderr, "-s - signed 16-bit LE symbols output\n");
        fprintf(stderr, "-f - float symbols output compatible with m17-packet-decode\n");
        fprintf(stderr, "-d - raw audio output - same as -r, but no RRC filtering (debug)\n");
        fprintf(stderr, "-w - libsndfile audio output - default (single channel, signed 16-bit LE, +7168 for the +1.0 symbol, 10 samples per symbol),\n");
        return -1;
    }

    //assert filename and not binary output
    if(strlen((const char*)fname)==0)
    {
        fprintf(stderr, "Filename not specified. Exiting...\n");
        return -1;
    }
    else if(out_type==OUT_TYPE_BIN)
    {
        fprintf(stderr, "Binary output file type not supported yet. Exiting...\n");
        return -1;
    }

    //obtain data and append with CRC
    memset(full_packet_data, 0, 33*25);

    //SMS Encode (-T) ./m17-packet-encode -f -o float.sym -T 'This is a simple SMS text message sent over M17 Packet Data.'
    if(sms_encode == 1)
    {
        num_bytes = strlen((const char*)text); //No need to check for zero return, since the default text string is supplied
        if(num_bytes > 821) num_bytes = 821; //add the 0x05 protocol byte, 0x00 terminator, and 2 byte CRC to get to 825
        full_packet_data[0] = 0x05; //SMS Protocol
        memcpy(full_packet_data+1, text, num_bytes);
        num_bytes+= 2; //add one for 0x05 protocol byte and 1 for terminating 0x00 to get to 823
        fprintf(stderr, "SMS: %s\n", full_packet_data+1);
    }

    //RAW Encode (-R) ./m17-packet-encode -f -o float.sym -R 5B69001E135152397C0A0000005A45
    else if(raw_encode == 1)
    {
        memcpy(full_packet_data, raw, num_bytes);
    }

    //Old Method pre-encoded data over stdin // echo -en "\x05Testing M17 packet mode.\x00" | ./m17-packet-encode -S N0CALL -D AB1CDE -C 7 -n 26 -f -o float.sym
    else if(std_encode == 1)
    {
        //assert number of bytes
        if(num_bytes==0)
        {
            fprintf(stderr, "Number of bytes not set. Exiting...\n");
            return -1;
        }

        if(fread(full_packet_data, num_bytes, 1, stdin)<1)
        {
            fprintf(stderr, "Packet data too short. Exiting...\n");
            return -1;
        }
        fprintf(stderr, "SMS: %s\n", full_packet_data+1);
        //
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
    fprintf(stderr, "CAN: %02d\n", can);
    fprintf(stderr, "Data CRC:\t%04hX\n", packet_crc);
    type=((uint16_t)0x01<<1)|((uint16_t)can<<7); //packet mode, content: data
    lsf.type[0]=(uint16_t)type>>8;
    lsf.type[1]=(uint16_t)type&0xFF;
    memset(&lsf.meta, 0, 112/8);

    //calculate LSF CRC
    uint16_t lsf_crc=LSF_CRC(&lsf);
    lsf.crc[0]=lsf_crc>>8;
    lsf.crc[1]=lsf_crc&0xFF;
    fprintf(stderr, "LSF  CRC:\t%04hX\n", lsf_crc);

    //encode LSF data
    conv_encode_LSF(enc_bits, &lsf);

    //fill preamble
    memset((uint8_t*)full_packet, 0, 36*192*10*sizeof(float));
    gen_preamble(full_packet, &pkt_sym_cnt, 0); //type: pre-LSF

    //send LSF syncword
    gen_syncword(full_packet, &pkt_sym_cnt, SYNC_LSF);

    //reorder bits
    reorder_bits(rf_bits, enc_bits);

    //randomize
    randomize_bits(rf_bits);

    //fill packet with LSF
    gen_data(full_packet, &pkt_sym_cnt, rf_bits);

    //read Packet Data from variable
    pkt_cnt=0;
    uint16_t tmp=num_bytes;
    while(num_bytes)
    {
        //send packet frame syncword
        gen_syncword(full_packet, &pkt_sym_cnt, SYNC_PKT);

        //the following examples produce exactly 25 bytes, which exactly one frame, but >= meant this would never produce a final frame with EOT bit set
        //echo -en "\x05Testing M17 packet mo\x00" | ./m17-packet-encode -S N0CALL -D ALL -C 10 -n 23 -o float.sym -f
        //./m17-packet-encode -S N0CALL -D ALL -C 10 -o float.sym -f -T 'this is a simple text'
        if(num_bytes>25) //fix for frames that, with terminating byte and crc, land exactly on 25 bytes (or %25==0)
        {
            memcpy(pkt_chunk, &full_packet_data[pkt_cnt*25], 25);
            pkt_chunk[25]=pkt_cnt<<2;
            fprintf(stderr, "FN:%02d (full frame)\n", pkt_cnt);

            //encode the packet frame
            conv_encode_packet_frame(enc_bits, pkt_chunk);

            //reorder bits
            reorder_bits(rf_bits, enc_bits);

            //randomize
            randomize_bits(rf_bits);

            //fill packet with frame data
            gen_data(full_packet, &pkt_sym_cnt, rf_bits);

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
            reorder_bits(rf_bits, enc_bits);

            //randomize
            randomize_bits(rf_bits);

            //fill packet with frame data
            gen_data(full_packet, &pkt_sym_cnt, rf_bits);

            num_bytes=0;
        }

        //debug dump
        //for(uint8_t i=0; i<26; i++)
            //fprintf(stderr, "%02X", pkt_chunk[i]);
        //fprintf(stderr, "\n");

        pkt_cnt++;
    }

    num_bytes=tmp; //bring back the num_bytes value
    fprintf (stderr, "PKT:");
    for(uint16_t i=0; i<pkt_cnt*25; i++)
    {
        if ( (i != 0) && ((i%25) == 0) )
            fprintf (stderr, "\n    ");

        fprintf (stderr, " %02X", full_packet_data[i]);
    }
    fprintf(stderr, "\n");

    //send EOT
    gen_eot(full_packet, &pkt_sym_cnt);

    if (out_type == OUT_TYPE_UPS_NO_FLT || out_type == OUT_TYPE_S16_RRC) //open wav file out
    {
        sprintf (wav_name, "%s", fname);
        info.samplerate = 48000;
        info.channels = 1;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
        wav = sf_open (wav_name, SFM_WRITE, &info); //write only, no append
        if (wav == NULL)
        {
            fprintf (stderr,"Error - could not open raw wav output file %s\n", wav_name);
            return -1;
        }
    }
    else //dump baseband to a file
        fp=fopen((const char*)fname, "wb");
    
    //debug mode - symbols multiplied by 7168 scaling factor
    /*for(uint16_t i=0; i<pkt_sym_cnt; i++)
    {
        int16_t val=roundf(full_packet[i]*RRC_DEV);
        fwrite(&val, 2, 1, fp);
    }*/

    //standard mode - filtered baseband
    if(out_type==OUT_TYPE_S16_RAW)
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
    else if(out_type==OUT_TYPE_S16_SYMB)
    {
        for(uint16_t i=0; i<pkt_sym_cnt; i++)
        {
            int16_t val=full_packet[i];
            fwrite(&val, 2, 1, fp);
        }
    }

    //float symbol stream compatible with m17-packet-decode
    else if(out_type==OUT_TYPE_FLOAT)
    {
        for(uint16_t i=0; i<pkt_sym_cnt; i++)
        {
            float val=full_packet[i];
            fwrite(&val, 4, 1, fp);
        }
    }
  
    //simple 10x upsample * 7168.0f
    else if (out_type == OUT_TYPE_UPS_NO_FLT)
    {   

        //array of upsample full_packet
        float up[1920*35]; memset (up, 0, 1920*35*sizeof(float));

        //10x upsample from full_packet to up
        for (int i = 0; i < 192*len; i++)
        {
            for (int j = 0; j < 10; j++)
                up[(i*10)+j] = full_packet[i];
        }

        //array of shorts for sndfile wav output
        short bb[1920*35]; memset (bb, 0, 1920*35*sizeof(short));

        //write dead air to sndfile wav
        sf_write_short(wav, bb, 1920);

        //load bb with upsample, use len to see how many we need to send
        for (int i = 0; i < 1920*len; i++)
            bb[i] = (short)(up[i] * 7168.0f);

        //write to sndfile wav
        sf_write_short(wav, bb, 1920*len);
    }

    //standard mode - filtered baseband (converted to wav)
    else if(out_type == OUT_TYPE_S16_RRC)
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
                short tmp[2]; tmp[0]=mac;
                sf_write_short(wav, tmp, 1);
            }
        }
    }

    //close file, depending on type opened
    if(out_type == OUT_TYPE_UPS_NO_FLT || out_type == OUT_TYPE_S16_RRC)
    {
        sf_write_sync(wav);
        sf_close(wav);
    }
    else
    {
        fclose(fp);
    }
    
	return 0;
}
