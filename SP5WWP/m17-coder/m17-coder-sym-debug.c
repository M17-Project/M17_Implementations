#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

//libm17
#include <m17.h>
//tinier-aes
#include "../../tinier-aes/aes.c"

//#define FN60_DEBUG

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
int type = 1; //1=AES128, 2=AES192, 3=AES256

uint8_t key[32]; //TODO: replace with a `-K` arg key entry
uint8_t iv[16];

time_t epoch = 1577836800L;         //Jan 1, 2020, 00:00:00 UTC

//main routine
int main(int argc, char* argv[])
{
    //debug
    //printf("%06X\n", golay24_encode(1)); //golay encoder codeword test
    //printf("%d -> %d -> %d\n", 1, intrl_seq[1], intrl_seq[intrl_seq[1]]); //interleaver bijective reciprocality test, f(f(x))=x
    //return 0;

    //encryption init
    if(argc>1 && strstr(argv[1], "-K"))
      encryption=2; //AES key was passed

    srand(time(NULL)); //seed random number generator
    memset(key, 0, 32*sizeof(uint8_t));
    memset(iv, 0, 16*sizeof(uint8_t));

    //generate random key
    // for (uint8_t i = 0; i < 32; i++)
    //     key[i] = rand() & 0xFF;

    //hard coded key
    for (uint8_t i = 0; i < 32; i++)
      key[i] = 0x77;

    //print the random key
    fprintf (stderr, "\nAES Key:");
    for (uint8_t i = 0; i < 32; i++)
    {
      if (i == 16)
        fprintf (stderr, "\n        ");
      fprintf (stderr, " %02X", key[i]);
    }
    fprintf (stderr, "\n");

    if(encryption==2)
    {
        //TODO: read user input key
        
        // *((int32_t*)&iv[0])=(uint32_t)time(NULL)-(uint32_t)epoch; //timestamp //note: I don't think this works as intended
        for(uint8_t i=0; i<4; i++)    iv[i] = ((uint32_t)(time(NULL)&0xFFFFFFFF)-(uint32_t)epoch) >> (24-(i*8));
        for(uint8_t i=4; i<4+10; i++) iv[i] = rand() & 0xFF; //10 random bytes
    }

    memset(next_lsf.dst, 0xFF, 6*sizeof(uint8_t)); //broadcast

    //AB1CDE
    next_lsf.src[0] = 0x00;
    next_lsf.src[1] = 0x00;
    next_lsf.src[2] = 0x1F;
    next_lsf.src[3] = 0x24;
    next_lsf.src[4] = 0x5D;
    next_lsf.src[5] = 0x51;

    if (encryption == 2) //AES ENC, 3200 voice
    {
      next_lsf.type[0] = 0x03;
      next_lsf.type[1] = 0x95;
    }
    else //no enc or enc_st field, normal 3200 voice
    {
      next_lsf.type[0] = 0x00;
      next_lsf.type[1] = 0x05;
    }

    finished = 0;

    if (encryption == 2)
    {
      memcpy(&(next_lsf.meta), iv, 14); //AES encryption enabled - use 112 bits of IV
    }
    else //scrambler, or clear
    {
      memset(next_lsf.meta, 0, 14*sizeof(uint8_t));
    }

    // while(!finished)
    for (uint8_t z = 0; z < 31; z++) //just crank out 5 superframes
    {
        if(lich_cnt == 0)
        {
            lsf = next_lsf;

            //calculate LSF CRC
            uint16_t ccrc=LSF_CRC(&lsf);
            lsf.crc[0]=ccrc>>8;
            lsf.crc[1]=ccrc&0xFF;
        }

        memset(next_data, 0, sizeof(next_data));
        memcpy(data, next_data, sizeof(data));

        if (encryption==2) //AES encryption enabled - use 112 bits of IV
        {
          memcpy(&(next_lsf.meta), iv, 14);
          iv[14] = (fn >> 8) & 0xFF;
          iv[15] = (fn >> 0) & 0xFF;
        }
        else //Scrambler, or Clear
        {
          memset(next_lsf.meta, 0, 14*sizeof(uint8_t));
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
                fprintf(stderr, "FN: %03d; IV: ", fn);
                for(uint8_t i=0; i<16; i++)
                    fprintf(stderr, "%02X", iv[i]);
                fprintf(stderr, "\n");

                fprintf(stderr, "\t IN: ");
                for(uint8_t i=0; i<16; i++)
                    fprintf(stderr, "%02X", data[i]);
                fprintf(stderr, "\n");

                aes_ctr_bytewise_payload_crypt(iv, key, data, type);

                fprintf(stderr, "\tOUT: ");
                for(uint8_t i=0; i<16; i++)
                    fprintf(stderr, "%02X", data[i]);
                fprintf(stderr, "\n");
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

            // fprintf(stderr, "\tDATA: ");
            // for(uint8_t i=0; i<16; i++)
            //     fprintf(stderr, "%02X", data[i]);
            // fprintf(stderr, "\n");

            //increment the Frame Number
            fn = (fn + 1) % 0x8000;

            //increment the LICH counter
            lich_cnt = (lich_cnt + 1) % 6;

            //debug-only
			#ifdef FN60_DEBUG
            if(fn==6*10)
                return 0;
			#endif
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

//encode debug with -- ./m17-coder-sym-debug -K > float.sym
//decode debug with -- m17-fme -r -f float.sym -v 1 -E '7777777777777777 7777777777777777 7777777777777777 7777777777777777'