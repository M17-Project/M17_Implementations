//--------------------------------------------------------------------
// M17 C library - m17consts.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#ifndef M17_CONSTS
#define M17_CONSTS

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#define BSB_SPS             10                      //samples per symbol
#define FLT_SPAN            8                       //baseband RRC filter span in symbols
#define SYM_PER_SWD         8                       //symbols per syncword
#define SW_LEN              (BSB_SPS*SYM_PER_SWD)   //syncword detector length
#define SYM_PER_PLD         184                     //symbols per payload in a frame
#define SYM_PER_FRA         192                     //symbols per whole 40 ms frame
#define RRC_DEV             7168.0f                 //.rrc file deviation for +1.0 symbol

//L2 metric threshold
#define DIST_THRESH         2.0f                    //threshold for distance (syncword detection)

//Viterbi
#define K			        5                       //constraint length
#define NUM_STATES	        (1 << (K - 1))          //number of states

//syncword patterns (RX) TODO:Compute those at runtime from the consts below
extern const int8_t lsf_sync[8];
extern const int8_t str_sync[8];
extern const int8_t pkt_sync[8];

//symbol levels (RX)
extern const float symbs[4];

//dibits-symbols map (TX)
extern const int8_t symbol_map[4];

//syncwords
extern const uint16_t SYNC_LSF;
extern const uint16_t SYNC_STR;
extern const uint16_t SYNC_PKT;
extern const uint16_t SYNC_BER;
extern const uint16_t EOT_MRKR;

//puncturing pattern P_1
extern const uint8_t P_1[61];

//puncturing pattern P_2
extern const uint8_t P_2[12];

//puncturing pattern P_3
extern const uint8_t P_3[8];

//M17 CRC polynomial
extern const uint16_t M17_CRC_POLY;

//sample RRC filter for 48kHz sample rate
//alpha=0.5, span=8, sps=10, gain=sqrt(sps)
extern const float taps_10[8*10+1];

//sample RRC filter for 24kHz sample rate
//alpha=0.5, span=8, sps=5, gain=sqrt(sps)
extern const float taps_5[8*5+1];

//randomizing pattern
extern const uint8_t rand_seq[46];

//interleaver pattern
extern const uint16_t intrl_seq[368];

/**
 * @brief Structure holding Link Setup Frame data.
 * 
 */
typedef struct LSF
{
	uint8_t dst[6];
	uint8_t src[6];
	uint8_t type[2];
	uint8_t meta[112/8];
	uint8_t crc[2];
};

#ifdef __cplusplus
}
#endif
#endif
