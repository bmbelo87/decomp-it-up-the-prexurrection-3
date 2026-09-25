#ifndef DF_RESOURCE_H
#define DF_RESOURCE_H

#include "pumpy.h"

#define DF_MAGIC_DF "df"
#define DF_MAGIC_SD "sd"
#define DF_ENTRY_NAME_LEN 32
#define DF_ENTRY_SIZE 40
#define DF_MAX_ENTRIES 512
#define DF_HEADER_ENTRY_TABLE 0x4C

typedef struct {
    char name[DF_ENTRY_NAME_LEN];
    uint32_t offset;
    uint32_t size;
} DFEntry;

typedef struct {
    char path[MAX_PATH];
    uint8_t* data;
    uint32_t fileSize;
    int entryCount;
    DFEntry entries[DF_MAX_ENTRIES];
    bool isOpen;
} DFArchive;

bool DF_Open(const char* path);
void DF_Close(void);
int DF_GetCount(void);
const char* DF_GetName(int idx);
uint32_t DF_GetSize(int idx);
uint8_t* DF_ReadAlloc(int idx);
int DF_Find(const char* name);

#endif
