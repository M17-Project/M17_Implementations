#include <string.h>
#include <stdint.h>
#include "golay.h"

static const uint16_t encode_matrix[12]=
{
    0x8eb, 0x93e, 0xa97, 0xdc6, 0x367, 0x6cd,
    0xd99, 0x3da, 0x7b4, 0xf68, 0x63b, 0xc75
};

uint32_t golay24_encode(const uint16_t data)
{
    uint16_t checksum=0;

    for(uint8_t i=0; i<12; i++)
    {
        if(data&(1<<i))
        {
            checksum ^= encode_matrix[i];
        }
    }

    return (data<<12) | checksum;
}
