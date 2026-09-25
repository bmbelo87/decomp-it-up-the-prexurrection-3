#ifndef ZLIBINFLATE_H
#define ZLIBINFLATE_H

#include <stdint.h>
#include <stdbool.h>

int zlib_decompress(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t* outLen);
int zlib_decompress_ex(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t* outLen, uint32_t* inConsumed);

#endif
