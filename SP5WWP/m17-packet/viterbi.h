#ifndef VITERBI_H
#define VITERBI_H

#ifdef __cplusplus
extern "C" {
#endif
uint32_t decode(uint8_t* out, const uint16_t* in, uint16_t len);
uint32_t decodePunctured(uint8_t* out, const uint16_t* in, const uint8_t* punct, const uint16_t in_len, const uint16_t p_len);
void decodeBit(uint16_t s0, uint16_t s1, size_t pos);
uint32_t chainback(uint8_t* out, size_t pos, uint16_t len);
uint16_t q_AbsDiff(const uint16_t v1, const uint16_t v2);
void reset(void);
#ifdef __cplusplus
}
#endif

#endif
