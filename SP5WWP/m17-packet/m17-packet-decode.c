#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

//libm17
#include <m17.h>

#define DIST_THRESH					2.0f //distance threshold for the L2 metric (for syncword detection)

float sample;                       //last raw sample from the stdin
float last[8];                      //look-back buffer for finding syncwords
float dist;                         //Euclidean distance for finding syncwords in the symbol stream
float pld[SYM_PER_PLD];             //raw frame symbols
uint16_t soft_bit[2*SYM_PER_PLD];   //raw frame soft bits
uint16_t d_soft_bit[2*SYM_PER_PLD]; //deinterleaved soft bits

uint8_t lsf[30+1];                  //complete LSF (one byte extra needed for the Viterbi decoder)
uint8_t frame_data[26+1];           //decoded frame data, 206 bits, plus 4 flushing bits
uint8_t packet_data[33*25];         //whole packet data

uint8_t syncd=0;                    //syncword found?
uint8_t fl=0;                       //Frame=0 of LSF=1
int8_t last_fn;                     //last received frame number (-1 when idle)
uint8_t pushed;                     //counter for pushed symbols

uint8_t skip_payload_crc_check=0;   //skip payload CRC check
uint8_t callsigns=0;                //decode callsigns?
uint8_t show_viterbi=0;             //show Viterbi errors?
uint8_t text_only=0;                //display text only (for text message mode)
uint8_t show_errorless=0;           //display error-less frames only (based on CRC match)

int main(int argc, char* argv[])
{
    //scan command line options - if there are any
    //TODO: support for strings with spaces, the code below is NOT foolproof!
    if(argc>1)
    {
        for(uint8_t i=1; i<argc; i++)
        {
            if(argv[i][0]=='-')
            {
                if(argv[i][1]=='c') //-c - decode callsigns
                {
                    callsigns=1;
                }
                else if(argv[i][1]=='t') //-t - display text oly (in short text message mode)
                {
                    text_only=1;
                }
                else if(argv[i][1]=='s') //-s - skip payload CRC check
                {
                    skip_payload_crc_check=1;
                }
                else if(argv[i][1]=='v') //-v - show Viterbi errors
                {
                    show_viterbi=1;
                }
                else if(argv[i][1]=='f') //-f - show error-less frames only
                {
                    show_errorless = 1;
                }
                else if(argv[i][1]=='h') //-h - help on usage
                {
                    fprintf(stderr, "Usage:\n");
                    fprintf(stderr, "-c - decode callsigns,\n");
                    fprintf(stderr, "-f - decode error-free frames only,\n");
                    fprintf(stderr, "-h - display this help message and exit\n");
                    fprintf(stderr, "-s - skip payload CRC check,\n");
                    fprintf(stderr, "-t - display text payload only,\n");
                    fprintf(stderr, "-v - show detected errors at the Viterbi decoder,\n");
                    return -1;
                }
                else
                {
                    fprintf(stderr, "Unknown param detected. Exiting...\n");
                    return -1; //unknown option
                }
            }
        }
    }

    while(fread((uint8_t*)&sample, 4, 1, stdin) > 0)
    {
        if(!syncd)
        {
            //push new symbol
            for(uint8_t i=0; i<7; i++)
            {
                last[i]=last[i+1];
            }

            last[7]=sample;

            //calculate euclidean norm
            dist = eucl_norm(last, pkt_sync_symbols, 8);

            //fprintf(stderr, "pkt_sync dist: %3.5f\n", dist);
            if(dist<DIST_THRESH) //frame syncword detected
            {
                //fprintf(stderr, "pkt_sync\n");
                syncd=1;
                pushed=0;
                fl=0;
            }
            else
            {
                //calculate euclidean norm again, this time against LSF syncword
                dist = eucl_norm(last, lsf_sync_symbols, 8);

                //fprintf(stderr, "lsf_sync dist: %3.5f\n", dist);
                if(dist<DIST_THRESH) //LSF syncword
                {
                    //fprintf(stderr, "lsf_sync\n");
                    syncd=1;
                    pushed=0;
                    last_fn=-1;
                    memset(packet_data, 0, 33*25);
                    fl=1;
                }
            }
        }
        else
        {
            pld[pushed++]=sample;

            if(pushed==SYM_PER_PLD) //frame acquired
            {
                //get current time
                time_t now = time(NULL);
				struct tm* tm_now = localtime(&now);

                for(uint8_t i=0; i<SYM_PER_PLD; i++)
                {
                    //bit 0
                    if(pld[i]>=symbol_list[3])
                    {
                        soft_bit[i*2+1]=0xFFFF;
                    }
                    else if(pld[i]>=symbol_list[2])
                    {
                        soft_bit[i*2+1]=-(float)0xFFFF/(symbol_list[3]-symbol_list[2])*symbol_list[2]+pld[i]*(float)0xFFFF/(symbol_list[3]-symbol_list[2]);
                    }
                    else if(pld[i]>=symbol_list[1])
                    {
                        soft_bit[i*2+1]=0x0000;
                    }
                    else if(pld[i]>=symbol_list[0])
                    {
                        soft_bit[i*2+1]=(float)0xFFFF/(symbol_list[1]-symbol_list[0])*symbol_list[1]-pld[i]*(float)0xFFFF/(symbol_list[1]-symbol_list[0]);
                    }
                    else
                    {
                        soft_bit[i*2+1]=0xFFFF;
                    }

                    //bit 1
                    if(pld[i]>=symbol_list[2])
                    {
                        soft_bit[i*2]=0x0000;
                    }
                    else if(pld[i]>=symbol_list[1])
                    {
                        soft_bit[i*2]=0x7FFF-pld[i]*(float)0xFFFF/(symbol_list[2]-symbol_list[1]);
                    }
                    else
                    {
                        soft_bit[i*2]=0xFFFF;
                    }
                }

                //derandomize
                for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
                {
                    if((rand_seq[i/8]>>(7-(i%8)))&1) //soft XOR. flip soft bit if "1"
                        soft_bit[i]=0xFFFF-soft_bit[i];
                }

                //deinterleave
                for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
                {
                    d_soft_bit[i]=soft_bit[intrl_seq[i]];
                }

                //if it is a frame
                if(!fl)
                {
                    //decode
                    uint32_t e=viterbi_decode_punctured(frame_data, d_soft_bit, puncture_pattern_3, SYM_PER_PLD*2, 8);

                    //dump FN
                    uint8_t rx_fn=(frame_data[26]>>2)&0x1F;
                    uint8_t rx_last=frame_data[26]>>7;
                    //fprintf(stderr, "FN%d, (%d)\n", rx_fn, rx_last);

                    if(show_viterbi)
                    {
                        fprintf(stderr, "   \033[93mFrame %d Viterbi error:\033[39m %1.1f\n", rx_last?last_fn+1:rx_fn, (float)e/0xFFFF);
                    }

                    //copy data - might require some fixing
                    if(rx_fn<=31 && rx_fn==last_fn+1 && !rx_last)
                    {
                        memcpy(&packet_data[rx_fn*25], &frame_data[1], 25);
                        last_fn++;
                    }
                    else if(rx_last)
                    {
                        memcpy(&packet_data[(last_fn+1)*25], &frame_data[1], rx_fn);
                        uint16_t p_len=strlen((const char*)packet_data);

                        if(show_errorless==0 || (show_errorless==1 && CRC_M17(packet_data, p_len+3)==0))
                        {
                            fprintf(stderr, " \033[93mContent\033[39m\n");

                            //dump data
                            if(packet_data[0]==0x05) //if a text message
                            {
                                fprintf(stderr, " ├ \033[93mType:\033[39m SMS\n");

                                if(skip_payload_crc_check)
                                {
                                    fprintf(stderr, " └ \033[93mText:\033[39m %s\n", &packet_data[1]);
                                }
                                else
                                {
                                    fprintf(stderr, " ├ \033[93mText:\033[39m %s\n", &packet_data[1]);

                                    //CRC
                                    fprintf(stderr, " └ \033[93mPayload CRC:\033[39m");
                                    if(CRC_M17(packet_data, p_len+3)) //3: terminating null plus a 2-byte CRC
                                        fprintf(stderr, " \033[91mmismatch\033[39m\n");
                                    else
                                        fprintf(stderr, " \033[92mmatch\033[39m\n");
                                }
                            }
                            else
                            {
                                if(!text_only)
                                {
                                    fprintf(stderr, " └ \033[93mPayload:\033[39m ");
                                    for(uint16_t i=0; i<(last_fn+1)*25+rx_fn; i++)
                                    {
                                        if(i!=0 && (i%25)==0)
                                            fprintf(stderr, "\n     ");
                                        fprintf(stderr, " %02X", packet_data[i]);
                                    }
                                    fprintf(stderr, "\n");
                                }
                            }
                        }
                    }
                }
                else //if it is LSF
                {
                    //decode
                    uint32_t e=viterbi_decode_punctured(lsf, d_soft_bit, puncture_pattern_1, 2*SYM_PER_PLD, 61);

                    //shift the buffer 1 position left - get rid of the encoded flushing bits
                    for(uint8_t i=0; i<30; i++)
                        lsf[i]=lsf[i+1];

                    if(show_errorless==0 || (show_errorless==1 && CRC_M17(lsf, 30)==0))
                    {
                        fprintf(stderr, "\033[96m[%02d:%02d:%02d] \033[92mPacket received\033[39m\n", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

                        if(!text_only)
                        {
                            //dump data
                            if(callsigns)
                            {
                                uint8_t d_dst[12], d_src[12]; //decoded strings

                                decode_callsign_bytes(d_dst, &lsf[0]);
                                decode_callsign_bytes(d_src, &lsf[6]);

                                //DST
                                fprintf(stderr, " ├ \033[93mDestination:\033[39m %s\n", d_dst);

                                //SRC
                                fprintf(stderr, " ├ \033[93mSource:\033[39m %s\n", d_src);
                            }
                            else
                            {
                                //DST
                                fprintf(stderr, " ├ \033[93mDestination:\033[39m ");
                                for(uint8_t i=0; i<6; i++)
                                    fprintf(stderr, "%02X", lsf[i]);
                                fprintf(stderr, "\n");

                                //SRC
                                fprintf(stderr, " ├ \033[93mSource:\033[39m ");
                                for(uint8_t i=0; i<6; i++)
                                    fprintf(stderr, "%02X", lsf[6+i]);
                                fprintf(stderr, "\n");
                            }

                            //TYPE
                            fprintf(stderr, " ├ \033[93mType:\033[39m ");
                            for(uint8_t i=0; i<2; i++)
                                fprintf(stderr, "%02X", lsf[12+i]);
                            fprintf(stderr, "\n");

                            //META
                            fprintf(stderr, " ├ \033[93mMeta:\033[39m ");
                            for(uint8_t i=0; i<14; i++)
                                fprintf(stderr, "%02X", lsf[14+i]);
                            fprintf(stderr, "\n");

                            //Viterbi decoder errors
                            if(show_viterbi)
                            {
                                fprintf(stderr, " ├ \033[93mLSF Viterbi error:\033[39m %1.1f\n", (float)e/0xFFFF);
                            }

                            //CRC
                            fprintf(stderr, " └ \033[93mLSF CRC:\033[39m");
                            if(CRC_M17(lsf, 30))
                                fprintf(stderr, " \033[91mmismatch\033[39m\n");
                            else
                                fprintf(stderr, " \033[92mmatch\033[39m\n");
                        }
                    }
                }

                //job done
                syncd=0;
                pushed=0;

                for(uint8_t i=0; i<8; i++)
                    last[i]=0.0;
            }
        }
    }
}
