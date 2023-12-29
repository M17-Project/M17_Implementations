//--------------------------------------------------------------------
// M17 C library - encode/convol.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <payload/lsf.h>

extern const uint8_t puncture_pattern_1[61];
extern const uint8_t puncture_pattern_2[12];
extern const uint8_t puncture_pattern_3[8];

void conv_encode_stream_frame(uint8_t* out, const uint8_t* in, const uint16_t fn);
void conv_encode_packet_frame(uint8_t* out, const uint8_t* in);
void conv_encode_LSF(uint8_t* out, const struct LSF *in);

#ifdef __cplusplus
}
#endif
