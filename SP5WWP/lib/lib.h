//-------------------------------
// M17 C library - lib.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 5 January 2024
//-------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#define BSB_SPS             10                      //samples per symbol
#define FLT_SPAN            8                       //baseband RRC filter span in symbols
#define SYM_PER_SWD         8                       //symbols per syncword
#define SW_LEN              (BSB_SPS*SYM_PER_SWD)   //syncword detector length
#define SYM_PER_PLD         184                     //symbols per payload in a frame
#define SYM_PER_FRA         192                     //symbols per whole 40 ms frame
#define RRC_DEV             7168.0f                 //.rrc file deviation for +1.0 symbol

//L2 metric threshold
#define DIST_THRESH         2.0f                    //threshold for distance (syncword detection)

void send_preamble(float out[SYM_PER_FRA], uint32_t *cnt, const uint8_t type);
void send_syncword(float out[SYM_PER_SWD], uint32_t *cnt, const uint16_t syncword);
void send_data(float out[SYM_PER_PLD], uint32_t *cnt, const uint8_t* in);
void send_eot(float out[SYM_PER_FRA], uint32_t *cnt);

#ifdef __cplusplus
}
#endif
