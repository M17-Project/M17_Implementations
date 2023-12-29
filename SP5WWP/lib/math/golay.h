//--------------------------------------------------------------------
// M17 C library - math/golay.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

extern const uint16_t encode_matrix[12];
extern const uint16_t decode_matrix[12];

uint32_t golay24_encode(const uint16_t data);
uint16_t golay24_sdecode(const uint16_t codeword[24]);
void decode_LICH(uint8_t* outp, const uint16_t* inp);

#ifdef __cplusplus
}
#endif
