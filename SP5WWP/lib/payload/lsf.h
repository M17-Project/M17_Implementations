//-------------------------------
// M17 C library - payload/lsf.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 28 December 2023
//-------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure holding Link Setup Frame data.
 *
 */
struct LSF
{
	uint8_t dst[6];
	uint8_t src[6];
	uint8_t type[2];
	uint8_t meta[112/8];
	uint8_t crc[2];
};

#ifdef __cplusplus
}
#endif
