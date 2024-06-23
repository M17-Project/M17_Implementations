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

//#define FN60_DEBUG

//TODO: Load Signature Private and Public Keys from file
//TODO: More Thorough Testing to make sure everything is good
//TODO: Round of Cleanup (and test after cleaning up!
//TODO: OR Frametype Bits depending on encryption type, subtype, and signed sig

//Wishlist: Please Woj, can we use the subtype on AES to signal AES 128, AES 192, or AES 256?
//          We already to it for Scrambler

//Wishlist: way to fix this warning without changing uECC source code or disabling -Wall -Wextra
//../../micro-ecc/curve-specific.inc:544:59: warning: unused parameter ‘curve’ [-Wunused-parameter]
//544 | static void mod_sqrt_secp224r1(uECC_word_t *a, uECC_Curve curve) {

struct LSF lsf, next_lsf;

uint8_t lich[6];                    //48 bits packed raw, unencoded LICH
uint8_t lich_encoded[12];           //96 bits packed, encoded LICH
uint8_t enc_bits[SYM_PER_PLD*2];    //type-2 bits, unpacked
uint8_t rf_bits[SYM_PER_PLD*2];     //type-4 bits, unpacked

float frame_buff[192];
uint32_t frame_buff_cnt;

uint8_t data[16], next_data[16];    //raw payload, packed bits
uint16_t fn=0;                      //16-bit Frame Number (for the stream mode)
uint8_t lich_cnt=0;                 //0..5 LICH counter
uint8_t got_lsf=0;                  //have we transmitted the LSF yet?
uint8_t finished=0;                 //no more data at stdin?

//used for signatures
uint8_t digest[16]={0};             //16-byte field for the stream digest
uint8_t signed_str=0;               //is the stream supposed to be signed?
uint8_t priv_key_loaded=0;          //do we have a sig key loaded?
uint8_t priv_key[32]={0};           //private key
uint8_t sig[64]={0};                //ECDSA signature

int dummy=0;                        //dummy var to make compiler quieter

//AES
uint8_t encryption=0;
int aes_type = 1; //1=AES128, 2=AES192, 3=AES256
uint8_t key[32];
uint8_t iv[16];
time_t epoch = 1577836800L; //Jan 1, 2020, 00:00:00 UTC

//Scrambler
uint8_t scr_bytes[16];
uint8_t scrambler_pn[128];
uint32_t scrambler_seed=0;
int8_t scrambler_subtype = -1;

//debug mode (preset lsf, type, zero payload for enc testing, etc)
uint8_t debug_mode=0;

//scrambler pn sequence generation
void scrambler_sequence_generator()
{
  int i = 0;
  uint32_t lfsr, bit;
  lfsr = scrambler_seed;

  //only set if not initially set (first run), it is possible (and observed) that the scrambler_subtype can 
  //change on subsequent passes if the current SEED for the LFSR falls below one of these thresholds
  if (scrambler_subtype == -1)
  {
    if      (lfsr > 0 && lfsr <= 0xFF)          scrambler_subtype = 0; // 8-bit key
    else if (lfsr > 0xFF && lfsr <= 0xFFFF)     scrambler_subtype = 1; //16-bit key
    else if (lfsr > 0xFFFF && lfsr <= 0xFFFFFF) scrambler_subtype = 2; //24-bit key
    else                                        scrambler_subtype = 0; // 8-bit key (default)
  }

  //TODO: Set Frame Type based on scrambler_subtype value
  if (debug_mode > 1)
  {
    fprintf (stderr, "\nScrambler Key: 0x%06X; Seed: 0x%06X; Subtype: %02d;", scrambler_seed, lfsr, scrambler_subtype);
    fprintf (stderr, "\n pN: ");
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
  if (scrambler_subtype == 0) scrambler_seed &= 0xFF;
  if (scrambler_subtype == 1) scrambler_seed &= 0xFFFF;
  if (scrambler_subtype == 2) scrambler_seed &= 0xFFFFFF;
  else                        scrambler_seed &= 0xFF;

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
    fprintf(stderr, "-s - Private key for ECDSA signature, 32 bytes (-s [hex_string|key_file]),\n");
    fprintf(stderr, "-K - AES encryption key (-K [hex_string|text_file]),\n");
    fprintf(stderr, "-k - Scrambler encryption seed value (-k [hex_string]),\n");
    fprintf(stderr, "-D - Debug mode,\n");
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

//main routine
int main(int argc, char* argv[])
{
    srand(time(NULL)); //random number generator (for IV rand() seed value)
    memset(key, 0, 32*sizeof(uint8_t));
    memset(iv, 0, 16*sizeof(uint8_t));

    //scan command line options for input data (purely optional)
    if(argc>=1)
    {
        for(uint8_t i=1; i<argc; i++)
        {
            if(argv[i][0]=='-')
            {
                if(argv[i][1]=='s') //-s - private key for digital signature
                {
                    uint16_t len=strlen(argv[i+1]);

                    if(len!=32*2) //for secp256r1
                    {
                        fprintf(stderr, "Invalid private key length. Exiting...\n");
                        return -1;
                    }

                    parse_raw_key_string(priv_key, argv[i+1]);
                    priv_key_loaded=1; //mainly for debug mode
                    i++;
                }
                else if(argv[i][1]=='K') //-K - AES Encryption
                {
                    if(strstr(argv[i+1], ".")) //if the next arg contains a dot - read key from a text file
                    {
                        char fname[128]={'\0'}; //output file
                        if(strlen(&argv[i+1][0])>0)
                            memcpy(fname, &argv[i+1][0], strlen(argv[i+1]));
                        else
                        {
                            fprintf(stderr, "Invalid filename. Exiting...\n");
                            return -1;
                        }

                        FILE* fp;
                        char source_str[64];

                        fp = fopen(fname, "r");
                        if(!fp)
                        {
                            fprintf(stderr, "Failed to load file %s.\n", fname);
                            return -1;
                        }

                        //size check
                        size_t len = fread(source_str, 1, 64, fp); //TODO: check length
                        fclose(fp);

                        if(len==256/4)
                            fprintf(stderr, "AES256");
                        else if(len==192/4)
                            fprintf(stderr, "AES192");
                        else if(len==128/4)
                            fprintf(stderr, "AES128");
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
                            fprintf(stderr, "AES256");
                        else if(len==192/4)
                            fprintf(stderr, "AES192");
                        else if(len==128/4)
                            fprintf(stderr, "AES128");
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

                    encryption=2; //AES key was passed
                }
                else if(argv[i][1]=='k') //-k - Scrambler Encryption
                {
                    //length check
                    uint8_t length=strlen(argv[i+1]);
                    if(length==0 || length>24/4) //24-bit is the largest seed value
                    {
                        fprintf(stderr, "Invalid key length.\n");
                        return -1;
                    }

                    parse_raw_key_string(key, argv[i+1]);
                    scrambler_seed = (key[0] << 16) | (key[1] << 8) | (key[2] << 0);

                    if(length<=2)
                    {
                        scrambler_seed = scrambler_seed >> 16;
                        fprintf(stderr, "Scrambler key: 0x%02X (8-bit)\n", scrambler_seed);
                    }
                    else if(length<=4)
                    {
                        scrambler_seed = scrambler_seed >> 8;
                        fprintf(stderr, "Scrambler key: 0x%04X (16-bit)\n", scrambler_seed);
                    }
                    else
                        fprintf(stderr, "Scrambler key: 0x%06X (24-bit)\n", scrambler_seed);

                    encryption=1; //Scrambler key was passed
                }
                else if(argv[i][1]=='D') //-D - Debug Mode
                {
                    debug_mode=1;
                }

                else if(argv[i][1]=='h') //-h - help / usage
                {
                    usage();
                    return -1;
                }
                else
                {
                    fprintf(stderr, "Unknown param detected. Exiting...\n");
                    usage();
                    return -1;
                }
            }
        }
    }

    if(encryption==2)
    {
        for(uint8_t i=0; i<4; i++)
            iv[i] = ((uint32_t)(time(NULL)&0xFFFFFFFF)-(uint32_t)epoch) >> (24-(i*8));
        for(uint8_t i=3; i<14; i++)
            iv[i] = rand() & 0xFF; //10 random bytes
    }

    const struct uECC_Curve_t* curve = uECC_secp256r1();

    //send out the preamble
    frame_buff_cnt=0;
	send_preamble(frame_buff, &frame_buff_cnt, 0); //0 - LSF preamble, as opposed to 1 - BERT preamble
    fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);

    if (debug_mode == 1)
    {
        //broadcast
        memset(lsf.dst, 0xFF, 6*sizeof(uint8_t));

        //N0CALL
        lsf.src[0] = 0x00;
        lsf.src[1] = 0x00;
        lsf.src[2] = 0x4B;
        lsf.src[3] = 0x13;
        lsf.src[4] = 0xD1;
        lsf.src[5] = 0x06;

        if (encryption == 2) //AES ENC, 3200 voice
        {
            lsf.type[0] = 0x03;
            lsf.type[1] = 0x95;
        }
        else if (encryption == 1) //Scrambler ENC, 3200 Voice
        {
            lsf.type[0] = 0x03;
            lsf.type[1] = 0xCD;
        }
        else //no enc or subtype field, normal 3200 voice
        {
            lsf.type[0] = 0x00;
            lsf.type[1] = 0x05;
        }

        //a signature key is loaded, OR this bit
        if(priv_key_loaded)
        {
            signed_str = 1;
            lsf.type[0] |= 0x8;
        }
            
        //calculate LSF CRC (unclear whether or not this is only 
        //needed here for debug, or if this is missing on every initial LSF)
        uint16_t ccrc=LSF_CRC(&lsf);
        lsf.crc[0]=ccrc>>8;
        lsf.crc[1]=ccrc&0xFF;

        finished = 0;

        //debug sig with random payloads (don't play the audio)
        for (uint8_t i = 0; i < 16; i++)
            data[i] = 0x69; //rand() & 0xFF;
    }
    else
    {
        //read data
        dummy=fread(&(lsf.dst), 6, 1, stdin);
        dummy=fread(&(lsf.src), 6, 1, stdin);
        dummy=fread(&(lsf.type), 2, 1, stdin);
        dummy=fread(&(lsf.meta), 14, 1, stdin);
        dummy=fread(data, 16, 1, stdin);
    }

    //AES encryption enabled - use 112 bits of IV
    if(encryption==2)
    {
        memcpy(&(lsf.meta), iv, 14);
        iv[14] = (fn >> 8) & 0x7F;
        iv[15] = (fn >> 0) & 0xFF;

        //re-calculate LSF CRC with IV insertion
        uint16_t ccrc=LSF_CRC(&lsf);
        lsf.crc[0]=ccrc>>8;
        lsf.crc[1]=ccrc&0xFF;
    }

    while(!finished)
    {
        if(!got_lsf)
        {
            //debug
            //fprintf(stderr, "LSF\n");

            //send LSF syncword
            frame_buff_cnt=0;
			send_syncword(frame_buff, &frame_buff_cnt, SYNC_LSF);
            fwrite((uint8_t*)frame_buff, SYM_PER_SWD*sizeof(float), 1, stdout);

            //encode LSF data
            conv_encode_LSF(enc_bits, &lsf);

            //reorder bits
            reorder_bits(rf_bits, enc_bits);

            //randomize
            randomize_bits(rf_bits);

			//send LSF data
            frame_buff_cnt=0;
			send_data(frame_buff, &frame_buff_cnt, rf_bits);
            fwrite((uint8_t*)frame_buff, SYM_PER_PLD*sizeof(float), 1, stdout);

            //check the SIGNED STREAM flag
            signed_str=(lsf.type[0]>>3)&1;

            //set the flag
            got_lsf=1;
        }

        if (debug_mode == 1)
        {
            //broadcast
            memset(next_lsf.dst, 0xFF, 6*sizeof(uint8_t));

            //N0CALL
            next_lsf.src[0] = 0x00;
            next_lsf.src[1] = 0x00;
            next_lsf.src[2] = 0x4B;
            next_lsf.src[3] = 0x13;
            next_lsf.src[4] = 0xD1;
            next_lsf.src[5] = 0x06;

            if (encryption == 2) //AES ENC, 3200 voice
            {
                next_lsf.type[0] = 0x03;
                next_lsf.type[1] = 0x95;
            }
            else if (encryption == 1) //Scrambler ENC, 3200 Voice
            {
                next_lsf.type[0] = 0x03;
                next_lsf.type[1] = 0xCD;
            }
            else //no enc or subtype field, normal 3200 voice
            {
                next_lsf.type[0] = 0x00;
                next_lsf.type[1] = 0x05;
            }

            //a signature key is loaded, OR this bit
            if(priv_key_loaded)
                next_lsf.type[0] |= 0x8;

            finished = 0;

            memset(next_data, 0, sizeof(next_data));
            memcpy(data, next_data, sizeof(data));
            if (fn == 60)
                finished = 1;

            //debug sig with random payloads (don't play the audio)
            for (uint8_t i = 0; i < 16; i++)
                data[i] = 0x69; //rand() & 0xFF;
                
        }
        else
        {
            //check if theres any more data
            if(fread(&(next_lsf.dst), 6, 1, stdin)<1) finished=1;
            if(fread(&(next_lsf.src), 6, 1, stdin)<1) finished=1;
            if(fread(&(next_lsf.type), 2, 1, stdin)<1) finished=1;
            if(fread(&(next_lsf.meta), 14, 1, stdin)<1) finished=1;
            if(fread(next_data, 16, 1, stdin)<1) finished=1;
        }

        //AES
        if(encryption==2)
        {
            memcpy(&(next_lsf.meta), iv, 14);
            iv[14] = (fn >> 8) & 0x7F;
            iv[15] = (fn >> 0) & 0xFF;
            aes_ctr_bytewise_payload_crypt(iv, key, data, aes_type);
        }

        //Scrambler
        else if (encryption == 1)
        {
            scrambler_sequence_generator();
            for(uint8_t i=0; i<16; i++)
            {
                data[i] ^= scr_bytes[i];
            }
        }

        if(!finished)
        {
            //debug payload dump
            /*fprintf(stderr, "ENC-Payload %04X: ", fn);
            for(uint8_t i=0; i<sizeof(digest); i++)
                fprintf(stderr, "%02X", data[i]);
            fprintf(stderr, "\n");*/

            //send frame
            frame_buff_cnt=0;
            send_syncword(frame_buff, &frame_buff_cnt, SYNC_STR);
            extract_LICH(lich, lich_cnt, &lsf);
            encode_LICH(lich_encoded, lich);
            unpack_LICH(enc_bits, lich_encoded);
            conv_encode_stream_frame(&enc_bits[96], data, fn);
            reorder_bits(rf_bits, enc_bits);
            randomize_bits(rf_bits);
            send_data(frame_buff, &frame_buff_cnt, rf_bits);
            fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);
            fn = (fn + 1) % 0x8000; //increment FN
            lich_cnt = (lich_cnt + 1) % 6; //continue with next LICH_CNT

            //update the stream digest if required
            if(signed_str)
            {
                for(uint8_t i=0; i<sizeof(digest); i++)
                    digest[i]^=data[i];
                uint8_t tmp=digest[0];
                for(uint8_t i=0; i<sizeof(digest)-1; i++)
                    digest[i]=digest[i+1];
                digest[sizeof(digest)-1]=tmp;
            }

            //update LSF every 6 frames (superframe boundary)
            if(fn>0 && lich_cnt==0)
            {
                lsf = next_lsf; 

                //calculate LSF CRC
                uint16_t ccrc=LSF_CRC(&lsf);
                lsf.crc[0]=ccrc>>8;
                lsf.crc[1]=ccrc&0xFF;
            }

            memcpy(data, next_data, 16);
        }
        else //send last frame(s)
        {
            //debug data dump
            /*fprintf(stderr, "ENC-Payload %04X: ", fn);
            for(uint8_t i=0; i<sizeof(digest); i++)
                fprintf(stderr, "%02X", data[i]);
            fprintf(stderr, "\n");*/

            //send frame
            frame_buff_cnt=0;
            send_syncword(frame_buff, &frame_buff_cnt, SYNC_STR);
            extract_LICH(lich, lich_cnt, &lsf);
            encode_LICH(lich_encoded, lich);
            unpack_LICH(enc_bits, lich_encoded);
            if(!signed_str)
                conv_encode_stream_frame(&enc_bits[96], data, (fn | 0x8000));
            else
                conv_encode_stream_frame(&enc_bits[96], data, fn);
            reorder_bits(rf_bits, enc_bits);
            randomize_bits(rf_bits);
            send_data(frame_buff, &frame_buff_cnt, rf_bits);
            fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);

            //if we are done, and the stream is signed, so we need to transmit the signature (4 frames)
            if(signed_str)
            {
                //update digest
                for(uint8_t i=0; i<sizeof(digest); i++)
                    digest[i]^=data[i];
                uint8_t tmp=digest[0];
                for(uint8_t i=0; i<sizeof(digest)-1; i++)
                    digest[i]=digest[i+1];
                digest[sizeof(digest)-1]=tmp;

                //sign the digest
                uECC_sign(priv_key, digest, sizeof(digest), sig, curve);

                //4 frames with 512-bit signature
                fn = 0x7FFC; //signature has to start at 0x7FFC to end at 0x7FFF (0xFFFF with EoT marker set)
                for(uint8_t i=0; i<4; i++)
                {
                    //dump payload
                    /*fprintf(stderr, "ENC-Payload %04X: ", fn);
                    for(uint8_t i=0; i<sizeof(digest); i++)
                        fprintf(stderr, "%02X", data[i]);
                    fprintf(stderr, "\n");*/

                    frame_buff_cnt=0;
                    send_syncword(frame_buff, &frame_buff_cnt, SYNC_STR);
                    extract_LICH(lich, lich_cnt, &lsf);
                    encode_LICH(lich_encoded, lich);
                    unpack_LICH(enc_bits, lich_encoded);
                    conv_encode_stream_frame(&enc_bits[96], &sig[i*16], fn);
                    reorder_bits(rf_bits, enc_bits);
                    randomize_bits(rf_bits);
                    send_data(frame_buff, &frame_buff_cnt, rf_bits);
                    fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);
                    fn = (fn<0x7FFE) ? fn+1 : (0x7FFF|0x8000);
                    lich_cnt = (lich_cnt + 1) % 6; //continue with next LICH_CNT
                }

                //dump data
                /*fprintf(stderr, "ENC-Digest: ");
                for(uint8_t i=0; i<sizeof(digest); i++)
                    fprintf(stderr, "%02X", digest[i]);
                fprintf(stderr, "\n");

                fprintf(stderr, "Key: ");
                for(uint8_t i=0; i<sizeof(priv_key); i++)
                    fprintf(stderr, "%02X", priv_key[i]);
                fprintf(stderr, "\n");

                fprintf(stderr, "Signature: ");
                for(uint8_t i=0; i<sizeof(sig); i++)
                    fprintf(stderr, "%02X", sig[i]);
                fprintf(stderr, "\n");*/

                if (debug_mode == 1)
                {
                fprintf(stderr, "Signature: ");
                for(uint8_t i=0; i<sizeof(sig); i++)
                {
                    if (i == 16 || i == 32 || i == 48)
                        fprintf(stderr, "\n           ");
                    fprintf(stderr, "%02X", sig[i]);
                }
                    
                fprintf(stderr, "\n");
                }
            }

            //send EOT frame
            frame_buff_cnt=0;
            send_eot(frame_buff, &frame_buff_cnt);
            fwrite((uint8_t*)frame_buff, SYM_PER_PLD*sizeof(float), 1, stdout);
            //fprintf(stderr, "Stream has ended. Exiting.\n");
        }
	}

	return 0;
}

//DEBUG:
//Scrambler
//encode debug with -- ./m17-coder-sym -D -k 123456 > scr.sym
//decode debug with -- m17-fme -r -f scr.sym -v 1 -e 123456

//AES (with file import)
//encode debug with -- ./m17-coder-sym -D -K sample_aes_key.txt> float.sym
//decode debug with -- m17-fme -r -f float.sym -v 1 -J sample_aes_key.txt

//Signatures
//encode debug with -- ./m17-coder-sym -D -s 69b07d7afe7f843e56ecbf536a49461dc5901c975d895bf1649cabff8f9b208b > float.sym
//decode debug with -- cat ../m17-coder/float.sym | ./m17-decoder-sym -s c6c03dd11276aa917e7d83ae16d7f4fbf06f31be5869f9ae8004c329947dc4eeef0d9363653c8edf93e50912c6c515b40e0a8cbeea5e984dbc78e1993c8fbd5d
//decode debug with -- m17-fme -r -f float.sym -v 1 -k ../m17-decoder/sample_pub_key.txt

//Signatures and AES
//encode debug with -- ./m17-coder-sym -D -K sample_aes_key.txt -s 69b07d7afe7f843e56ecbf536a49461dc5901c975d895bf1649cabff8f9b208b > float.sym
//decode debug with -- cat ../m17-coder/float.sym | ./m17-decoder-sym -s c6c03dd11276aa917e7d83ae16d7f4fbf06f31be5869f9ae8004c329947dc4eeef0d9363653c8edf93e50912c6c515b40e0a8cbeea5e984dbc78e1993c8fbd5d -K sample_aes_key.txt
//decode debug with -- m17-fme -r -f float.sym -v 1 -k ../m17-decoder/sample_pub_key.txt -J sample_aes_key.txt

//Signatures and 24-bit Scrambler
//encode debug with -- ./m17-coder-sym -D -k 543210 -s 69b07d7afe7f843e56ecbf536a49461dc5901c975d895bf1649cabff8f9b208b > float.sym
//decode debug with -- cat ../m17-coder/float.sym | ./m17-decoder-sym -s c6c03dd11276aa917e7d83ae16d7f4fbf06f31be5869f9ae8004c329947dc4eeef0d9363653c8edf93e50912c6c515b40e0a8cbeea5e984dbc78e1993c8fbd5d -k 543210
//decode debug with -- m17-fme -r -f float.sym -v 1 -k ../m17-decoder/sample_pub_key.txt -e 543210
