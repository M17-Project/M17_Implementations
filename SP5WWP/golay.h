#ifndef GOLAY_H
#define GOLAY_H

void IntToSoft(uint16_t* out, const uint16_t in, uint8_t len);
uint16_t SoftToInt(const uint16_t* in, uint8_t len);
uint16_t Div16(uint16_t a, uint16_t b);
uint16_t Mul16(uint16_t a, uint16_t b);
uint16_t SoftBitXOR(const uint16_t a, const uint16_t b);
void SoftXOR(uint16_t* out, const uint16_t* a, const uint16_t* b, uint8_t len);
uint32_t spopcount(const uint16_t* in, uint8_t siz);
void calcChecksumS(uint16_t* out, const uint16_t* value);
uint32_t SdetectErrors(const uint16_t* codeword);
uint16_t golay24_sdecode(const uint16_t* codeword);

#endif
