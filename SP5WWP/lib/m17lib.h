//-------------------------------
// M17 C library - m17lib.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 28 December 2023
//-------------------------------

#ifndef M17_LIB
#define M17_LIB

#ifdef __cplusplus
extern "C" {
#endif
#include <inttypes.h>

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
#define K			5                               //constraint length
#define NUM_STATES	(1 << (K - 1))                  //number of states

//type declarations
struct LSF
{
	uint8_t dst[6];
	uint8_t src[6];
	uint8_t type[2];
	uint8_t meta[112/8];
	uint8_t crc[2];
};

//consts
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

//Golay coding
extern const uint16_t encode_matrix[12];
extern const uint16_t decode_matrix[12];

//randomizing pattern
extern const uint8_t rand_seq[46];

//interleaver pattern
extern const uint16_t intrl_seq[368];

//functions
uint16_t CRC_M17(const uint8_t *in, const uint16_t len);

uint32_t golay24_encode(const uint16_t data);
void int_to_soft(uint16_t* out, const uint16_t in, uint8_t len);
uint16_t soft_to_int(const uint16_t* in, uint8_t len);
uint16_t div16(uint16_t a, uint16_t b);
uint16_t mul16(uint16_t a, uint16_t b);
uint16_t soft_bit_XOR(const uint16_t a, const uint16_t b);
void soft_XOR(uint16_t* out, const uint16_t* a, const uint16_t* b, uint8_t len);
uint32_t s_popcount(const uint16_t* in, uint8_t siz);
void s_calc_checksum(uint16_t* out, const uint16_t* value);
uint32_t s_detect_errors(const uint16_t* codeword);
uint16_t golay24_sdecode(const uint16_t* codeword);

uint32_t viterbi_decode(uint8_t* out, const uint16_t* in, uint16_t len);
uint32_t viterbi_decode_punctured(uint8_t* out, const uint16_t* in, const uint8_t* punct, const uint16_t in_len, const uint16_t p_len);
void viterbi_decode_bit(uint16_t s0, uint16_t s1, size_t pos);
uint32_t viterbi_chainback(uint8_t* out, size_t pos, uint16_t len);
uint16_t q_abs_diff(const uint16_t v1, const uint16_t v2);
void viterbi_reset(void);

void send_preamble(const uint8_t type);
void send_syncword(const uint16_t syncword);
void send_data(const uint8_t* in);
void send_eot(void);
void conv_encode_stream_frame(uint8_t* out, const uint8_t* in, const uint16_t fn);
void conv_encode_packet_frame(uint8_t* out, uint8_t* in);
void conv_encode_LSF(uint8_t* out, const struct LSF *in);
uint16_t LSF_CRC(const struct LSF *in);

void decode_callsign_bytes(uint8_t *outp, const uint8_t *inp);
void decode_callsign_value(uint8_t *outp, const uint64_t inp);
uint8_t encode_callsign(uint64_t* out, const uint8_t* inp);
void decode_LICH(uint8_t* outp, const uint16_t* inp);

float eucl_norm(const float* in1, const int8_t* in2, uint8_t len);
#ifdef __cplusplus
}
#endif

#endif
