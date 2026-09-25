#include "df_resource.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static DFArchive g_dfArchive;

bool DF_Open(const char* path) {
    if (g_dfArchive.isOpen) {
        DF_Close();
    }

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0x70) { fclose(f); return false; }

    g_dfArchive.data = (uint8_t*)malloc(sz);
    if (!g_dfArchive.data) { fclose(f); return false; }
    fread(g_dfArchive.data, 1, sz, f);
    fclose(f);

    g_dfArchive.fileSize = (uint32_t)sz;

    // Check magic
    if (memcmp(g_dfArchive.data, "df", 2) != 0 &&
        memcmp(g_dfArchive.data, "sd", 2) != 0) {
        free(g_dfArchive.data);
        g_dfArchive.data = NULL;
        return false;
    }

    uint32_t count = *(uint32_t*)(g_dfArchive.data + 0x34);
    if (count > DF_MAX_ENTRIES) count = DF_MAX_ENTRIES;

    strncpy(g_dfArchive.path, path, sizeof(g_dfArchive.path) - 1);

    g_dfArchive.entryCount = 0;
    int pos = DF_HEADER_ENTRY_TABLE;
    for (uint32_t i = 0; i < count; i++) {
        if (pos + DF_ENTRY_SIZE > (int)g_dfArchive.fileSize) break;

        memcpy(g_dfArchive.entries[i].name, g_dfArchive.data + pos, DF_ENTRY_NAME_LEN);
        g_dfArchive.entries[i].name[DF_ENTRY_NAME_LEN - 1] = '\0';

        g_dfArchive.entries[i].offset = *(uint32_t*)(g_dfArchive.data + pos + 32);
        g_dfArchive.entries[i].size = *(uint32_t*)(g_dfArchive.data + pos + 36);

        g_dfArchive.entryCount++;
        pos += DF_ENTRY_SIZE;
    }

    g_dfArchive.isOpen = true;
    return true;
}

void DF_Close(void) {
    if (g_dfArchive.data) {
        free(g_dfArchive.data);
        g_dfArchive.data = NULL;
    }
    g_dfArchive.fileSize = 0;
    g_dfArchive.entryCount = 0;
    g_dfArchive.isOpen = false;
    g_dfArchive.path[0] = '\0';
}

int DF_GetCount(void) {
    return g_dfArchive.entryCount;
}

const char* DF_GetName(int idx) {
    if (idx < 0 || idx >= g_dfArchive.entryCount) return NULL;
    return g_dfArchive.entries[idx].name;
}

uint32_t DF_GetSize(int idx) {
    if (idx < 0 || idx >= g_dfArchive.entryCount) return 0;
    return g_dfArchive.entries[idx].size;
}

uint8_t* DF_ReadAlloc(int idx) {
    if (idx < 0 || idx >= g_dfArchive.entryCount) return NULL;
    DFEntry* e = &g_dfArchive.entries[idx];
    if (e->offset + e->size > g_dfArchive.fileSize) return NULL;

    uint8_t* buf = (uint8_t*)malloc(e->size);
    if (!buf) return NULL;
    memcpy(buf, g_dfArchive.data + e->offset, e->size);
    return buf;
}

int DF_Find(const char* name) {
    for (int i = 0; i < g_dfArchive.entryCount; i++) {
        if (_stricmp(g_dfArchive.entries[i].name, name) == 0)
            return i;
    }
    return -1;
}
