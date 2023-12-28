#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../lib/m17lib.h"

//#define FN60_DEBUG

struct LSF lsf, next_lsf;

uint8_t lich[6];                    //48 bits packed raw, unencoded LICH
uint8_t lich_encoded[12];           //96 bits packed, encoded LICH
uint8_t enc_bits[SYM_PER_PLD*2];    //type-2 bits, unpacked
uint8_t rf_bits[SYM_PER_PLD*2];     //type-4 bits, unpacked

uint8_t data[16], next_data[16];    //raw payload, packed bits
uint16_t fn=0;                      //16-bit Frame Number (for the stream mode)
uint8_t lich_cnt=0;                 //0..5 LICH counter
uint8_t got_lsf=0;                  //have we filled the LSF struct yet?
uint8_t finished=0;

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

        //we could discard the data we already have
        if(fread(&(next_lsf.dst), 6, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.src), 6, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.type), 2, 1, stdin)<1) finished=1;
        if(fread(&(next_lsf.meta), 14, 1, stdin)<1) finished=1;
        if(fread(next_data, 16, 1, stdin)<1) finished=1;

        if(got_lsf) //stream frames
        {
            //send stream frame syncword
            send_syncword(SYNC_STR);

            //extract LICH from the whole LSF
            switch(lich_cnt)
            {
                case 0:
                    lich[0]=lsf.dst[0];
                    lich[1]=lsf.dst[1];
                    lich[2]=lsf.dst[2];
                    lich[3]=lsf.dst[3];
                    lich[4]=lsf.dst[4];
                break;

                case 1:
                    lich[0]=lsf.dst[5];
                    lich[1]=lsf.src[0];
                    lich[2]=lsf.src[1];
                    lich[3]=lsf.src[2];
                    lich[4]=lsf.src[3];
                break;

                case 2:
                    lich[0]=lsf.src[4];
                    lich[1]=lsf.src[5];
                    lich[2]=lsf.type[0];
                    lich[3]=lsf.type[1];
                    lich[4]=lsf.meta[0];
                break;

                case 3:
                    lich[0]=lsf.meta[1];
                    lich[1]=lsf.meta[2];
                    lich[2]=lsf.meta[3];
                    lich[3]=lsf.meta[4];
                    lich[4]=lsf.meta[5];
                break;

                case 4:
                    lich[0]=lsf.meta[6];
                    lich[1]=lsf.meta[7];
                    lich[2]=lsf.meta[8];
                    lich[3]=lsf.meta[9];
                    lich[4]=lsf.meta[10];
                break;

                case 5:
                    lich[0]=lsf.meta[11];
                    lich[1]=lsf.meta[12];
                    lich[2]=lsf.meta[13];
                    lich[3]=lsf.crc[0];
                    lich[4]=lsf.crc[1];
                break;

                default:
                    ;
                break;
            }
            lich[5]=lich_cnt<<5;

            //encode the LICH
            uint32_t val;

            val=golay24_encode((lich[0]<<4)|(lich[1]>>4));
            lich_encoded[0]=(val>>16)&0xFF;
            lich_encoded[1]=(val>>8)&0xFF;
            lich_encoded[2]=(val>>0)&0xFF;
            val=golay24_encode(((lich[1]&0x0F)<<8)|lich[2]);
            lich_encoded[3]=(val>>16)&0xFF;
            lich_encoded[4]=(val>>8)&0xFF;
            lich_encoded[5]=(val>>0)&0xFF;
            val=golay24_encode((lich[3]<<4)|(lich[4]>>4));
            lich_encoded[6]=(val>>16)&0xFF;
            lich_encoded[7]=(val>>8)&0xFF;
            lich_encoded[8]=(val>>0)&0xFF;
            val=golay24_encode(((lich[4]&0x0F)<<8)|lich[5]);
            lich_encoded[9]=(val>>16)&0xFF;
            lich_encoded[10]=(val>>8)&0xFF;
            lich_encoded[11]=(val>>0)&0xFF;

            //unpack LICH (12 bytes)
            memset(enc_bits, 0, SYM_PER_PLD*2);
            for(uint8_t i=0; i<12; i++)
            {
                for(uint8_t j=0; j<8; j++)
                    enc_bits[i*8+j]=(lich_encoded[i]>>(7-j))&1;
            }

            //encode the rest of the frame
            conv_encode_stream_frame(&enc_bits[96], data, finished ? (fn | 0x8000) : fn);

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

            //send dummy symbols (debug)
            /*float s=0.0;
            for(uint8_t i=0; i<SYM_PER_PLD; i++) //40ms * 4800 - 8 (syncword)
                fwrite((uint8_t*)&s, sizeof(float), 1, stdout);*/

			//send frame data
			send_data(rf_bits);

            /*printf("\tDATA: ");
            for(uint8_t i=0; i<16; i++)
                printf("%02X", data[i]);
            printf("\n");*/

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

            //encode LSF data
            conv_encode_LSF(enc_bits, &lsf);

            //send out the preamble and LSF
			send_preamble(0); //0 - LSF preamble, as opposed to 1 - BERT preamble

            //send LSF syncword
			send_syncword(SYNC_LSF);

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

			//send LSF data
			send_data(rf_bits);

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
            send_eot();
	}

	return 0;
}
