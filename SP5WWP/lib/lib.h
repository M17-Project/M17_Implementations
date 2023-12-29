//-------------------------------
// M17 C library - lib.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 28 December 2023
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

void send_preamble(const uint8_t type);
void send_syncword(const uint16_t syncword);
void send_data(const uint8_t* in);
void send_eot(void);

#ifdef __cplusplus
}
#endif
