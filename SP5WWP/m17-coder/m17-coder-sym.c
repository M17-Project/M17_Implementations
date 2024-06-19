#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

//libm17
#include <m17.h>
//micro-ecc
#include "../../micro-ecc/uECC.h"

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

//used for signatures
uint8_t digest[16]={0};             //16-byte field for the stream digest
uint8_t signed_str=0;               //is the stream supposed to be signed?

//main routine
int main(void)
{
    //debug
    //printf("%06X\n", golay24_encode(1)); //golay encoder codeword test
    //printf("%d -> %d -> %d\n", 1, intrl_seq[1], intrl_seq[intrl_seq[1]]); //interleaver bijective reciprocality test, f(f(x))=x
    //return 0;

    if(fread(&(next_lsf.dst), 6, 1, stdin)<1) finished=1;
    if(fread(&(next_lsf.src), 6, 1, stdin)<1) finished=1;
    if(fread(&(next_lsf.type), 2, 1, stdin)<1) finished=1;
    if(fread(&(next_lsf.meta), 14, 1, stdin)<1) finished=1;
    if(fread(next_data, 16, 1, stdin)<1) finished=1;

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

        //calculate stream digest
        signed_str=(lsf.type[0]>>3)&1;
        if(signed_str) //signed stream? check bit 11 of TYPE
        {
            for(uint8_t i=0; i<sizeof(digest); i++)
                digest[i]^=data[i];
            uint8_t tmp=digest[0];
            for(uint8_t i=0; i<sizeof(digest)-1; i++)
                digest[i]=digest[i+1];
            digest[sizeof(digest)-1]=tmp;
        }

        //we could discard the data we already have
        if(fread(&(next_lsf.dst), 6, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.src), 6, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.type), 2, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.meta), 14, 1, stdin)<1) finished=1;
        if(fread(next_data, 16, 1, stdin)<1) finished=1;

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

            //encode the rest of the frame (starting at bit 96 - 0..95 are filled with LICH)
            if(!signed_str)
                conv_encode_stream_frame(&enc_bits[96], data, finished ? (fn | 0x8000) : fn);
            else //dont set the MSB is the stream is signed
            {
                conv_encode_stream_frame(&enc_bits[96], data, fn);
            }

            //reorder bits
            reorder_bits(rf_bits, enc_bits);

            //randomize
            randomize_bits(rf_bits);

			//send frame data
			send_data(frame_buff, &frame_buff_cnt, rf_bits);
            fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);

            //increment the Frame Number
            fn = (fn + 1) % 0x8000;

            //increment the LICH counter
            lich_cnt = (lich_cnt + 1) % 6;

            if(finished && signed_str) //if we are done, and the stream is signed, so we need to transmit the signature (4 frames)
            {
                uint8_t sig[64];

                for(uint8_t i=0; i<sizeof(sig); i++) //test fill
                    sig[i]=i;

                //1 of 4
                fn = 0x7FFC; //signature has to start at 0x7FFC to end at 0x7FFF (0xFFFF with EoT marker set)
                for(uint8_t i=0; i<4; i++)
                {
                    frame_buff_cnt=0;
                    send_syncword(frame_buff, &frame_buff_cnt, SYNC_STR);
                    extract_LICH(lich, lich_cnt, &lsf); //continue with next LICH_CNT
                    encode_LICH(lich_encoded, lich);
                    unpack_LICH(enc_bits, lich_encoded);
                    conv_encode_stream_frame(&enc_bits[96], &sig[i*16], fn);
                    reorder_bits(rf_bits, enc_bits);
                    randomize_bits(rf_bits);
                    send_data(frame_buff, &frame_buff_cnt, rf_bits);
                    fwrite((uint8_t*)frame_buff, SYM_PER_FRA*sizeof(float), 1, stdout);
                    fn = (fn<0x7FFE) ? fn+1 : (0x7FFF|0x8000);
                    lich_cnt = (lich_cnt + 1) % 6;
                }
            }

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
