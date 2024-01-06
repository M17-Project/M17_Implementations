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

#define CHAR_MAP " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."

void decode_callsign_bytes(uint8_t *outp, const uint8_t inp[6]);
void decode_callsign_value(uint8_t *outp, const uint64_t inp);
int8_t encode_callsign_bytes(uint8_t out[6], const uint8_t *inp);
int8_t encode_callsign_value(uint64_t *out, const uint8_t *inp);

#ifdef __cplusplus
}
#endif
