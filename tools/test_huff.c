#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// debug version with prints
#define Z_OK          0
#define Z_STREAM_END  1

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

typedef struct {
    uint16_t count[16];
    uint16_t symbol[288];
} huff_table;

static int huff_build(huff_table* ht, const int* lens, int num) {
    int maxLen = 0;
    for (int i = 0; i < num; i++)
        if (lens[i] > maxLen) maxLen = lens[i];
    if (maxLen > 15) maxLen = 15;

    memset(ht->count, 0, sizeof(ht->count));
    for (int i = 0; i < num; i++)
        if (lens[i] > 0) ht->count[lens[i]]++;

    uint16_t code = 0;
    uint16_t nextCode[16] = {0};
    for (int len = 1; len <= maxLen; len++) {
        code = (code + ht->count[len - 1]) << 1;
        nextCode[len] = code;
    }

    memset(ht->symbol, 0, sizeof(ht->symbol));
    for (int i = 0; i < num; i++) {
        if (lens[i] > 0) {
            int len = lens[i];
            uint16_t c = nextCode[len]++;
            // Reverse bits for lookup
            uint16_t rev = 0;
            for (int b = 0; b < len; b++) {
                rev = (rev << 1) | ((c >> b) & 1);
            }
            ht->symbol[rev] = (uint16_t)i;
        }
    }
    return maxLen;
}

int main(void) {
    // Build test - fixed tables
    int ll[288];
    for (int i = 0; i < 144; i++) ll[i] = 8;
    for (int i = 144; i < 256; i++) ll[i] = 9;
    for (int i = 256; i < 280; i++) ll[i] = 7;
    for (int i = 280; i < 288; i++) ll[i] = 8;

    huff_table ht;
    int maxB = huff_build(&ht, ll, 288);
    printf("Fixed LL max bits: %d\n", maxB);
    printf("count[7]=%d count[8]=%d count[9]=%d\n", ht.count[7], ht.count[8], ht.count[9]);

    // Now try to decode a literal
    uint8_t test_data[] = {0xf3, 0xcb, 0x23, 0xe4, 0xcc, 0xc2, 0xc0, 0xc0};
    // First check: what's the first byte of the zlib stream? 78 9C is header
    // Deflate data starts at byte 2: 0x3B
    uint8_t deflate_data[] = {0x3b, 0xcb, 0x23, 0xe4, 0xcc, 0xc2, 0xc0, 0xc0, 0xc0, 0x04};
    
    // BFINAL=1, BTYPE=01 (fixed Huffman): first 3 bits
    // First byte 0x3B = 00111011
    // Reading bits: LSB first. Bit 0 = 1 (BFINAL=1), bits 1-2 = 01 (BTYPE=1=fixed)
    printf("First byte 0x3B in binary (LSB first): ");
    for (int i = 0; i < 8; i++) printf("%d", (0x3B >> i) & 1);
    printf("\n");

    // So first 3 bits consumed: BFINAL=1, BTYPE=01
    // Next we decode fixed Huffman literals/lengths
    
    zlib_stream z;
    z.in = deflate_data;
    z.inPos = 0;
    z.inLen = sizeof(deflate_data);
    z.bitBuf = 0;
    z.bitCount = 0;

    int bfinal = bits_read(&z, 1);
    int btype = bits_read(&z, 2);
    printf("BFINAL=%d BTYPE=%d\n", bfinal, btype);
    printf("bitCount after header=%d\n", z.bitCount);

    // Now try decoding a literal using huff_decode
    // With fixed Huffman, first literal should be a byte
    // Let's see what the first code decodes to
    uint32_t code = 0;
    int codeLen = 0;
    while (codeLen < maxB) {
        int b = bits_read(&z, 1);
        if (b < 0) break;
        code = (code << 1) | b;
        codeLen++;
        printf("  code=%d len=%d sym_table[%d]=%d\n", code, codeLen, code, ht.symbol[code]);
        if (ht.symbol[code] != 0) break;
    }

    return 0;
}
