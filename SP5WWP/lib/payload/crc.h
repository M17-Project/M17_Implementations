//--------------------------------------------------------------------
// M17 C library - payload/crc.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 29 December 2023
//--------------------------------------------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include "lsf.h"

//M17 CRC polynomial
extern const uint16_t M17_CRC_POLY;

uint16_t CRC_M17(const uint8_t *in, const uint16_t len);
uint16_t LSF_CRC(const struct LSF *in);

#ifdef __cplusplus
}
#endif
