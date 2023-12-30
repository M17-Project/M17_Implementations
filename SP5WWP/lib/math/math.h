//--------------------------------------------------------------------
// M17 C library - math/math.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

uint16_t q_abs_diff(const uint16_t v1, const uint16_t v2);
float eucl_norm(const float* in1, const int8_t* in2, const uint8_t n);
void int_to_soft(uint16_t* out, const uint16_t in, const uint8_t len);
uint16_t soft_to_int(const uint16_t* in, const uint8_t len);
uint16_t div16(const uint16_t a, const uint16_t b);
uint16_t mul16(const uint16_t a, const uint16_t b);
uint16_t soft_bit_XOR(const uint16_t a, const uint16_t b);
void soft_XOR(uint16_t* out, const uint16_t* a, const uint16_t* b, const uint8_t len);

#ifdef __cplusplus
}
#endif
