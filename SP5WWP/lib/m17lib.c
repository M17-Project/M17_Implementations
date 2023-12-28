//-------------------------------
// M17 C library - m17lib.c
//
// Wojciech Kaczmarski, SP5WWP
// M17 Project, 28 December 2023
//-------------------------------
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "m17lib.h"

//syncword patterns (RX) TODO:Compute those at runtime from the consts below
const int8_t lsf_sync[8]={+3, +3, +3, +3, -3, -3, +3, -3};
const int8_t str_sync[8]={-3, -3, -3, -3, +3, +3, -3, +3};
const int8_t pkt_sync[8]={+3, -3, +3, +3, -3, -3, -3, -3};

//symbol levels (RX)
const float symbs[4]={-3.0, -1.0, +1.0, +3.0};

//dibits-symbols map (TX)
const int8_t symbol_map[4]={+1, +3, -1, -3};

//syncwords
const uint16_t SYNC_LSF = 0x55F7;
const uint16_t SYNC_STR = 0xFF5D;
const uint16_t SYNC_PKT = 0x75FF;
const uint16_t SYNC_BER = 0xDF55;
const uint16_t EOT_MRKR = 0x555D;

//puncturing pattern P_1
const uint8_t P_1[61]={1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,
                         1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,
                         1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,
                         1,0,1,1,1,0,1,1,1,0,1,1};

//puncturing pattern P_2
const uint8_t P_2[12]={1,1,1,1,1,1,1,1,1,1,1,0};

//puncturing pattern P_3
const uint8_t P_3[8]={1,1,1,1,1,1,1,0};

//M17 CRC polynomial
const uint16_t M17_CRC_POLY = 0x5935;

//sample RRC filter for 48kHz sample rate
//alpha=0.5, span=8, sps=10, gain=sqrt(sps)
const float taps_10[8*10+1]=
{
	    -0.003195702904062073f, -0.002930279157647190f, -0.001940667871554463f,
	    -0.000356087678023658f,  0.001547011339077758f,  0.003389554791179751f,
	     0.004761898604225673f,  0.005310860846138910f,  0.004824746306020221f,
	     0.003297923526848786f,  0.000958710871218619f, -0.001749908029791816f,
	    -0.004238694106631223f, -0.005881783042101693f, -0.006150256456781309f,
	    -0.004745376707651645f, -0.001704189656473565f,  0.002547854551539951f,
	     0.007215575568844704f,  0.011231038205363532f,  0.013421952197060707f,
	     0.012730475385624438f,  0.008449554307303753f,  0.000436744366018287f,
	    -0.010735380379191660f, -0.023726883538258272f, -0.036498030780605324f,
	    -0.046500883189991064f, -0.050979050575999614f, -0.047340680079891187f,
	    -0.033554880492651755f, -0.008513823955725943f,  0.027696543159614194f,
	     0.073664520037517042f,  0.126689053778116234f,  0.182990955139333916f,
	     0.238080025892859704f,  0.287235637987091563f,  0.326040247765297220f,
	     0.350895727088112619f,  0.359452932027607974f,  0.350895727088112619f,
	     0.326040247765297220f,  0.287235637987091563f,  0.238080025892859704f,
	     0.182990955139333916f,  0.126689053778116234f,  0.073664520037517042f,
	     0.027696543159614194f, -0.008513823955725943f, -0.033554880492651755f,
	    -0.047340680079891187f, -0.050979050575999614f, -0.046500883189991064f,
	    -0.036498030780605324f, -0.023726883538258272f, -0.010735380379191660f,
	     0.000436744366018287f,  0.008449554307303753f,  0.012730475385624438f,
	     0.013421952197060707f,  0.011231038205363532f,  0.007215575568844704f,
	     0.002547854551539951f, -0.001704189656473565f, -0.004745376707651645f,
	    -0.006150256456781309f, -0.005881783042101693f, -0.004238694106631223f,
	    -0.001749908029791816f,  0.000958710871218619f,  0.003297923526848786f,
	     0.004824746306020221f,  0.005310860846138910f,  0.004761898604225673f,
	     0.003389554791179751f,  0.001547011339077758f, -0.000356087678023658f,
	    -0.001940667871554463f, -0.002930279157647190f, -0.003195702904062073f
};

//sample RRC filter for 24kHz sample rate
//alpha=0.5, span=8, sps=5, gain=sqrt(sps)
const float taps_5[8*5+1]=
{
	-0.004519384154389f, -0.002744505321971f,
	 0.002187793653660f,  0.006734308458208f,
	 0.006823188093192f,  0.001355815246317f,
	-0.005994389201970f, -0.008697733303330f,
	-0.002410076268276f,  0.010204314627992f,
	 0.018981413448435f,  0.011949415510291f,
	-0.015182045838927f, -0.051615756197679f,
	-0.072094910038768f, -0.047453533621088f,
	 0.039168634270669f,  0.179164496628150f,
	 0.336694345124862f,  0.461088271869920f,
	 0.508340710642860f,  0.461088271869920f,
	 0.336694345124862f,  0.179164496628150f,
	 0.039168634270669f, -0.047453533621088f,
	-0.072094910038768f, -0.051615756197679f,
	-0.015182045838927f,  0.011949415510291f,
	 0.018981413448435f,  0.010204314627992f,
	-0.002410076268276f, -0.008697733303330f,
	-0.005994389201970f,  0.001355815246317f,
	 0.006823188093192f,  0.006734308458208f,
	 0.002187793653660f, -0.002744505321971f,
	-0.004519384154389f
};

//Golay coding
const uint16_t encode_matrix[12]=
{
    0x8eb, 0x93e, 0xa97, 0xdc6, 0x367, 0x6cd,
    0xd99, 0x3da, 0x7b4, 0xf68, 0x63b, 0xc75
};

const uint16_t decode_matrix[12]=
{
    0xc75, 0x49f, 0x93e, 0x6e3, 0xdc6, 0xf13,
    0xab9, 0x1ed, 0x3da, 0x7b4, 0xf68, 0xa4f
};

//randomizing pattern
const uint8_t rand_seq[46]=
{
    0xD6, 0xB5, 0xE2, 0x30, 0x82, 0xFF, 0x84, 0x62, 0xBA, 0x4E, 0x96, 0x90, 0xD8, 0x98, 0xDD, 0x5D, 0x0C, 0xC8, 0x52, 0x43, 0x91, 0x1D, 0xF8,
    0x6E, 0x68, 0x2F, 0x35, 0xDA, 0x14, 0xEA, 0xCD, 0x76, 0x19, 0x8D, 0xD5, 0x80, 0xD1, 0x33, 0x87, 0x13, 0x57, 0x18, 0x2D, 0x29, 0x78, 0xC3
};

//interleaver pattern
const uint16_t intrl_seq[368]=
{
	0, 137, 90, 227, 180, 317, 270, 39, 360, 129, 82, 219, 172, 309, 262, 31,
	352, 121, 74, 211, 164, 301, 254, 23, 344, 113, 66, 203, 156, 293, 246, 15,
	336, 105, 58, 195, 148, 285, 238, 7, 328, 97, 50, 187, 140, 277, 230, 367,
	320, 89, 42, 179, 132, 269, 222, 359, 312, 81, 34, 171, 124, 261, 214, 351,
	304, 73, 26, 163, 116, 253, 206, 343, 296, 65, 18, 155, 108, 245, 198, 335,
	288, 57, 10, 147, 100, 237, 190, 327, 280, 49, 2, 139, 92, 229, 182, 319,
	272, 41, 362, 131, 84, 221, 174, 311, 264, 33, 354, 123, 76, 213, 166, 303,
	256, 25, 346, 115, 68, 205, 158, 295, 248, 17, 338, 107, 60, 197, 150, 287,
	240, 9, 330, 99, 52, 189, 142, 279, 232, 1, 322, 91, 44, 181, 134, 271,
	224, 361, 314, 83, 36, 173, 126, 263, 216, 353, 306, 75, 28, 165, 118, 255,
	208, 345, 298, 67, 20, 157, 110, 247, 200, 337, 290, 59, 12, 149, 102, 239,
	192, 329, 282, 51, 4, 141, 94, 231, 184, 321, 274, 43, 364, 133, 86, 223,
	176, 313, 266, 35, 356, 125, 78, 215, 168, 305, 258, 27, 348, 117, 70, 207,
	160, 297, 250, 19, 340, 109, 62, 199, 152, 289, 242, 11, 332, 101, 54, 191,
	144, 281, 234, 3, 324, 93, 46, 183, 136, 273, 226, 363, 316, 85, 38, 175,
	128, 265, 218, 355, 308, 77, 30, 167, 120, 257, 210, 347, 300, 69, 22, 159,
	112, 249, 202, 339, 292, 61, 14, 151, 104, 241, 194, 331, 284, 53, 6, 143,
	96, 233, 186, 323, 276, 45, 366, 135, 88, 225, 178, 315, 268, 37, 358, 127,
	80, 217, 170, 307, 260, 29, 350, 119, 72, 209, 162, 299, 252, 21, 342, 111,
	64, 201, 154, 291, 244, 13, 334, 103, 56, 193, 146, 283, 236, 5, 326, 95,
	48, 185, 138, 275, 228, 365, 318, 87, 40, 177, 130, 267, 220, 357, 310, 79,
	32, 169, 122, 259, 212, 349, 302, 71, 24, 161, 114, 251, 204, 341, 294, 63,
	16, 153, 106, 243, 196, 333, 286, 55, 8, 145, 98, 235, 188, 325, 278, 47
};

//Viterbi vars
uint32_t prevMetrics[NUM_STATES];
uint32_t currMetrics[NUM_STATES];
uint32_t prevMetricsData[NUM_STATES];
uint32_t currMetricsData[NUM_STATES];
uint16_t viterbi_history[244];

//CRC
uint16_t CRC_M17(const uint8_t *in, const uint16_t len)
{
	uint32_t crc=0xFFFF; //init val

	for(uint16_t i=0; i<len; i++)
	{
		crc^=in[i]<<8;
		for(uint8_t j=0; j<8; j++)
		{
			crc<<=1;
			if(crc&0x10000)
				crc=(crc^M17_CRC_POLY)&0xFFFF;
		}
	}

	return crc&(0xFFFF);
}

//Golay coding
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

//0 index - LSB
void int_to_soft(uint16_t* out, const uint16_t in, uint8_t len)
{
	for(uint8_t i=0; i<len; i++)
	{
		(in>>i)&1 ? (out[i]=0xFFFF) : (out[i]=0);
	}
}

uint16_t soft_to_int(const uint16_t* in, uint8_t len)
{
	uint16_t tmp=0;

	for(uint8_t i=0; i<len; i++)
	{
		if(in[i]>0x7FFF)
			tmp|=(1<<i);
	}

	return tmp;
}

//Quadrant I fixed point division with saturation
//result=a/b
uint16_t div16(uint16_t a, uint16_t b)
{
	uint32_t aa=(uint32_t)a<<16;
	uint32_t r=aa/b;

	if(r<=0xFFFF)
		return r;
	else
		return 0xFFFF;
}

//Quadrant I fixed point multiplication
//result=a/b
uint16_t mul16(uint16_t a, uint16_t b)
{
	return (uint16_t)(((uint32_t)a*b)>>16);
}

//use bilinear interpolation for XOR
uint16_t soft_bit_XOR(const uint16_t a, const uint16_t b)
{
	return mul16(div16(0xFFFF-b, 0xFFFF), div16(a, 0xFFFF)) + mul16(div16(b, 0xFFFF), div16(0xFFFF-a, 0xFFFF));
}

//soft XOR
void soft_XOR(uint16_t* out, const uint16_t* a, const uint16_t* b, uint8_t len)
{
	for(uint8_t i=0; i<len; i++)
		out[i]=soft_bit_XOR(a[i], b[i]);
}

//soft equivalent of popcount
uint32_t s_popcount(const uint16_t* in, uint8_t siz)
{
	uint32_t tmp=0;

	for(uint8_t i=0; i<siz; i++)
		tmp+=in[i];

	return tmp;
}

void s_calc_checksum(uint16_t* out, const uint16_t* value)
{
    uint16_t checksum[12];
    uint16_t soft_em[12]; //soft valued encoded matrix entry

    for(uint8_t i=0; i<12; i++)
    	checksum[i]=0;

    for(uint8_t i=0; i<12; i++)
    {
    	int_to_soft(soft_em, encode_matrix[i], 12);

        if(value[i]>0x7FFF)
        {
            soft_XOR(checksum, checksum, soft_em, 12);
        }
    }

    memcpy((uint8_t*)out, (uint8_t*)checksum, 12*2);
}

uint32_t s_detect_errors(const uint16_t* codeword)
{
    uint16_t data[12];
    uint16_t parity[12];
    uint16_t cksum[12];
    uint16_t syndrome[12];
    uint32_t weight; //for soft popcount

	memcpy((uint8_t*)data, (uint8_t*)&codeword[12], 2*12);
	memcpy((uint8_t*)parity, (uint8_t*)&codeword[0], 2*12);

	s_calc_checksum(cksum, data);
	soft_XOR(syndrome, parity, cksum, 12);

	weight=s_popcount(syndrome, 12);

	//all (less than 4) errors in the parity part
    if(weight < 4*0xFFFE)
    {
    	//printf("1: %1.2f\n", (float)weight/0xFFFF);
        return soft_to_int(syndrome, 12);
    }

    //one of the errors in data part, up to 3 in parity
    for(uint8_t i = 0; i<12; i++)
    {
        uint16_t e = 1<<i;
        uint16_t coded_error = encode_matrix[i];
        uint16_t scoded_error[12]; //soft coded_error
        uint16_t sc[12]; //syndrome^coded_error

        int_to_soft(scoded_error, coded_error, 12);
        soft_XOR(sc, syndrome, scoded_error, 12);
        weight=s_popcount(sc, 12);

        if(weight < 3*0xFFFE)
        {
        	//printf("2: %1.2f\n", (float)weight/0xFFFF+1);
        	uint16_t s=soft_to_int(syndrome, 12);
            return (e << 12) | (s ^ coded_error);
        }
    }

    //two of the errors in data part and up to 2 in parity
    for(uint8_t i = 0; i<11; i++)
    {
    	for(uint8_t j = i+1; j<12; j++)
    	{
    		uint16_t e = (1<<i) | (1<<j);
        	uint16_t coded_error = encode_matrix[i]^encode_matrix[j];
        	uint16_t scoded_error[12]; //soft coded_error
	        uint16_t sc[12]; //syndrome^coded_error

	        int_to_soft(scoded_error, coded_error, 12);
	        soft_XOR(sc, syndrome, scoded_error, 12);
	        weight=s_popcount(sc, 12);

	        if(weight < 2*0xFFFF)
	        {
	        	//printf("3: %1.2f\n", (float)weight/0xFFFF+2);
	        	uint16_t s=soft_to_int(syndrome, 12);
	            return (e << 12) | (s ^ coded_error);
	        }
		}
    }

	//algebraic decoding magic
    uint16_t inv_syndrome[12]={0,0,0,0,0,0,0,0,0,0,0,0};
    uint16_t dm[12]; //soft decode matrix

    for(uint8_t i=0; i<12; i++)
    {
        if(syndrome[i] > 0x7FFF)
        {
        	int_to_soft(dm, decode_matrix[i], 12);
        	soft_XOR(inv_syndrome, inv_syndrome, dm, 12);
        }
    }

	//all (less than 4) errors in the data part
	weight=s_popcount(inv_syndrome, 12);
    if(weight < 4*0xFFFF)
    {
    	//printf("4: %1.2f\n", (float)weight/0xFFFF);
        return soft_to_int(inv_syndrome, 12) << 12;
    }

	//one error in parity bits, up to 3 in data - this part has some quirks, the reason remains unknown
    for(uint8_t i=0; i<12; i++)
    {
        uint16_t e = 1<<i;
        uint16_t coding_error = decode_matrix[i];

        uint16_t ce[12]; //soft coding error
        uint16_t tmp[12];

        int_to_soft(ce, coding_error, 12);
        soft_XOR(tmp, inv_syndrome, ce, 12);
        weight=s_popcount(tmp, 12);

        if(weight < 3*(0xFFFF+2))
        {
        	//printf("5: %1.2f\n", (float)weight/0xFFFF+1);
            return ((soft_to_int(inv_syndrome, 12) ^ coding_error) << 12) | e;
        }
    }

    return 0xFFFFFFFF;
}

//soft decode
uint16_t golay24_sdecode(const uint16_t* codeword)
{
    //match the bit order in M17
    uint16_t cw[24];
    for(uint8_t i=0; i<24; i++)
        cw[i]=codeword[23-i];

    uint32_t errors = s_detect_errors(cw);

    if(errors == 0xFFFFFFFF)
		return 0xFFFF;

    return (((soft_to_int(&cw[0], 16) | (soft_to_int(&cw[16], 8) << 16)) ^ errors) >> 12) & 0x0FFF;
}

//Viterbi
/**
 * Decode unpunctured convolutionally encoded data.
 *
 * @param out: destination array where decoded data is written.
 * @param in: input data.
 * @param len: input length in bits
 * @return number of bit errors corrected.
 */
uint32_t viterbi_decode(uint8_t* out, const uint16_t* in, uint16_t len)
{
    if(len > 244*2)
		fprintf(stderr, "Input size exceeds max history\n");

    viterbi_reset();

    size_t pos = 0;
    for(size_t i = 0; i < len; i += 2)
    {
        uint16_t s0 = in[i];
        uint16_t s1 = in[i + 1];

        viterbi_decode_bit(s0, s1, pos);
        pos++;
    }

    return viterbi_chainback(out, pos, len/2);
}

/**
 * Decode punctured convolutionally encoded data.
 *
 * @param out: destination array where decoded data is written.
 * @param in: input data.
 * @param punct: puncturing matrix.
 * @param in_len: input data length.
 * @param p_len: puncturing matrix length (entries).
 * @return number of bit errors corrected.
 */
uint32_t viterbi_decode_punctured(uint8_t* out, const uint16_t* in, const uint8_t* punct, const uint16_t in_len, const uint16_t p_len)
{
    if(in_len > 244*2)
		fprintf(stderr, "Input size exceeds max history\n");

	uint16_t umsg[244*2];           //unpunctured message
	uint8_t p=0;		            //puncturer matrix entry
	uint16_t u=0;		            //bits count - unpunctured message
    uint16_t i=0;                   //bits read from the input message

	while(i<in_len)
	{
		if(punct[p])
		{
			umsg[u]=in[i];
			i++;
		}
		else
		{
			umsg[u]=0x7FFF;
		}

		u++;
		p++;
		p%=p_len;
	}

    return viterbi_decode(out, umsg, u) - (u-in_len)*0x7FFF;
}

/**
 * Decode one bit and update trellis.
 *
 * @param s0: cost of the first symbol.
 * @param s1: cost of the second symbol.
 * @param pos: bit position in history.
 */
void viterbi_decode_bit(uint16_t s0, uint16_t s1, size_t pos)
{
    static const uint16_t COST_TABLE_0[] = {0, 0, 0, 0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    static const uint16_t COST_TABLE_1[] = {0, 0xFFFF, 0xFFFF, 0, 0, 0xFFFF, 0xFFFF, 0};

    for(uint8_t i = 0; i < NUM_STATES/2; i++)
    {
        uint32_t metric = q_abs_diff(COST_TABLE_0[i], s0)
                        + q_abs_diff(COST_TABLE_1[i], s1);


        uint32_t m0 = prevMetrics[i] + metric;
        uint32_t m1 = prevMetrics[i + NUM_STATES/2] + (0x1FFFE - metric);

        uint32_t m2 = prevMetrics[i] + (0x1FFFE - metric);
        uint32_t m3 = prevMetrics[i + NUM_STATES/2] + metric;

        uint8_t i0 = 2 * i;
        uint8_t i1 = i0 + 1;

        if(m0 >= m1)
        {
            viterbi_history[pos]|=(1<<i0);
            currMetrics[i0] = m1;
        }
        else
        {
            viterbi_history[pos]&=~(1<<i0);
            currMetrics[i0] = m0;
        }

        if(m2 >= m3)
        {
            viterbi_history[pos]|=(1<<i1);
            currMetrics[i1] = m3;
        }
        else
        {
            viterbi_history[pos]&=~(1<<i1);
            currMetrics[i1] = m2;
        }
    }

    //swap
    uint32_t tmp[NUM_STATES];
    for(uint8_t i=0; i<NUM_STATES; i++)
    {
    	tmp[i]=currMetrics[i];
	}
	for(uint8_t i=0; i<NUM_STATES; i++)
    {
    	currMetrics[i]=prevMetrics[i];
    	prevMetrics[i]=tmp[i];
	}
}

/**
 * History chainback to obtain final byte array.
 *
 * @param out: destination byte array for decoded data.
 * @param pos: starting position for the chainback.
 * @param len: length of the output in bits.
 * @return minimum Viterbi cost at the end of the decode sequence.
 */
uint32_t viterbi_chainback(uint8_t* out, size_t pos, uint16_t len)
{
    uint8_t state = 0;
    size_t bitPos = len+4;

    memset(out, 0, (len-1)/8+1);

    while(pos > 0)
    {
        bitPos--;
        pos--;
        uint16_t bit = viterbi_history[pos]&((1<<(state>>4)));
        state >>= 1;
        if(bit)
        {
        	state |= 0x80;
        	out[bitPos/8]|=1<<(7-(bitPos%8));
		}
    }

    uint32_t cost = prevMetrics[0];

    for(size_t i = 0; i < NUM_STATES; i++)
    {
        uint32_t m = prevMetrics[i];
        if(m < cost) cost = m;
    }

    return cost;
}

/**
 * Utility function to compute the absolute value of a difference between
 * two fixed-point values.
 *
 * @param v1: first value
 * @param v2: second value
 * @return abs(v1-v2)
 */
uint16_t q_abs_diff(const uint16_t v1, const uint16_t v2)
{
    if(v2 > v1) return v2 - v1;
    return v1 - v2;
}

/**
 * Reset the decoder state.
 *
 */
void viterbi_reset(void)
{
	memset((uint8_t*)viterbi_history, 0, 2*244);
	memset((uint8_t*)currMetrics, 0, 4*NUM_STATES);
    memset((uint8_t*)prevMetrics, 0, 4*NUM_STATES);
    memset((uint8_t*)currMetricsData, 0, 4*NUM_STATES);
    memset((uint8_t*)prevMetricsData, 0, 4*NUM_STATES);
}

//M17
void send_preamble(const uint8_t type)
{
    float symb;

    if(type) //pre-BERT
    {
        for(uint16_t i=0; i<192/2; i++) //40ms * 4800 = 192
        {
            symb=-3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
            symb=+3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
        }
    }
    else //pre-LSF
    {
        for(uint16_t i=0; i<192/2; i++) //40ms * 4800 = 192
        {
            symb=+3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
            symb=-3.0;
            fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
        }
    }
}

void send_syncword(const uint16_t syncword)
{
    float symb;

    for(uint8_t i=0; i<16; i+=2)
    {
        symb=symbol_map[(syncword>>(14-i))&3];
        fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
    }
}

//send the data (can be used for both LSF and frames)
void send_data(const uint8_t* in)
{
	float s=0.0;
	for(uint16_t i=0; i<SYM_PER_PLD; i++) //40ms * 4800 - 8 (syncword)
	{
		s=symbol_map[in[2*i]*2+in[2*i+1]];
		fwrite((uint8_t*)&s, sizeof(float), 1, stdout);
	}
}

void send_eot(void)
{
    float symb=+3.0;
    for(uint16_t i=0; i<192; i++) //40ms * 4800 = 192
    {
        fwrite((uint8_t*)&symb, sizeof(float), 1, stdout);
    }
}

//out - unpacked bits
//in - packed raw bits
//fn - frame number
void conv_encode_stream_frame(uint8_t* out, const uint8_t* in, const uint16_t fn)
{
	uint8_t pp_len = sizeof(P_2);
	uint8_t p=0;			//puncturing pattern index
	uint16_t pb=0;			//pushed punctured bits
	uint8_t ud[144+4+4];	//unpacked data

	memset(ud, 0, 144+4+4);

	//unpack frame number
	for(uint8_t i=0; i<16; i++)
	{
		ud[4+i]=(fn>>(15-i))&1;
	}

	//unpack data
	for(uint8_t i=0; i<16; i++)
	{
		for(uint8_t j=0; j<8; j++)
		{
			ud[4+16+i*8+j]=(in[i]>>(7-j))&1;
		}
	}

	//encode
	for(uint8_t i=0; i<144+4; i++)
	{
		uint8_t G1=(ud[i+4]                +ud[i+1]+ud[i+0])%2;
        uint8_t G2=(ud[i+4]+ud[i+3]+ud[i+2]        +ud[i+0])%2;

		//printf("%d%d", G1, G2);

		if(P_2[p])
		{
			out[pb]=G1;
			pb++;
		}

		p++;
		p%=pp_len;

		if(P_2[p])
		{
			out[pb]=G2;
			pb++;
		}

		p++;
		p%=pp_len;
	}

	//printf("pb=%d\n", pb);
}

//out - unpacked bits
//in - packed raw bits
void conv_encode_packet_frame(uint8_t* out, uint8_t* in)
{
	uint8_t pp_len = sizeof(P_3);
	uint8_t p=0;			//puncturing pattern index
	uint16_t pb=0;			//pushed punctured bits
	uint8_t ud[206+4+4];	//unpacked data

	memset(ud, 0, 206+4+4);

	//unpack data
	for(uint8_t i=0; i<26; i++)
	{
		for(uint8_t j=0; j<8; j++)
		{
            if(i<=24 || j<=5)
                ud[4+i*8+j]=(in[i]>>(7-j))&1;
		}
	}

	//encode
	for(uint8_t i=0; i<206+4; i++)
	{
		uint8_t G1=(ud[i+4]                +ud[i+1]+ud[i+0])%2;
        uint8_t G2=(ud[i+4]+ud[i+3]+ud[i+2]        +ud[i+0])%2;

		//fprintf(stderr, "%d%d", G1, G2);

		if(P_3[p])
		{
			out[pb]=G1;
			pb++;
		}

		p++;
		p%=pp_len;

		if(P_3[p])
		{
			out[pb]=G2;
			pb++;
		}

		p++;
		p%=pp_len;
	}

	//fprintf(stderr, "pb=%d\n", pb);
}

//out - unpacked bits
//in - packed raw bits (LSF struct)
void conv_encode_LSF(uint8_t* out, const struct LSF *in)
{
	uint8_t pp_len = sizeof(P_1);
	uint8_t p=0;			//puncturing pattern index
	uint16_t pb=0;			//pushed punctured bits
	uint8_t ud[240+4+4];	//unpacked data

	memset(ud, 0, 240+4+4);

	//unpack DST
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i]   =((in->dst[0])>>(7-i))&1;
		ud[4+i+8] =((in->dst[1])>>(7-i))&1;
		ud[4+i+16]=((in->dst[2])>>(7-i))&1;
		ud[4+i+24]=((in->dst[3])>>(7-i))&1;
		ud[4+i+32]=((in->dst[4])>>(7-i))&1;
		ud[4+i+40]=((in->dst[5])>>(7-i))&1;
	}

	//unpack SRC
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+48]=((in->src[0])>>(7-i))&1;
		ud[4+i+56]=((in->src[1])>>(7-i))&1;
		ud[4+i+64]=((in->src[2])>>(7-i))&1;
		ud[4+i+72]=((in->src[3])>>(7-i))&1;
		ud[4+i+80]=((in->src[4])>>(7-i))&1;
		ud[4+i+88]=((in->src[5])>>(7-i))&1;
	}

	//unpack TYPE
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+96] =((in->type[0])>>(7-i))&1;
		ud[4+i+104]=((in->type[1])>>(7-i))&1;
	}

	//unpack META
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+112]=((in->meta[0])>>(7-i))&1;
		ud[4+i+120]=((in->meta[1])>>(7-i))&1;
		ud[4+i+128]=((in->meta[2])>>(7-i))&1;
		ud[4+i+136]=((in->meta[3])>>(7-i))&1;
		ud[4+i+144]=((in->meta[4])>>(7-i))&1;
		ud[4+i+152]=((in->meta[5])>>(7-i))&1;
		ud[4+i+160]=((in->meta[6])>>(7-i))&1;
		ud[4+i+168]=((in->meta[7])>>(7-i))&1;
		ud[4+i+176]=((in->meta[8])>>(7-i))&1;
		ud[4+i+184]=((in->meta[9])>>(7-i))&1;
		ud[4+i+192]=((in->meta[10])>>(7-i))&1;
		ud[4+i+200]=((in->meta[11])>>(7-i))&1;
		ud[4+i+208]=((in->meta[12])>>(7-i))&1;
		ud[4+i+216]=((in->meta[13])>>(7-i))&1;
	}

	//unpack CRC
	for(uint8_t i=0; i<8; i++)
	{
		ud[4+i+224]=((in->crc[0])>>(7-i))&1;
		ud[4+i+232]=((in->crc[1])>>(7-i))&1;
	}

	//encode
	for(uint8_t i=0; i<240+4; i++)
	{
		uint8_t G1=(ud[i+4]                +ud[i+1]+ud[i+0])%2;
        uint8_t G2=(ud[i+4]+ud[i+3]+ud[i+2]        +ud[i+0])%2;

		//printf("%d%d", G1, G2);

		if(P_1[p])
		{
			out[pb]=G1;
			pb++;
		}

		p++;
		p%=pp_len;

		if(P_1[p])
		{
			out[pb]=G2;
			pb++;
		}

		p++;
		p%=pp_len;
	}

	//printf("pb=%d\n", pb);
}

uint16_t LSF_CRC(const struct LSF *in)
{
    uint8_t d[28];

    memcpy(&d[0], in->dst, 6);
    memcpy(&d[6], in->src, 6);
    memcpy(&d[12], in->type, 2);
    memcpy(&d[14], in->meta, 14);

    return CRC_M17(d, 28);
}

//decodes a 6-byte long array to a callsign
void decode_callsign_bytes(uint8_t *outp, const uint8_t *inp)
{
	uint64_t encoded=0;

	//repack the data to a uint64_t
	for(uint8_t i=0; i<6; i++)
		encoded|=(uint64_t)inp[5-i]<<(8*i);

	//check if the value is reserved (not a callsign)
	if(encoded>=262144000000000ULL)
	{
        if(encoded==0xFFFFFFFFFFFF) //broadcast
        {
            sprintf((char*)outp, "#BCAST");
        }
        else
        {
            outp[0]=0;
        }

        return;
	}

	//decode the callsign
	uint8_t i=0;
	while(encoded>0)
	{
		outp[i]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."[encoded%40];
		encoded/=40;
		i++;
	}
	outp[i]=0;
}

//decodes a 48-bit value to a callsign
void decode_callsign_value(uint8_t *outp, const uint64_t inp)
{
    uint64_t encoded=inp;

	//check if the value is reserved (not a callsign)
	if(encoded>=262144000000000ULL)
	{
        if(encoded==0xFFFFFFFFFFFF) //broadcast
        {
            sprintf((char*)outp, "#BCAST");
        }
        else
        {
            outp[0]=0;
        }

        return;
	}

	//decode the callsign
	uint8_t i=0;
	while(encoded>0)
	{
		outp[i]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."[encoded%40];
		encoded/=40;
		i++;
	}
	outp[i]=0;
}

//encode callsign into a 48-bit value
uint8_t encode_callsign(uint64_t* out, const uint8_t* inp)
{
    //assert inp length
    if(strlen((const char*)inp)>9)
    {
        return -1;
    }

    const uint8_t charMap[40]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

    uint64_t tmp=0;

    if(strcmp((const char*)inp, "ALL")==0)
    {
        *out=0xFFFFFFFFFFFF;
        return 0;
    }

    for(int8_t i=strlen((const char*)inp)-1; i>=0; i--)
    {
        for(uint8_t j=0; j<40; j++)
        {
            if(inp[i]==charMap[j])
            {
                tmp=tmp*40+j;
                break;
            }
        }
    }

    *out=tmp;
    return 0;
}

//soft decodes LICH into a 6-byte array
//input - soft bits
//output - an array of packed bits
void decode_LICH(uint8_t* outp, const uint16_t* inp)
{
    uint16_t tmp;

    memset(outp, 0, 5);

    tmp=golay24_sdecode(&inp[0]);
    outp[0]=(tmp>>4)&0xFF;
    outp[1]|=(tmp&0xF)<<4;
    tmp=golay24_sdecode(&inp[1*24]);
    outp[1]|=(tmp>>8)&0xF;
    outp[2]=tmp&0xFF;
    tmp=golay24_sdecode(&inp[2*24]);
    outp[3]=(tmp>>4)&0xFF;
    outp[4]|=(tmp&0xF)<<4;
    tmp=golay24_sdecode(&inp[3*24]);
    outp[4]|=(tmp>>8)&0xF;
    outp[5]=tmp&0xFF;
}

//calculate L2 norm between two len-dimensional vectors of floats
float eucl_norm(const float* in1, const int8_t* in2, uint8_t len)
{
    float tmp = 0.0f;

    for(uint8_t i=0; i<len; i++)
    {
        tmp += powf(in1[i]-(float)in2[i], 2.0f);
    }

    return sqrt(tmp);
}
