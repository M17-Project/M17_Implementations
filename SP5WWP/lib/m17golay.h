//--------------------------------------------------------------------
// M17 C library - m17golay.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#ifndef M17_GOLAY_LIB
#define M17_GOLAY_LIB

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

//consts
extern const uint16_t encode_matrix[12];
extern const uint16_t decode_matrix[12];

//functions
uint32_t golay24_encode(const uint16_t data);
uint16_t golay24_sdecode(const uint16_t codeword[24]);
void decode_LICH(uint8_t* outp, const uint16_t* inp);

#ifdef __cplusplus
}
#endif
#endif
