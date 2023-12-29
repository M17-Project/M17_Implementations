//-------------------------------
// M17 C library - math/rrc.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 28 December 2023
//-------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//sample RRC filter for 48kHz sample rate
//alpha=0.5, span=8, sps=10, gain=sqrt(sps)
extern const float rrc_taps_10[8*10+1];

//sample RRC filter for 24kHz sample rate
//alpha=0.5, span=8, sps=5, gain=sqrt(sps)
extern const float rrc_taps_5[8*5+1];

#ifdef __cplusplus
}
#endif
