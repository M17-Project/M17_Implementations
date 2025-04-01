#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

//libm17
#include "../../libm17/m17.h"
//micro-ecc
#include "../../micro-ecc/uECC.h"
//tinier-aes
#include "../../tinier-aes/aes.h"

//TODO: Set Encryption type and subtype from decoded frame type field, not hard set based on entered values when a key is passed

//settings
uint8_t decode_callsigns=0;
uint8_t show_viterbi_errs=0;
uint8_t show_meta=0;
uint8_t show_lsf_crc=0;
float dist_thresh=2.0;				//distance threshold for the L2 metric (for syncword detection), default: 2.0

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

//used for signatures
uint8_t digest[16]={0};             //16-byte field for the stream digest
uint8_t signed_str=0;               //is the stream signed?
uint8_t pub_key[64]={0};            //public key
uint8_t sig[64]={0};                //ECDSA signature

//encryption
typedef enum
{
    ENCR_NONE,
    ENCR_SCRAM,
    ENCR_AES,
    ENCR_RES //reserved
} encr_t;
encr_t encryption=ENCR_NONE;

//AES
typedef enum
{
    AES128,
    AES192,
    AES256
} aes_t;
uint8_t key[32];
uint8_t iv[16];
time_t epoch = 1577836800L; //Jan 1, 2020, 00:00:00 UTC

//Scrambler
uint8_t scr_bytes[16];
uint8_t scrambler_pn[128];
uint32_t scrambler_key=0; //keep set to initial value for seed calculation function
uint32_t scrambler_seed=0;
int8_t scrambler_subtype = -1;
int8_t aes_subtype = -1;

//debug mode
uint8_t debug_mode=0; //TODO: Remove lines looking at this

//this is generating a correct seed value based on the fn value,
//ideally, we would only want to run this under poor signal, frame skips, etc 
//Note: Running this every frame will lag if high fn values (observed with test file)
uint32_t scrambler_seed_calculation(int8_t subtype, uint32_t key, int fn)
{
  int i;
  uint32_t lfsr, bit;

  lfsr = key; bit = 0;
  for (i = 0; i < 128*fn; i++)
  {
    //get feedback bit with specified taps, depending on the subtype
    if (subtype == 0)
      bit = (lfsr >> 7) ^ (lfsr >> 5) ^ (lfsr >> 4) ^ (lfsr >> 3);
    else if (subtype == 1)
      bit = (lfsr >> 15) ^ (lfsr >> 14) ^ (lfsr >> 12) ^ (lfsr >> 3);
    else if (subtype == 2)
      bit = (lfsr >> 23) ^ (lfsr >> 22) ^ (lfsr >> 21) ^ (lfsr >> 16);
    else bit = 0; //should never get here, but just in case
    
    bit &= 1; //truncate bit to 1 bit
    lfsr = (lfsr << 1) | bit; //shift LFSR left once and OR bit onto LFSR's LSB
    lfsr &= 0xFFFFFF; //truncate lfsr to 24-bit

  }

  //truncate seed so subtype will continue to set properly on subsequent passes
  if      (scrambler_subtype == 0) scrambler_seed &= 0xFF;
  else if (scrambler_subtype == 1) scrambler_seed &= 0xFFFF;
  else if (scrambler_subtype == 2) scrambler_seed &= 0xFFFFFF;

  //debug
  //fprintf (stderr, "\nScrambler Key: 0x%06X; Seed: 0x%06X; Subtype: %02d; FN: %05d; ", key, lfsr, subtype, fn);

  return lfsr;
}

//set scrambler subtype based on len of key at time of init
int8_t scrambler_subtype_set(uint32_t scrambler_seed)
{
    if      (scrambler_seed > 0 && scrambler_seed <= 0xFF)          return 0; // 8-bit key
    else if (scrambler_seed > 0xFF && scrambler_seed <= 0xFFFF)     return 1; //16-bit key
    else if (scrambler_seed > 0xFFFF && scrambler_seed <= 0xFFFFFF) return 2; //24-bit key
    else                                                            return 0; // 8-bit key (default)
}

//scrambler pn sequence generation
void scrambler_sequence_generator()
{
  int i = 0;
  uint32_t lfsr, bit;
  lfsr = scrambler_seed;

  //TODO: Set Frame Type based on scrambler_subtype value
  if (debug_mode>1)
  {
    fprintf(stderr, "\nScrambler Key: 0x%06X; Seed: 0x%06X; Subtype: %02d;", scrambler_seed, lfsr, scrambler_subtype);
    fprintf(stderr, "\n pN: ");
  }
  
  //run pN sequence with taps specified
  for (i = 0; i < 128; i++)
  {
    //get feedback bit with specified taps, depending on the scrambler_subtype
    if (scrambler_subtype == 0)
      bit = (lfsr >> 7) ^ (lfsr >> 5) ^ (lfsr >> 4) ^ (lfsr >> 3);
    else if (scrambler_subtype == 1)
      bit = (lfsr >> 15) ^ (lfsr >> 14) ^ (lfsr >> 12) ^ (lfsr >> 3);
    else if (scrambler_subtype == 2)
      bit = (lfsr >> 23) ^ (lfsr >> 22) ^ (lfsr >> 21) ^ (lfsr >> 16);
    else bit = 0; //should never get here, but just in case
    
    bit &= 1; //truncate bit to 1 bit (required since I didn't do it above)
    lfsr = (lfsr << 1) | bit; //shift LFSR left once and OR bit onto LFSR's LSB
    lfsr &= 0xFFFFFF; //truncate lfsr to 24-bit (really doesn't matter)
    scrambler_pn[i] = bit;

  }

  //pack bit array into byte array for easy data XOR
  pack_bit_array_into_byte_array(scrambler_pn, scr_bytes, 16);

  //save scrambler seed for next round
  scrambler_seed = lfsr;

  //truncate seed so subtype will continue to set properly on subsequent passes
  if      (scrambler_subtype == 0) scrambler_seed &= 0xFF;
  else if (scrambler_subtype == 1) scrambler_seed &= 0xFFFF;
  else if (scrambler_subtype == 2) scrambler_seed &= 0xFFFFFF;

  if (debug_mode > 1)
  {
    //debug packed bytes
    for (i = 0; i < 16; i++)
        fprintf (stderr, " %02X", scr_bytes[i]);
    fprintf (stderr, "\n");
  }
  
}

void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "-c - Display decoded callsigns,\n");
    fprintf(stderr, "-v - Display Viterbi error metrics,\n");
    fprintf(stderr, "-m - Display META fields,\n");
    fprintf(stderr, "-l - Display LSF CRC checks,\n");
    fprintf(stderr, "-d - Set syncword detection threshold (decimal value),\n");
    fprintf(stderr, "-s - Public key for ECDSA signature, 64 bytes (-s [hex_string|key_file]),\n");
    fprintf(stderr, "-K - AES encryption key (-K [hex_string|text_file]),\n");
    fprintf(stderr, "-k - Scrambler encryption seed value (-k [hex_string]),\n");
    fprintf(stderr, "-h - help / print usage\n");
}

//convert a user string (as hex octets) into a uint8_t array for key
void parse_raw_key_string(uint8_t* dest, const char* inp)
{
    uint16_t len = strlen(inp);

    if(len==0) return; //return silently and pretend nothing happened

    memset(dest, 0, len/2); //one character represents half of a byte

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
}

int main(int argc, char* argv[])
{
    if(argc>1) //arg scanning: not foolproof at all
    {
        for(uint8_t i=1; i<argc; i++)
        {
            if(!strcmp(argv[i], "-c"))
            {
                decode_callsigns=1;
                printf("Decode callsigns: ON\n");
            }

            if(!strcmp(argv[i], "-v"))
            {
                show_viterbi_errs=1;
                printf("Show Viterbi errors: ON\n");
            }

            if(!strcmp(argv[i], "-m"))
            {
                show_meta=1;
                printf("Show META field: ON\n");
            }

            if(!strcmp(argv[i], "-d"))
            {
                dist_thresh=atof(argv[i+1]);
                if(dist_thresh>=0)
                    printf("Syncword detection threshold: %1.2f\n", dist_thresh);
                else
                {
                    printf("Invalid syncword detection threshold, setting to default (2.0)\n");
                    dist_thresh=2.0f;
                }
                i++;
            }

            if(!strcmp(argv[i], "-s"))
            {
                if(strstr(argv[i+1], ".")) //if the next arg contains a dot - read key from a text file
                {
                    if(strlen(argv[i+1])<3)
                    {
                        fprintf(stderr, "Invalid filename. Exiting...\n");
                        return -1;
                    }

                    FILE* fp;
                    char source_str[64*2];

                    fp = fopen(argv[i+1], "r");
                    if(!fp)
                    {
                        fprintf(stderr, "Failed to load file %s.\n", argv[i+1]);
                        return -1;
                    }

                    //size check
                    size_t len = fread(source_str, 1, 64*2, fp);
                    fclose(fp);

                    if(len!=64*2) //for secp256r1
                    {
                        fprintf(stderr, "Invalid public key length. Exiting...\n");
                        return -1;
                    }

                    parse_raw_key_string(pub_key, source_str);
                }
                else
                {
                    uint16_t len=strlen(argv[i+1]);

                    if(len!=64*2) //for secp256r1
                    {
                        fprintf(stderr, "Invalid public key length. Exiting...\n");
                        return -1;
                    }

                    parse_raw_key_string(pub_key, argv[i+1]);
                }

                i++;
            }
            
            if(argv[i][1]=='K') //-K - AES Encryption
            {
                if(strstr(argv[i+1], ".")) //if the next arg contains a dot - read key from a text file
                {
                    if(strlen(argv[i+1])<3)
                    {
                        fprintf(stderr, "Invalid filename. Exiting...\n");
                        return -1;
                    }

                    FILE* fp;
                    char source_str[64];

                    fp = fopen(argv[i+1], "r");
                    if(!fp)
                    {
                        fprintf(stderr, "Failed to load file %s.\n", argv[i+1]);
                        return -1;
                    }

                    //size check
                    size_t len = fread(source_str, 1, 64, fp);
                    fclose(fp);

                    if(len==256/4)
                    {
                        fprintf(stderr, "AES256");
                        aes_subtype = 2;
                    }
                    else if(len==192/4)
                    {
                        fprintf(stderr, "AES192");
                        aes_subtype = 1;
                    }  
                    else if(len==128/4)
                    {
                        fprintf(stderr, "AES128");
                        aes_subtype = 0;
                    } 
                    else
                    {
                        fprintf(stderr, "Invalid key length.\n");
                        return -1;
                    }

                    parse_raw_key_string(key, source_str);

                    fprintf(stderr, " key:");
                    for(uint8_t i=0; i<len/2; i++)
                    {
                        if(i==16)
                            fprintf(stderr, "\n           ");
                        fprintf(stderr, " %02X", key[i]);
                    }
                    fprintf(stderr, "\n");
                }
                else
                {
                    //size check
                    size_t len = strlen(argv[i+1]);

                    if(len==256/4)
                    {
                        fprintf(stderr, "AES256");
                        aes_subtype = 2;
                    }
                    else if(len==192/4)
                    {
                        fprintf(stderr, "AES192");
                        aes_subtype = 1;
                    } 
                    else if(len==128/4)
                    {
                        fprintf(stderr, "AES128");
                        aes_subtype = 0;
                    }
                    else
                    {
                        fprintf(stderr, "Invalid key length.\n");
                        return -1;
                    }

                    parse_raw_key_string(key, argv[i+1]);

                    fprintf(stderr, " key:");
                    for(uint8_t i=0; i<len/2; i++)
                    {
                        if(i==16)
                            fprintf(stderr, "\n           ");
                        fprintf(stderr, " %02X", key[i]);
                    }
                    fprintf(stderr, "\n");
                }

                encryption=ENCR_AES; //AES key was passed
                i++;
            }
            if(argv[i][1]=='k') //-k - Scrambler Encryption
            {
                //length check
                uint8_t length=strlen(argv[i+1]);
                if(length==0 || length>24/4) //24-bit is the largest seed value
                {
                    fprintf(stderr, "Invalid key length.\n");
                    return -1;
                }

                parse_raw_key_string(key, argv[i+1]);
                scrambler_key = (key[0] << 16) | (key[1] << 8) | (key[2] << 0);

                if(length<=2)
                {
                    scrambler_key = scrambler_key >> 16;
                    fprintf(stderr, "Scrambler key: 0x%02X (8-bit)\n", scrambler_key);
                }
                else if(length<=4)
                {
                    scrambler_key = scrambler_key >> 8;
                    fprintf(stderr, "Scrambler key: 0x%04X (16-bit)\n", scrambler_key);
                }
                else
                    fprintf(stderr, "Scrambler key: 0x%06X (24-bit)\n", scrambler_key);

                encryption=ENCR_SCRAM; //Scrambler key was passed
                scrambler_seed = scrambler_key; //set initial seed value to key value
                scrambler_subtype = scrambler_subtype_set(scrambler_seed);
            }

            if(!strcmp(argv[i], "-l"))
            {
                show_lsf_crc=1;
                printf("Show LSF CRC: ON\n");
            }

            if(!strcmp(argv[i], "-h"))
            {
                usage();
                return 0;
            }            
        }
    }

    printf("Awaiting samples...\n");
    const struct uECC_Curve_t* curve = uECC_secp256r1();

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
            dist = eucl_norm(last, str_sync_symbols, 8);

            if(dist<dist_thresh) //frame syncword detected
            {
                //fprintf(stderr, "str_sync dist: %3.5f\n", dist);
                syncd=1;
                pushed=0;
                fl=0;
            }
            else
            {
                //calculate euclidean norm again, this time against LSF syncword
                dist = eucl_norm(last, lsf_sync_symbols, 8);

                if(dist<dist_thresh) //LSF syncword
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
                //slice symbols to soft dibits
                slice_symbols(soft_bit, pld);

                //derandomize
                randomize_soft_bits(soft_bit);

                //deinterleave
                reorder_soft_bits(d_soft_bit, soft_bit);

                //if it is a frame
                if(!fl)
                {
                    //extract data
                    for(uint16_t i=0; i<272; i++)
                    {
                        enc_data[i]=d_soft_bit[96+i];
                    }

                    //decode
                    uint32_t e=viterbi_decode_punctured(frame_data, enc_data, puncture_pattern_2, 272, 12);

                    uint16_t fn = (frame_data[1] << 8) | frame_data[2];
                    uint16_t type=(uint16_t)lsf[12]*0x100+lsf[13]; //big-endian
                    signed_str=(type>>11)&1;

                    ///if the stream is signed (process before decryption)
                    if(signed_str && fn<0x7FFC)
                    {
                        if(fn==0)
                            memset(digest, 0, sizeof(digest));

                        for(uint8_t i=0; i<sizeof(digest); i++)
                            digest[i]^=frame_data[3+i];
                        uint8_t tmp=digest[0];
                        for(uint8_t i=0; i<sizeof(digest)-1; i++)
                            digest[i]=digest[i+1];
                        digest[sizeof(digest)-1]=tmp;
                    }

                    //NOTE: Don't attempt decryption when a signed stream is >= 0x7FFC
                    //The Signature is not encrypted
                    
                    //AES
                    if(encryption==ENCR_AES)
                    {
                        memcpy(iv, lsf+14, 14);
                        iv[14] = frame_data[1] & 0x7F;
                        iv[15] = frame_data[2] & 0xFF;

                        if(signed_str && (fn % 0x8000)<0x7FFC) //signed stream
                            aes_ctr_bytewise_payload_crypt(iv, key, frame_data+3, aes_subtype);
                        else if(!signed_str)                    //non-signed stream
                            aes_ctr_bytewise_payload_crypt(iv, key, frame_data+3, aes_subtype);
                    }

                    //Scrambler
                    if(encryption==ENCR_SCRAM)
                    {
                        if(fn != 0 && (fn % 0x8000)!=expected_next_fn) //frame skip, etc
                            scrambler_seed = scrambler_seed_calculation(scrambler_subtype, scrambler_key, fn&0x7FFF);
                        else if(fn == 0) scrambler_seed = scrambler_key; //reset back to key value

                        if(signed_str && (fn % 0x8000)<0x7FFC) //signed stream
                            scrambler_sequence_generator();
                        else if(!signed_str)                    //non-signed stream
                            scrambler_sequence_generator();
                        else memset(scr_bytes, 0, sizeof(scr_bytes)); //zero out stale scrambler bytes so they aren't applied to the sig frames
                        
                        for(uint8_t i=0; i<16; i++)
                        {
                            frame_data[i+3] ^= scr_bytes[i];
                        }
                    }

                    //dump data - first byte is empty
                    printf("FN: %04X PLD: ", fn);
                    for(uint8_t i=3; i<19; i++)
                    {
                        printf("%02X", frame_data[i]);
                    }
                    if(show_viterbi_errs)
                        printf(" e=%1.1f\n", (float)e/0xFFFF);
                    
                    printf("\n");

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
                    if((lich_cnt==0) || ((fn % 0x8000)!=expected_next_fn && fn<0x7FFC))
                        lich_chunks_rcvd=0;

                    lich_chunks_rcvd|=(1<<lich_cnt);
                    memcpy(&lsf[lich_cnt*5], lich_b, 5);

                    //debug - dump LICH
                    if(lich_chunks_rcvd==0x3F) //all 6 chunks received?
                    {
                        if(decode_callsigns)
                        {
                            uint8_t d_dst[12], d_src[12]; //decoded strings

                            decode_callsign_bytes(d_dst, &lsf[0]);
                            decode_callsign_bytes(d_src, &lsf[6]);

                            //DST
                            printf("DST: %-9s ", d_dst);

                            //SRC
                            printf("SRC: %-9s ", d_src);
                        }
                        else
                        {
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
                        }

                        //TYPE
                        printf("TYPE: %04X (", type);
                        if(type&&1)
                            printf("STREAM: ");
                        else
                            printf("PACKET: "); //shouldn't happen
                        if(((type>>1)&3)==1)
                            printf("DATA, ");
                        else if(((type>>1)&3)==2)
                            printf("VOICE, ");
                        else if(((type>>1)&3)==3)
                            printf("VOICE+DATA, "); 
                        printf("ENCR: ");
                        if(((type>>3)&3)==0)
                            printf("PLAIN, ");
                        else if(((type>>3)&3)==1)
                        {
                            printf("SCRAM ");
                            if(((type>>5)&3)==0)
                                printf("8-bit, ");
                            else if(((type>>5)&3)==1)
                                printf("16-bit, ");
                            else if(((type>>5)&3)==2)
                                printf("24-bit, ");
                        }
                        else if(((type>>3)&3)==2)
                        {
                            printf("AES");
                            if(((type>>5)&3)==0)
                                printf("128");
                            else if(((type>>5)&3)==1)
                                printf("192");
                            else if(((type>>5)&3)==2)
                                printf("256");

                            printf(", ");
                        }
                        else
                            printf("UNK, ");
                        printf("CAN: %d", (type>>7)&0xF);
                        if(signed_str)
                        {
                            printf(", SIGNED");
                        }
                        printf(") ");

                        //META
                        if(show_meta)
                        {
                            printf("META: ");
                            for(uint8_t i=0; i<14; i++)
                                printf("%02X", lsf[14+i]);
                            printf(" ");
                        }

                        //CRC
                        if(show_lsf_crc)
                        {
                            //printf("CRC: ");
                            //for(uint8_t i=0; i<2; i++)
                                //printf("%02X", lsf[28+i]);
                            if(CRC_M17(lsf, 30))
                                printf("LSF_CRC_ERR");
                            else
                                printf("LSF_CRC_OK");
                        }
                        printf("\n");
                    }

                    //if the contents of the payload is now digital signature, not data/voice
                    if(fn>=0x7FFC && signed_str)
                    {
                        memcpy(&sig[((fn&0x7FFF)-0x7FFC)*16], &frame_data[3], 16);
                        
                        if(fn==(0x7FFF|0x8000))
                        {
                            //dump data
                            /*printf("DEC-Digest: ");
                            for(uint8_t i=0; i<sizeof(digest); i++)
                                printf("%02X", digest[i]);
                            printf("\n");

                            printf("Key: ");
                            for(uint8_t i=0; i<sizeof(pub_key); i++)
                                printf("%02X", pub_key[i]);
                            printf("\n");

                            printf("Signature: ");
                            for(uint8_t i=0; i<sizeof(sig); i++)
                                printf("%02X", sig[i]);
                            printf("\n");*/

                            if(uECC_verify(pub_key, digest, sizeof(digest), sig, curve))
                            {
                                printf("Signature OK\n");
                            }
                            else
                            {
                                printf("Signature invalid\n");
                            }
                        }
                    }

                    expected_next_fn = (fn + 1) % 0x8000;
                }
                else //lsf
                {
                    printf("{LSF} ");

                    //decode
                    uint32_t e=viterbi_decode_punctured(lsf, d_soft_bit, puncture_pattern_1, 2*SYM_PER_PLD, 61);

                    //shift the buffer 1 position left - get rid of the encoded flushing bits
                    for(uint8_t i=0; i<30; i++)
                        lsf[i]=lsf[i+1];

                    //dump data
                    if(decode_callsigns)
                    {
                        uint8_t d_dst[12], d_src[12]; //decoded strings

                        decode_callsign_bytes(d_dst, &lsf[0]);
                        decode_callsign_bytes(d_src, &lsf[6]);

                        //DST
                        printf("DST: %-9s ", d_dst);

                        //SRC
                        printf("SRC: %-9s ", d_src);
                    }
                    else
                    {
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
                    }

                    //TYPE
                    uint16_t type=(uint16_t)lsf[12]*0x100+lsf[13]; //big-endian
                    printf("TYPE: %04X (", type);
                    if(type&&1)
                        printf("STREAM: ");
                    else
                        printf("PACKET: "); //shouldn't happen
                    if(((type>>1)&3)==1)
                        printf("DATA, ");
                    else if(((type>>1)&3)==2)
                        printf("VOICE, ");
                    else if(((type>>1)&3)==3)
                        printf("VOICE+DATA, "); 
                    printf("ENCR: ");
                    if(((type>>3)&3)==0)
                        printf("PLAIN, ");
                    else if(((type>>3)&3)==1)
                    {
                        printf("SCRAM ");
                        if(((type>>5)&3)==0)
                            printf("8-bit, ");
                        else if(((type>>5)&3)==1)
                            printf("16-bit, ");
                        else if(((type>>5)&3)==2)
                            printf("24-bit, ");
                    }
                    else if(((type>>3)&3)==2)
                    {
                        printf("AES");
                        if(((type>>5)&3)==0)
                            printf("128");
                        else if(((type>>5)&3)==1)
                            printf("192");
                        else if(((type>>5)&3)==2)
                            printf("256");

                        printf(", ");
                    }
                    else
                        printf("UNK, ");
                    printf("CAN: %d", (type>>7)&0xF);
                    if((type>>11)&1)
                    {
                        printf(", SIGNED");
                        signed_str=1;
                    }
                    else
                        signed_str=0;
                    printf(") ");

                    //META
                    if(show_meta)
                    {
                        printf("META: ");
                        for(uint8_t i=0; i<14; i++)
                            printf("%02X", lsf[14+i]);
                        printf(" ");
                    }

                    //CRC
                    if(show_lsf_crc)
                    {
                        //printf("CRC: ");
                        //for(uint8_t i=0; i<2; i++)
                            //printf("%02X", lsf[28+i]);
                        if(CRC_M17(lsf, 30))
                            printf("LSF_CRC_ERR");
                        else
                            printf("LSF_CRC_OK");
                        printf(" ");
                    }

                    //Viterbi decoder errors
                    if(show_viterbi_errs)
                        printf("e=%1.1f\n", (float)e/0xFFFF);

                    printf("\n");
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
