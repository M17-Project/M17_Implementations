//-------------------------------
// M17 C library - encode/symbols.h
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 5 January 2024
//-------------------------------
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

//dibits-symbols map (TX)
extern const int8_t symbol_map[4];

//End of Transmission symbol pattern
extern const float eot_symbols[8];

#ifdef __cplusplus
}
#endif
