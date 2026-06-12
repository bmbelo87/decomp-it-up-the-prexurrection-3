#include "zlibinflate.h"
#include <string.h>

#define Z_OK          0
#define Z_STREAM_END  1
#define Z_NEED_DICT   2
#define Z_ERRNO      (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR  (-3)
#define Z_MEM_ERROR   (-4)
#define Z_BUF_ERROR   (-5)

#define HUFF_INVALID 0xFFFF

typedef struct {
    const uint8_t* in;
    uint32_t inPos;
    uint32_t inLen;
    uint8_t* out;
    uint32_t outPos;
    uint32_t outLen;
    uint32_t bitBuf;
    int bitCount;
} zlib_stream;

static int bits_read(zlib_stream* z, int n) {
    while (z->bitCount < n) {
        if (z->inPos >= z->inLen) return -1;
        z->bitBuf |= (uint32_t)z->in[z->inPos++] << z->bitCount;
        z->bitCount += 8;
    }
    int val = (int)(z->bitBuf & ((1u << n) - 1));
    z->bitBuf >>= n;
    z->bitCount -= n;
    return val;
}

static void bits_align(zlib_stream* z) {
    z->bitCount = 0;
    z->bitBuf = 0;
}

#define HUFF_TABLE_SIZE 2048

typedef struct {
    uint16_t symbol[HUFF_TABLE_SIZE];
    uint8_t  codeLen[HUFF_TABLE_SIZE];
    int maxLen;
} huff_table;

static int huff_build(huff_table* ht, const int* lens, int num) {
    int count[16] = {0};
    int maxLen = 0;
    for (int i = 0; i < num; i++)
        if (lens[i] > maxLen) maxLen = lens[i];
    if (maxLen > 15) maxLen = 15;
    ht->maxLen = maxLen;

    memset(ht->symbol, 0xFF, HUFF_TABLE_SIZE * sizeof(uint16_t));
    memset(ht->codeLen, 0, HUFF_TABLE_SIZE);

    for (int i = 0; i < num; i++)
        if (lens[i] > 0) count[lens[i]]++;

    uint16_t nextCode[16] = {0};
    uint16_t code = 0;
    for (int len = 1; len <= maxLen; len++) {
        code = (code + count[len - 1]) << 1;
        nextCode[len] = code;
    }

    for (int i = 0; i < num; i++) {
        if (lens[i] > 0) {
            int len = lens[i];
            uint16_t c = nextCode[len]++;
            if (c < HUFF_TABLE_SIZE) {
                ht->symbol[c] = (uint16_t)i;
                ht->codeLen[c] = (uint8_t)len;
            }
        }
    }
    return maxLen;
}

static int huff_decode(zlib_stream* z, huff_table* ht) {
    uint32_t code = 0;
    int maxBits = ht->maxLen;
    for (int len = 1; len <= maxBits; len++) {
        if (z->bitCount <= 0) {
            if (z->inPos >= z->inLen) return -1;
            z->bitBuf |= (uint32_t)z->in[z->inPos++] << z->bitCount;
            z->bitCount += 8;
        }
        code = (code << 1) | (z->bitBuf & 1);
        z->bitBuf >>= 1;
        z->bitCount--;
        if (code < HUFF_TABLE_SIZE &&
            ht->symbol[code] != HUFF_INVALID &&
            ht->codeLen[code] == len)
            return (int)ht->symbol[code];
    }
    return -1;
}

/* --- length/distance tables --- */
static const int LENGTH_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const int LENGTH_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const int DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const int DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* --- inflate stored block --- */
static int inflate_stored(zlib_stream* z) {
    bits_align(z);
    int len = bits_read(z, 16);
    if (len < 0) return Z_DATA_ERROR;
    int nlen = bits_read(z, 16);
    if (nlen < 0) return Z_DATA_ERROR;
    if ((uint16_t)(len ^ nlen) != 0xFFFF) return Z_DATA_ERROR;
    for (int i = 0; i < len; i++) {
        if (z->inPos >= z->inLen || z->outPos >= z->outLen) return Z_DATA_ERROR;
        z->out[z->outPos++] = z->in[z->inPos++];
    }
    return Z_OK;
}

/* --- inflate dynamic Huffman block --- */
static int inflate_dynamic(zlib_stream* z) {
    int hlit = bits_read(z, 5) + 257;
    int hdist = bits_read(z, 5) + 1;
    int hclen = bits_read(z, 4) + 4;
    if (hlit > 286 || hdist > 30 || hclen > 19) return Z_DATA_ERROR;

    static const int CL_ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int clLens[19] = {0};
    for (int i = 0; i < hclen; i++) {
        int v = bits_read(z, 3);
        if (v < 0) return Z_DATA_ERROR;
        clLens[CL_ORDER[i]] = v;
    }

    huff_table clHt;
    huff_build(&clHt, clLens, 19);

    int allLens[286 + 30];
    int allCount = 0;
    while (allCount < hlit + hdist) {
        int sym = huff_decode(z, &clHt);
        if (sym < 0) return Z_DATA_ERROR;
        if (sym < 16) {
            allLens[allCount++] = sym;
        } else if (sym == 16) {
            int rep = bits_read(z, 2) + 3;
            if (allCount == 0) return Z_DATA_ERROR;
            int val = allLens[allCount - 1];
            for (int i = 0; i < rep && allCount < hlit + hdist; i++)
                allLens[allCount++] = val;
        } else if (sym == 17) {
            int rep = bits_read(z, 3) + 3;
            for (int i = 0; i < rep && allCount < hlit + hdist; i++)
                allLens[allCount++] = 0;
        } else if (sym == 18) {
            int rep = bits_read(z, 7) + 11;
            for (int i = 0; i < rep && allCount < hlit + hdist; i++)
                allLens[allCount++] = 0;
        }
    }

    huff_table llHt, distHt;
    huff_build(&llHt, allLens, hlit);
    huff_build(&distHt, allLens + hlit, hdist);

    while (1) {
        int sym = huff_decode(z, &llHt);
        if (sym < 0) return Z_DATA_ERROR;
        if (sym < 256) {
            if (z->outPos >= z->outLen) return Z_DATA_ERROR;
            z->out[z->outPos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return Z_STREAM_END;
        } else {
            sym -= 257;
            if (sym >= 29) return Z_DATA_ERROR;
            int length = LENGTH_BASE[sym];
            int extra = LENGTH_EXTRA[sym];
            if (extra > 0) {
                int e = bits_read(z, extra);
                if (e < 0) return Z_DATA_ERROR;
                length += e;
            }
            int distSym = huff_decode(z, &distHt);
            if (distSym < 0) return Z_DATA_ERROR;
            int distance = DIST_BASE[distSym];
            int dextra = DIST_EXTRA[distSym];
            if (dextra > 0) {
                int e = bits_read(z, dextra);
                if (e < 0) return Z_DATA_ERROR;
                distance += e;
            }
            if (z->outPos < (uint32_t)distance) return Z_DATA_ERROR;
            for (int i = 0; i < length; i++) {
                if (z->outPos >= z->outLen) return Z_DATA_ERROR;
                z->out[z->outPos] = z->out[z->outPos - distance];
                z->outPos++;
            }
        }
    }
}

/* --- fixed Huffman tables --- */
static int FIXED_LL_LENS[288];
static int FIXED_DIST_LENS[32];
static huff_table g_fixedLL;
static huff_table g_fixedDist;
static int g_fixedHuffBuilt = 0;

static void build_fixed_tables(void) {
    if (g_fixedHuffBuilt) return;
    for (int i = 0; i < 144; i++) FIXED_LL_LENS[i] = 8;
    for (int i = 144; i < 256; i++) FIXED_LL_LENS[i] = 9;
    for (int i = 256; i < 280; i++) FIXED_LL_LENS[i] = 7;
    for (int i = 280; i < 288; i++) FIXED_LL_LENS[i] = 8;
    for (int i = 0; i < 32; i++) FIXED_DIST_LENS[i] = 5;
    huff_build(&g_fixedLL, FIXED_LL_LENS, 288);
    huff_build(&g_fixedDist, FIXED_DIST_LENS, 32);
    g_fixedHuffBuilt = 1;
}

static int inflate_fixed(zlib_stream* z) {
    build_fixed_tables();
    while (1) {
        int sym = huff_decode(z, &g_fixedLL);
        if (sym < 0) return Z_DATA_ERROR;
        if (sym < 256) {
            if (z->outPos >= z->outLen) return Z_DATA_ERROR;
            z->out[z->outPos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return Z_STREAM_END;
        } else {
            sym -= 257;
            if (sym >= 29) return Z_DATA_ERROR;
            int length = LENGTH_BASE[sym];
            int extra = LENGTH_EXTRA[sym];
            if (extra > 0) {
                int e = bits_read(z, extra);
                if (e < 0) return Z_DATA_ERROR;
                length += e;
            }
            int distSym = huff_decode(z, &g_fixedDist);
            if (distSym < 0) return Z_DATA_ERROR;
            int distance = DIST_BASE[distSym];
            int dextra = DIST_EXTRA[distSym];
            if (dextra > 0) {
                int e = bits_read(z, dextra);
                if (e < 0) return Z_DATA_ERROR;
                distance += e;
            }
            if (z->outPos < (uint32_t)distance) return Z_DATA_ERROR;
            if (z->outPos + length > z->outLen) return Z_DATA_ERROR;
            for (int i = 0; i < length; i++) {
                z->out[z->outPos] = z->out[z->outPos - distance];
                z->outPos++;
            }
        }
    }
}

static int inflate_block(zlib_stream* z) {
    int bfinal = bits_read(z, 1);
    int btype = bits_read(z, 2);
    if (btype < 0) return Z_DATA_ERROR;

    int result;
    switch (btype) {
    case 0: result = inflate_stored(z); break;
    case 1: result = inflate_fixed(z); break;
    case 2: result = inflate_dynamic(z); break;
    default: return Z_DATA_ERROR;
    }

    if (result == Z_STREAM_END) {
        if (bfinal) return Z_STREAM_END;
        return Z_OK;
    }
    return result;
}

static uint32_t adler32(const uint8_t* data, uint32_t len) {
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

int zlib_decompress_ex(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t* outLen, uint32_t* inConsumed) {
    if (inLen < 6) return Z_DATA_ERROR;
    if ((in[0] * 256u + in[1]) % 31 != 0) return Z_DATA_ERROR;
    if ((in[0] & 0x0F) != 8) return Z_DATA_ERROR;

    zlib_stream z;
    z.in = in + 2;
    z.inPos = 0;
    z.inLen = inLen - 6;
    z.out = out;
    z.outPos = 0;
    z.outLen = *outLen;
    z.bitBuf = 0;
    z.bitCount = 0;

    int result;
    do {
        result = inflate_block(&z);
    } while (result == Z_OK);

    if (result != Z_STREAM_END) return result;

    uint32_t storedAdler = ((uint32_t)z.in[z.inPos] << 24) |
                           ((uint32_t)z.in[z.inPos + 1] << 16) |
                           ((uint32_t)z.in[z.inPos + 2] << 8)  |
                           ((uint32_t)z.in[z.inPos + 3]);

    uint32_t calcAdler = adler32(out, z.outPos);
    if (calcAdler != storedAdler) return Z_DATA_ERROR;

    *outLen = z.outPos;
    if (inConsumed) *inConsumed = 2 + z.inPos + 4;
    return Z_OK;
}

int zlib_decompress(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t* outLen) {
    return zlib_decompress_ex(in, inLen, out, outLen, NULL);
}
