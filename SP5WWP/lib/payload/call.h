//--------------------------------------------------------------------
// M17 C library - payload/call.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

void decode_callsign_bytes(uint8_t *outp, const uint8_t *inp);
void decode_callsign_value(uint8_t *outp, const uint64_t inp);
uint8_t encode_callsign(uint64_t* out, const uint8_t* inp);

#ifdef __cplusplus
}
#endif
