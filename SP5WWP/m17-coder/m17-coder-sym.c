#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

//libm17
#include <m17.h>
//tinier-aes
#include "../../tinier-aes/aes.h"

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
uint8_t got_lsf=0;                  //have we filled the LSF struct yet?
uint8_t finished=0;

//encryption
uint8_t encryption=0;
int aes_type = 1; //1=AES128, 2=AES192, 3=AES256
uint8_t key[32];
uint8_t iv[16];
time_t epoch = 1577836800L; //Jan 1, 2020, 00:00:00 UTC

//Scrambler
uint8_t scr_bytes[16];
uint8_t scrambler_pn[128];
uint32_t scrambler_key=0;
uint32_t scrambler_seed=0;
int8_t scrambler_subtype = -1;

//debug mode (preset lsf, zero payload for enc testing, etc)
uint8_t debug_mode=0;

//scrambler pn sequence generation
void scrambler_sequence_generator ()
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
    fprintf (stderr, "\nScrambler Key: 0x%06X; Seed: 0x%06X; Subtype: %02d;", scrambler_key, lfsr, scrambler_subtype);
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

void usage()
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "-K - AES Encryption Hex String (-K 7777777777777777777777777777777777777777777777777777777777777777),\n");
    fprintf(stderr, "-F - AES Encryption From File (-F aeskey.txt),\n");
    fprintf(stderr, "-k - Scrambler Encryption Hex String (-k 123456),\n");
    fprintf(stderr, "-D - Debug Mode,\n");
    fprintf(stderr, "-h - help / print usage,\n");
}

//convert a user string (as hex octets) into a uint8_t array for key
void parse_raw_key_string (char * input)
{
    uint8_t * raw = key; //set pointer to the key array

    //since we want this as octets, get strlen value, then divide by two
    uint16_t len = strlen((const char*)input);

    //if zero is returned, just do two
    if (len == 0) len = 2;

    //if odd number, then user didn't pass complete octets, but just add one to len value to make it even
    if (len&1) len++;

    //divide by two to get octet len
    len /= 2;

    //sanity check, maximum len should not exceed 32 for an AES key
    if (len > 32) len = 32;

    char octet_char[3];
    octet_char[2] = 0;
    uint16_t k = 0;
    uint16_t i = 0;

    //debug
    // fprintf (stderr, "\nRaw Len: %d; Raw Octets:", len);

    for (i = 0; i < len; i++)
    {
        strncpy (octet_char, input+k, 2);
        octet_char[2] = 0;
        sscanf (octet_char, "%hhX", &raw[i]);

        //debug
        // fprintf (stderr, " (%s)", octet_char);
        // fprintf (stderr, " %02X", raw[i]);

        k += 2;
    }

    // fprintf (stderr, "\n");
}

//main routine
int main(int argc, char* argv[])
{
    //debug
    //printf("%06X\n", golay24_encode(1)); //golay encoder codeword test
    //printf("%d -> %d -> %d\n", 1, intrl_seq[1], intrl_seq[intrl_seq[1]]); //interleaver bijective reciprocality test, f(f(x))=x
    //return 0;

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
                if(argv[i][1]=='K') //-K - AES Encryption
                {

                    parse_raw_key_string (argv[i+1]);

                    fprintf (stderr, "AES Key:");
                    for (uint8_t i = 0; i < 32; i++)
                    {
                        if (i == 16)
                            fprintf (stderr, "\n        ");
                        fprintf (stderr, " %02X", key[i]);
                    }
                    fprintf (stderr, "\n");


                    encryption=2; //AES key was passed
                }
                else if(argv[i][1]=='F') //-F - AES Encryption (key from file)
                {
                    char fname[128]={'\0'}; //output file
                    if(strlen(&argv[i+1][0])>0)
                        memcpy(fname, &argv[i+1][0], strlen(argv[i+1]));
                    else
                    {
                        fprintf(stderr, "Invalid filename. Exiting...\n");
                        return -1;
                    }

                    FILE *fp;
                    char *source_str;

                    fp = fopen(fname, "r");
                    if (!fp)
                    {
                        fprintf(stderr, "Failed to load file %s.\n", fname);
                        return -1;
                    }
                    source_str = (char*)malloc(64);
                    fread(source_str, 1, 64, fp);
                    fclose(fp);

                    parse_raw_key_string (source_str);

                    free(source_str);

                    fprintf (stderr, "AES Key:");
                    for (uint8_t i = 0; i < 32; i++)
                    {
                        if (i == 16)
                            fprintf (stderr, "\n        ");
                        fprintf (stderr, " %02X", key[i]);
                    }
                    fprintf (stderr, "\n");


                    encryption=2; //AES key was passed
                }
                else if(argv[i][1]=='k') //-k - Scrambler Encryption
                {
                    
                    parse_raw_key_string (argv[i+1]);
                    scrambler_key = (key[0] << 16) | (key[1] << 8) | (key[2] << 0);

                    fprintf (stderr, "Scrambler Key: 0x%06X;", scrambler_key);

                    scrambler_seed = scrambler_key; //initialize the seed with the key value

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

        finished = 0;
    }

    else
    {
        if(fread(&(next_lsf.dst), 6, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.src), 6, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.type), 2, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.meta), 14, 1, stdin)<1) finished=1;
        if(fread(next_data, 16, 1, stdin)<1) finished=1;
    }

    while(!finished)
    {
        if(lich_cnt == 0)
        {
            lsf = next_lsf;

            //calculate LSF CRC
            uint16_t ccrc=LSF_CRC(&lsf);
            lsf.crc[0]=ccrc>>8;
            lsf.crc[1]=ccrc&0xFF;
        }

        memcpy(data, next_data, sizeof(data));

        if (debug_mode == 1)
        {
            memset(next_data, 0, sizeof(next_data));
            memcpy(data, next_data, sizeof(data));
        }

        else
        {
            if(fread(&(next_lsf.dst), 6, 1, stdin)<1) finished=1;
            if(fread(&(next_lsf.src), 6, 1, stdin)<1) finished=1;
            if(fread(&(next_lsf.type), 2, 1, stdin)<1) finished=1;
            if(fread(&(next_lsf.meta), 14, 1, stdin)<1) finished=1;
            if(fread(next_data, 16, 1, stdin)<1) finished=1;
        }

        //AES encryption enabled - use 112 bits of IV
        if(encryption==2)
        {
            memcpy(&(next_lsf.meta), iv, 14);
            iv[14] = (fn >> 8) & 0x7F;
            iv[15] = (fn >> 0) & 0xFF;
        }

        if(got_lsf) //stream frames
        {
            //send stream frame syncword
            frame_buff_cnt=0;
            send_syncword(frame_buff, &frame_buff_cnt, SYNC_STR);

            //extract LICH from the whole LSF
            extract_LICH(lich, lich_cnt, &lsf);

            //encode the LICH
            encode_LICH(lich_encoded, lich);

            //unpack LICH (12 bytes)
            unpack_LICH(enc_bits, lich_encoded);

            //encrypt
            if(encryption==2)
            {
                iv[14] = (fn >> 8) & 0x7F;
                iv[15] = (fn >> 0) & 0xFF;
                aes_ctr_bytewise_payload_crypt(iv, key, data, aes_type);
            }

            else if (encryption == 1)
            {
                scrambler_sequence_generator();
                for(uint8_t i=0; i<16; i++)
                {
                  data[i] ^= scr_bytes[i];
                }
            }

            //encode the rest of the frame (starting at bit 96 - 0..95 are filled with LICH)
            conv_encode_stream_frame(&enc_bits[96], data, finished ? (fn | 0x8000) : fn);

            //reorder bits
            reorder_bits(rf_bits, enc_bits);

            //randomize
            randomize_bits(rf_bits);

            //send dummy symbols (debug)
            /*float s=0.0;
            for(uint8_t i=0; i<SYM_PER_PLD; i++) //40ms * 4800 - 8 (syncword)
                fwrite((uint8_t*)&s, sizeof(float), 1, stdout);*/

			//send frame data
			send_data(frame_buff, &frame_buff_cnt, rf_bits);
            fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);

            /*printf("\tDATA: ");
            for(uint8_t i=0; i<16; i++)
                printf("%02X", data[i]);
            printf("\n");*/

            //increment the Frame Number
            fn = (fn + 1) % 0x8000;

            //increment the LICH counter
            lich_cnt = (lich_cnt + 1) % 6;

            //debug-only
			if (debug_mode == 1)
            {
                if(fn==6*10)
                    return 0;
            }
            
        }
        else //LSF
        {
            got_lsf=1;

            //send out the preamble
            frame_buff_cnt=0;
			send_preamble(frame_buff, &frame_buff_cnt, 0); //0 - LSF preamble, as opposed to 1 - BERT preamble
            fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);

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

            //send dummy symbols (debug)
            /*float s=0.0;
            for(uint8_t i=0; i<184; i++) //40ms * 4800 - 8 (syncword)
                write((uint8_t*)&s, sizeof(float), 1, stdout);*/

            /*printf("DST: ");
            for(uint8_t i=0; i<6; i++)
                printf("%02X", lsf.dst[i]);
            printf(" SRC: ");
            for(uint8_t i=0; i<6; i++)
                printf("%02X", lsf.src[i]);
            printf(" TYPE: ");
            for(uint8_t i=0; i<2; i++)
                printf("%02X", lsf.type[i]);
            printf(" META: ");
            for(uint8_t i=0; i<14; i++)
                printf("%02X", lsf.meta[i]);
            printf(" CRC: ");
            for(uint8_t i=0; i<2; i++)
                printf("%02X", lsf.crc[i]);
			printf("\n");*/
		}

        if(finished)
        {
            frame_buff_cnt=0;
            send_eot(frame_buff, &frame_buff_cnt);
            fwrite((uint8_t*)frame_buff, SYM_PER_PLD*sizeof(float), 1, stdout);
        }
	}

	return 0;
}

//DEBUG:

//AES
//encode debug with -- ./m17-coder-sym -D -K 7777777777777777777777777777777777777777777777777777777777777777 > float.sym
//decode debug with -- m17-fme -r -f float.sym -v 1 -E '7777777777777777 7777777777777777 7777777777777777 7777777777777777'

//Scrambler
//encode debug with -- ./m17-coder-sym -D -k 123456 > scr.sym
//decode debug with -- m17-fme -r -f scr.sym -v 1 -e 123456

//AES (with file import)
//encode debug with -- ./m17-coder-sym -D -F aeskey.txt > float.sym
//decode debug with -- m17-fme -r -f float.sym -v 1 -E '7777777777777777 7777777777777777 7777777777777777 7777777777777777'
