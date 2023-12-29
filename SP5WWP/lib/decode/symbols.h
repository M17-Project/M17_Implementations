//-------------------------------
// M17 C library - decode/symbols.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 28 December 2023
//-------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

// syncword patterns (RX)
// TODO: Compute those at runtime from the consts below
extern const int8_t lsf_sync_symbols[8];
extern const int8_t str_sync_symbols[8];
extern const int8_t pkt_sync_symbols[8];

// symbol levels (RX)
extern const float symbol_levels[4];

#ifdef __cplusplus
}
#endif
