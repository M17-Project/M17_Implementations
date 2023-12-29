//--------------------------------------------------------------------
// M17 C library - m17crc.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#ifndef M17_CRC_LIB
#define M17_CRC_LIB

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include "m17consts.h"

//vars
;

//functions
uint16_t CRC_M17(const uint8_t *in, const uint16_t len);
uint16_t LSF_CRC(const struct LSF *in);

#ifdef __cplusplus
}
#endif
#endif

