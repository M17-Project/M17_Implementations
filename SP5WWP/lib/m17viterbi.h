//--------------------------------------------------------------------
// M17 C library - m17viterbi.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#ifndef M17_VITERBI_LIB
#define M17_VITERBI_LIB

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

//vars
;

//functions
uint32_t viterbi_decode(uint8_t* out, const uint16_t* in, const uint16_t len);
uint32_t viterbi_decode_punctured(uint8_t* out, const uint16_t* in, const uint8_t* punct, const uint16_t in_len, const uint16_t p_len);
void viterbi_decode_bit(uint16_t s0, uint16_t s1, size_t pos);
uint32_t viterbi_chainback(uint8_t* out, size_t pos, const uint16_t len);
void viterbi_reset(void);

#ifdef __cplusplus
}
#endif
#endif