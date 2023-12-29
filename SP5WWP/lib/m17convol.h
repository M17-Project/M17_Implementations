//--------------------------------------------------------------------
// M17 C library - m17convol.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#ifndef M17_CONVOL_LIB
#define M17_CONVOL_LIB

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include "m17consts.h"

//vars
;

//functions
void conv_encode_stream_frame(uint8_t* out, const uint8_t* in, const uint16_t fn);
void conv_encode_packet_frame(uint8_t* out, const uint8_t* in);
void conv_encode_LSF(uint8_t* out, const struct LSF *in);

#ifdef __cplusplus
}
#endif
#endif