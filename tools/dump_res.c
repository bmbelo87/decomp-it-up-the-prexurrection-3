#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push,1)
typedef struct {
    char name[24];
    uint32_t offset;
    uint32_t size;
} RESEntry;
#pragma pack(pop)

static void xor_decrypt(uint8_t* data, uint32_t size) {
    uint8_t key = 0xEF;
    uint8_t step = 0x4F;
    for (uint32_t i = 0; i < size; i++) {
        data[i] ^= key;
        key += step;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: dump_res <file>\n"); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "can't open '%s'\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc(fsize);
    fread(data, 1, fsize, f);
    fclose(f);

    if (memcmp(data, "RES", 3) != 0) { fprintf(stderr, "bad magic\n"); return 1; }

    uint32_t fileCount = *(uint32_t*)(data + 8);
    printf("RES file: %ld bytes, %u entries\n", fsize, fileCount);

    uint32_t dirSize = fileCount * sizeof(RESEntry);
    uint32_t h2 = 0x18 + dirSize;
    uint32_t seekRel = *(uint32_t*)(data + h2 + 0x40);
    uint32_t dataStart = h2 + 0x44 + seekRel;

    printf("dirSize=%u h2=0x%x seekRel=%u dataStart=0x%x\n", dirSize, h2, seekRel, dataStart);

    RESEntry* entries = (RESEntry*)malloc(dirSize);
    memcpy(entries, data + 0x18, dirSize);
    xor_decrypt((uint8_t*)entries, dirSize);

    for (uint32_t i = 0; i < fileCount; i++) {
        entries[i].offset += dataStart;
        printf("[%2u] name='%s' offset=0x%x size=%u\n",
               i, entries[i].name, entries[i].offset, entries[i].size);
    }

    free(entries);
    free(data);
    return 0;
}
