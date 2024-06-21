#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

//libm17
#include "../../libm17/m17.h"
//micro-ecc
#include "../../micro-ecc/uECC.h"

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

void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "-c - Display decoded callsigns,\n");
    fprintf(stderr, "-v - Display Viterbi error metrics,\n");
    fprintf(stderr, "-m - Display META fields,\n");
    fprintf(stderr, "-l - Display LSF CRC checks,\n");
    fprintf(stderr, "-d - Set syncword detection threshold (decimal value),\n");
    fprintf(stderr, "-s - Public key for ECDSA signature, 64 bytes (-s [hex_string|key_file]),\n");
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
                uint16_t len=strlen(argv[i+1]);

                if(len!=64*2) //for secp256r1
                {
                    fprintf(stderr, "Invalid private key length. Exiting...\n");
                    return -1;
                }

                parse_raw_key_string(pub_key, argv[i+1]);

                i++;
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

                    //if the stream is signed
                    if(signed_str && fn<0x7FFC)
                    {
                        for(uint8_t i=0; i<sizeof(digest); i++)
                            digest[i]^=frame_data[3+i];
                        uint8_t tmp=digest[0];
                        for(uint8_t i=0; i<sizeof(digest)-1; i++)
                            digest[i]=digest[i+1];
                        digest[sizeof(digest)-1]=tmp;
                    }

                    //extract LICH
                    for(uint16_t i=0; i<96; i++)
                    {
                        lich_chunk[i]=d_soft_bit[i];
                    }

                    //Golay decoder
                    decode_LICH(lich_b, lich_chunk);
                    lich_cnt=lich_b[5]>>5;

                    //If we're at the start of a superframe, or we missed a frame, reset the LICH state
                    if((lich_cnt==0) || ((fn % 0x8000)!=expected_next_fn))
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
                            if(((type>>5)&3)==1)
                                printf("8-bit, ");
                            else if(((type>>5)&3)==2)
                                printf("16-bit, ");
                            else if(((type>>5)&3)==3)
                                printf("24-bit, ");
                        }
                        else if(((type>>3)&3)==2)
                            printf("AES, ");
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
                        if(((type>>5)&3)==1)
                            printf("8-bit, ");
                        else if(((type>>5)&3)==2)
                            printf("16-bit, ");
                        else if(((type>>5)&3)==3)
                            printf("24-bit, ");
                    }
                    else if(((type>>3)&3)==2)
                        printf("AES, ");
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
