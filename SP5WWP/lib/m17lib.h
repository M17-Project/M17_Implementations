//-------------------------------
// M17 C library - m17lib.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 28 December 2023
//-------------------------------
#ifndef M17_LIB
#define M17_LIB

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

//functions
void send_preamble(const uint8_t type);
void send_syncword(const uint16_t syncword);
void send_data(const uint8_t* in);
void send_eot(void);

#ifdef __cplusplus
}
#endif
#endif
