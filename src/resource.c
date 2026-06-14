#include "pumpy.h"

#define RES_MAGIC "RES"
#define RES_KEY_INIT 0xEF
#define RES_KEY_STEP 0x4F

typedef struct {
    char name[16];
    uint32_t size;
    uint32_t offset;
    uint32_t checksum;
} RESEntry; // 28 bytes total

typedef struct {
    char path[MAX_PATH];
    uint8_t* data;
    uint32_t fileSize;
    int fileCount;
    RESEntry* entries;
} RESArchive;

static RESArchive* g_resArchive = NULL;

static void res_xor_decrypt(uint8_t* data, uint32_t size) {
    uint8_t key = RES_KEY_INIT;
    for (uint32_t i = 0; i < size; i++) {
        data[i] ^= key;
        key = (uint8_t)(key + RES_KEY_STEP);
    }
}

static int res_find_by_name(RESArchive* res, const char* name) {
    for (int i = 0; i < res->fileCount; i++) {
        if (_stricmp(res->entries[i].name, name) == 0)
            return i;
    }
    return -1;
}

bool RES_Open(const char* path) {
    if (g_resArchive) RES_Close();

    RESArchive* res = (RESArchive*)calloc(1, sizeof(RESArchive));
    if (!res) return false;

    strncpy(res->path, path, sizeof(res->path) - 1);

    FILE* f = fopen(path, "rb");
    if (!f) { free(res); return false; }

    fseek(f, 0, SEEK_END);
    res->fileSize = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    res->data = (uint8_t*)malloc(res->fileSize);
    if (!res->data) { fclose(f); free(res); return false; }

    if (fread(res->data, 1, res->fileSize, f) != res->fileSize) {
        fclose(f); free(res->data); free(res); return false;
    }
    fclose(f);

    if (memcmp(res->data, RES_MAGIC, 4) != 0) {
        Log_Print("RES: invalid magic in '%s'\n", path);
        free(res->data); free(res); return false;
    }

    res->fileCount = *(uint32_t*)(res->data + 8);
    uint32_t dirSize = res->fileCount * sizeof(RESEntry);

    uint8_t* dirEnc = res->data + 0x18;
    res->entries = (RESEntry*)calloc(res->fileCount, sizeof(RESEntry));
    if (!res->entries) { free(res->data); free(res); return false; }

    memcpy(res->entries, dirEnc, dirSize);
    res_xor_decrypt((uint8_t*)res->entries, dirSize);

    uint32_t h2 = 0x18 + dirSize;
    uint32_t seekRel = *(uint32_t*)(res->data + h2 + 0x40);
    uint32_t dataStart = h2 + 0x44 + seekRel;

    for (int i = 0; i < res->fileCount; i++) {
        res->entries[i].offset += dataStart;
    }

    g_resArchive = res;
    Log_Print("RES: opened '%s' (%d files, %u bytes)\n", path, res->fileCount, res->fileSize);
    return true;
}

int RES_GetCount(void) {
    return g_resArchive ? g_resArchive->fileCount : 0;
}

int RES_Find(const char* name) {
    if (!g_resArchive) return -1;
    return res_find_by_name(g_resArchive, name);
}

const char* RES_GetName(int index) {
    if (!g_resArchive || index < 0 || index >= g_resArchive->fileCount) return NULL;
    return g_resArchive->entries[index].name;
}

uint32_t RES_GetSize(int index) {
    if (!g_resArchive || index < 0 || index >= g_resArchive->fileCount) return 0;
    return g_resArchive->entries[index].size;
}

bool RES_Read(int index, uint8_t* buffer) {
    if (!g_resArchive || index < 0 || index >= g_resArchive->fileCount) return false;
    RESEntry* e = &g_resArchive->entries[index];
    if (e->offset + e->size > g_resArchive->fileSize) return false;
    memcpy(buffer, g_resArchive->data + e->offset, e->size);
    res_xor_decrypt(buffer, e->size);
    return true;
}

uint8_t* RES_ReadAlloc(int index) {
    if (!g_resArchive || index < 0 || index >= g_resArchive->fileCount) return NULL;
    RESEntry* e = &g_resArchive->entries[index];
    uint8_t* buf = (uint8_t*)malloc(e->size);
    if (!buf) return NULL;
    if (!RES_Read(index, buf)) { free(buf); return NULL; }
    return buf;
}

void RES_Close(void) {
    if (g_resArchive) {
        free(g_resArchive->data);
        free(g_resArchive->entries);
        free(g_resArchive);
        g_resArchive = NULL;
    }
}

bool RES_IsOpen(void) {
    return g_resArchive != NULL;
}

void* Resource_Load(int resId, const char* type, DWORD* outSize) {
    HRSRC hRes = FindResourceA(g_game.hInstance, MAKEINTRESOURCEA(resId), type);
    if (!hRes) {
        Log_Print("Resource: find %d/%s failed\n", resId, type);
        return NULL;
    }
    HGLOBAL hGlob = LoadResource(g_game.hInstance, hRes);
    if (!hGlob) return NULL;
    if (outSize) *outSize = SizeofResource(g_game.hInstance, hRes);
    return LockResource(hGlob);
}

int Resource_LoadWave(int resId, const char* name) {
    DWORD size;
    void* data = Resource_Load(resId, "WAVE", &size);
    if (!data) return -1;
    return Audio_LoadWAV(name, (const uint8_t*)data, size);
}

int Resource_LoadTexture(int resId, const char* name) {
    DWORD size;
    void* data = Resource_Load(resId, "TEXTURE", &size);
    if (!data) return -1;

    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    char tmpPath[MAX_PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s_tmp_%s", tmpDir, name);

    FILE* f = fopen(tmpPath, "wb");
    if (!f) { free(data); return -1; }
    fwrite(data, 1, size, f);
    fclose(f);

    int texId = Texture_Load(tmpPath);
    remove(tmpPath);
    return texId;
}

static bool hasExt(const char* name, const char* ext) {
    int nl = (int)strlen(name);
    int el = (int)strlen(ext);
    if (nl < el) return false;
    return _stricmp(name + nl - el, ext) == 0;
}

static int loadTextureFromRES(const char* resName) {
    int idx = RES_Find(resName);
    Log_Print("RES: tex find '%s' -> %d\n", resName, idx);
    if (idx < 0) {
        const char* ext = strrchr(resName, '.');
        char base[64];
        if (ext) {
            int baseLen = (int)(ext - resName);
            int cpLen = baseLen < 63 ? baseLen : 63;
            memcpy(base, resName, cpLen);
            base[cpLen] = '\0';
        } else {
            strncpy(base, resName, sizeof(base) - 1);
        }

        const char* exts[] = { "png", "tga", "jpg", "bmp" };
        for (int ei = 0; ei < 4; ei++) {
            char tryName[64];
            snprintf(tryName, sizeof(tryName), "%s.%s", base, exts[ei]);
            idx = RES_Find(tryName);
            Log_Print("RES: tex try '%s' -> %d\n", tryName, idx);
            if (idx >= 0) {
                Log_Print("RES: tex matched entry name='%s'\n", RES_GetName(idx));
                break;
            }
        }
        if (idx < 0) return -1;
    }

    uint32_t sz = RES_GetSize(idx);
    if (sz == 0) return -1;

    uint8_t* buf = RES_ReadAlloc(idx);
    if (!buf) return -1;

    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    char tmpPath[MAX_PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s_tmp_%s", tmpDir, RES_GetName(idx));

    FILE* f = fopen(tmpPath, "wb");
    if (f) {
        fwrite(buf, 1, sz, f);
        fclose(f);
        Log_Print("RES: tex loading '%s' (%u bytes)\n", tmpPath, sz);
        int texId = Texture_Load(tmpPath);
        Log_Print("RES: tex loaded -> %d\n", texId);
        remove(tmpPath);
        free(buf);
        return texId;
    }
    Log_Print("RES: tex write failed for '%s'\n", tmpPath);
    free(buf);
    return -1;
}

#define SPR_MAX_TILES 512

typedef struct {
    char name[16];
    char texture[64];
    int srcX, srcY, srcW, srcH;  // Posição e tamanho na tela
    int u1, v1, u2, v2;          // Coordenadas de recorte na textura
    bool flipH, flipV;           // Espelhamento
} SPRTile;

typedef struct {
    bool isAni;
    int tileCount;
    SPRTile tiles[SPR_MAX_TILES];
} SPRFile;

static bool parseSPR(const uint8_t* data, uint32_t size, SPRFile* spr) {
    memset(spr, 0, sizeof(SPRFile));
    const char* text = (const char*)data;

    // Parse header lines: TYPE ani/tile, NUM <count>
    int expectedTiles = 0;
    for (uint32_t i = 0; i + 4 < size; i++) {
        if (text[i] == '\n' || text[i] == '\r') { i++; continue; }
        if (i > 0 && text[i-1] != '\n' && text[i-1] != '\r') continue;
        // At start of a line
        if ((text[i+0] == 'T' || text[i+0] == 't') &&
            (text[i+1] == 'Y' || text[i+1] == 'y') &&
            (text[i+2] == 'P' || text[i+2] == 'p') &&
            (text[i+3] == 'E' || text[i+3] == 'e')) {
            const char* rest = text + i + 4;
            while (*rest == ' ' || *rest == '\t') rest++;
            if ((rest[0] == 'a' || rest[0] == 'A') &&
                (rest[1] == 'n' || rest[1] == 'N') &&
                (rest[2] == 'i' || rest[2] == 'I')) {
                spr->isAni = true;
            }
        } else if ((text[i+0] == 'N' || text[i+0] == 'n') &&
                   (text[i+1] == 'U' || text[i+1] == 'u') &&
                   (text[i+2] == 'M' || text[i+2] == 'm')) {
            const char* rest = text + i + 3;
            while (*rest == ' ' || *rest == '\t') rest++;
            expectedTiles = atoi(rest);
        }
    }

    // Formato SPR:
    // TYPE tile/ani
    // NUM <count>
    // T <texture> <srcX> <srcY> <srcW> <srcH> <u1> <v1> <u2> <v2>
    
    for (uint32_t i = 0; i < size; i++) {
        if (text[i] != 'T' && text[i] != 't') continue;
        if (i > 0 && text[i-1] != '\n' && text[i-1] != '\r') continue;
        if (i + 2 >= size) continue;
        if (text[i+1] != ' ' && text[i+1] != '\t') continue;
        if (i > 0 && text[i-1] != '\n' && text[i-1] != '\r') continue;
        if (i + 2 >= size) continue;
        if (text[i+1] != ' ' && text[i+1] != '\t') continue;

        const char* line = text + i + 2;
        uint32_t lineLen = 0;
        while (i + 2 + lineLen < size && line[lineLen] >= ' ') lineLen++;
        if (lineLen == 0) continue;

        char buf[256];
        uint32_t cp = lineLen < sizeof(buf) - 1 ? lineLen : sizeof(buf) - 1;
        memcpy(buf, line, cp);
        buf[cp] = '\0';

        char tokens[10][64];
        int tokCount = 0;
        memset(tokens, 0, sizeof(tokens));
        char* p = buf;
        while (*p && tokCount < 10) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            int ti = 0;
            while (*p && *p != ' ' && *p != '\t' && ti < 63) {
                tokens[tokCount][ti++] = *p++;
            }
            tokens[tokCount][ti] = '\0';
            tokCount++;
        }

        if (tokCount < 9) continue;  // T texture srcX srcY srcW srcH u1 v1 u2 v2

        SPRTile* t = &spr->tiles[spr->tileCount];
        memset(t, 0, sizeof(SPRTile));

        // Formato: T Arquivo.tga A B C D E F G H
        // A=srcX, B=srcY, C=srcW, D=srcH
        // E=u1 (X inicial recorte), F=v1 (Y inicial recorte)
        // G=u2 (X final recorte), H=v2 (Y final recorte)
        
        int ti = 0;
        strncpy(t->texture, tokens[ti], sizeof(t->texture) - 1); ti++;
        
        t->srcX = atoi(tokens[ti+0]);
        t->srcY = atoi(tokens[ti+1]);
        t->srcW = atoi(tokens[ti+2]);
        t->srcH = atoi(tokens[ti+3]);
        
        int E = atoi(tokens[ti+4]);  // u1 (X inicial recorte)
        int F = atoi(tokens[ti+5]);  // v1 (Y inicial recorte)
        int G = atoi(tokens[ti+6]);  // u2 (X final recorte)
        int H = atoi(tokens[ti+7]);  // v2 (Y final recorte)
        
        // No .sp2, valores negativos são offsets relativos
        // Se u1 > u2, a imagem é espelhada horizontalmente
        // Se v1 > v2, a imagem é espelhada verticalmente
        t->u1 = E;
        t->v1 = F;
        t->u2 = G;
        t->v2 = H;
        t->flipH = (E > G);  // Espelhado se inicial > final
        t->flipV = (F > H);  // Espelhado se inicial > final

        for (int ci = 0; t->texture[ci]; ci++) {
            if (t->texture[ci] >= 'A' && t->texture[ci] <= 'Z')
                t->texture[ci] = (char)(t->texture[ci] + 32);
        }

        spr->tileCount++;
        if (spr->tileCount >= SPR_MAX_TILES) break;
    }

    return spr->tileCount > 0;
}

static bool isValidBGA(const uint8_t* data, uint32_t size) {
    if (size < 8) return false;
    return (memcmp(data, "BGA2", 4) == 0) || (memcmp(data, "BGA", 3) == 0);
}

static bool loadBGAFromRES(const char* bgaName, int bgaIdx) {
    uint32_t bgaSize = RES_GetSize(bgaIdx);
    uint8_t* bgaData = RES_ReadAlloc(bgaIdx);
    if (!bgaData) return false;

    bool isBGA2 = (memcmp(bgaData, "BGA2", 4) == 0);

    if (!isBGA2 && memcmp(bgaData, "BGA", 3) != 0) {
        Log_Print("BGA: unknown BGA format\n");
        free(bgaData);
        return false;
    }

    BGAPicture* bga = &g_game.bgaPics[g_game.bgaPicCount];
    memset(bga, 0, sizeof(BGAPicture));
    snprintf(bga->name, sizeof(bga->name), "%s", bgaName);

    if (isBGA2) {
        bga->version = 2;
        int evPos = 16;
        int maxEvents = 50;
        for (int evt = 0; evt < maxEvents && evPos + 68 <= (int)bgaSize && bga->layerCount < MAX_BGA_LAYERS; evt++)
        {
            char filename[64];
            memset(filename, 0, sizeof(filename));
            int nameLen = 0;
            for (int i = 0; i < 64 && evPos + i < (int)bgaSize; i++)
            {
                char c = bgaData[evPos + i];
                if (c >= 0x20 && c <= 0x7E)
                {
                    if (nameLen < 63) filename[nameLen++] = c;
                }
                else break;
            }
            filename[nameLen] = '\0';

            int count = 0;
            if (evPos + 68 <= (int)bgaSize)
                count = *(int*)(bgaData + evPos + 64);

            BGALayer* layer = &bga->layers[bga->layerCount];

            if (count > 0 && nameLen > 0)
            {
                for (int ci = 0; filename[ci]; ci++)
                    if (filename[ci] >= 'A' && filename[ci] <= 'Z')
                        filename[ci] = (char)(filename[ci] + 32);

                strncpy(layer->filename, filename, sizeof(layer->filename) - 1);
                char* dot = strrchr(filename, '.');
                layer->isSPR = (dot && (_stricmp(dot, ".spr") == 0 || _stricmp(dot, ".sp2") == 0));

                int kfDataOff = evPos + 68;
                layer->kfCount = 0;
                for (int i = 0; i < count && layer->kfCount < MAX_BGA_KEYFRAMES; i++)
                {
                    uint32_t ofs = kfDataOff + i * 64;
                    if (ofs + 64 > bgaSize) break;
                    uint8_t* raw = bgaData + ofs;

                    float* floats = (float*)raw;
                    uint32_t ts = *(uint32_t*)(raw + 44);
                    uint32_t z_order = *(uint32_t*)(raw + 48);

                    BGAKeyframe* kf = &layer->keyframes[layer->kfCount];
                    kf->frame = (int)(ts & 0xFFFF);
                    kf->type = (int)((ts >> 16) & 0xFFFF);
                    kf->x = floats[0];
                    kf->y = floats[1];
                    kf->hotx = floats[2];
                    kf->hoty = floats[3];
                    kf->scaleX = floats[4];
                    kf->scaleY = floats[5];
                    kf->rotation = floats[6];
                    kf->r = floats[7];
                    kf->g = floats[8];
                    kf->b = floats[9];
                    kf->a = floats[10];
                    kf->z_order = (int)z_order;
                    layer->kfCount++;
                }

                if (layer->kfCount > 0)
                {
                    int maxF = layer->keyframes[layer->kfCount - 1].frame;
                    if (maxF > bga->maxFrame) bga->maxFrame = maxF;
                }

                Log_Print("BGA2: evt[%d] '%s' %s kf=%d maxFrame=%d\n",
                    evt, filename, layer->isSPR ? "SPR" : "TGA", layer->kfCount, bga->maxFrame);
            }
            else
            {
                layer->filename[0] = '\0';
                layer->isSPR = false;
                layer->kfCount = 0;
                Log_Print("BGA2: evt[%d] (empty)\n", evt);
            }

            bga->layerCount++;
            evPos += 64 + 4 + (count > 0 ? count * 64 : 0);
        }
    } else {
        bga->version = 0;

        int scanStart = 4;
        while (scanStart < (int)bgaSize - 4 && bga->layerCount < MAX_BGA_LAYERS) {
            char filename[64];
            int nameStart = -1;
            int nameEnd = -1;

            for (int i = scanStart; i + 4 < (int)bgaSize; i++) {
                if (bgaData[i] == '.') {
                    char ext[5];
                    memcpy(ext, bgaData + i, 4);
                    ext[4] = '\0';
                    if (_stricmp(ext, ".spr") == 0 || _stricmp(ext, ".sp2") == 0 ||
                        _stricmp(ext, ".tga") == 0 || _stricmp(ext, ".TGA") == 0) {
                        int s = i;
                        while (s > 0 && bgaData[s - 1] >= 0x20 && bgaData[s - 1] < 0x7F) s--;
                        int len = (i + 4 - s);
                        if (len < 64) {
                            memcpy(filename, bgaData + s, len);
                            filename[len] = '\0';
                            nameStart = s;
                            nameEnd = i + 4;
                            break;
                        }
                    }
                }
            }

            if (nameStart < 0) break;

            for (int ci = 0; filename[ci]; ci++) {
                if (filename[ci] >= 'A' && filename[ci] <= 'Z')
                    filename[ci] = (char)(filename[ci] + 32);
            }

            if (bga->layerCount == 0) {
                strncpy(bga->name, filename, sizeof(bga->name) - 1);
            }

            int ptr = nameEnd;
            while (ptr < (int)bgaSize && bgaData[ptr] == 0) ptr++;
            int aligned = ((ptr + 3) / 4) * 4;

            int numKF = 0;
            int dataOff = 0;
            for (int scan = aligned; scan < (int)bgaSize - 4; scan += 4) {
                uint32_t val = *(uint32_t*)(bgaData + scan);
                if (val >= 1 && val <= 5000) {
                    numKF = (int)val;
                    dataOff = scan + 4;
                    break;
                }
            }

            if (numKF > 0 && dataOff > 0) {
                BGALayer* layer = &bga->layers[bga->layerCount];
                strncpy(layer->filename, filename, sizeof(layer->filename) - 1);

                char* dot = strrchr(filename, '.');
                layer->isSPR = (dot && (_stricmp(dot, ".spr") == 0 || _stricmp(dot, ".sp2") == 0));

                int kfSize = 44;
                layer->kfCount = 0;
                for (int i = 0; i < numKF && layer->kfCount < MAX_BGA_KEYFRAMES; i++) {
                    uint32_t ofs = dataOff + i * kfSize;
                    if (ofs + kfSize > bgaSize) break;

                    uint32_t ts = *(uint32_t*)(bgaData + ofs);
                    float* floats = (float*)(bgaData + ofs + 4);
                    BGAKeyframe* kf = &layer->keyframes[layer->kfCount];

                    kf->frame = (int)(ts & 0xFFFF);
                    kf->type = (int)((ts >> 16) & 0xFFFF);
                    kf->x = floats[0];
                    kf->y = floats[1];
                    kf->hotx = floats[2];
                    kf->hoty = floats[3];
                    kf->scaleX = floats[4];
                    kf->scaleY = floats[5];
                    
                    kf->r = floats[6];
                    kf->g = floats[7];
                    kf->b = floats[8];
                    kf->a = floats[9];
                    kf->rotation = 0.0f;
                    kf->z_order = 0;

                    layer->kfCount++;
                }

                if (layer->kfCount > 0) {
                    int maxF = layer->keyframes[layer->kfCount - 1].frame;
                    if (maxF > bga->maxFrame) bga->maxFrame = maxF;
                }

                Log_Print("BGA0: '%s' %s kf=%d maxFrame=%d\n",
                    filename, layer->isSPR ? "SPR" : "TGA", layer->kfCount, bga->maxFrame);

                bga->layerCount++;
                scanStart = dataOff + numKF * kfSize;
            } else {
                break;
            }
        }
    }

    free(bgaData);
    g_game.bgaPicCount++;
    Log_Print("BGA: loaded '%s' (v%d, %d layers)\n",
        bgaName, bga->version, bga->layerCount);

    return true;
}

static int loadSPRFromRES(const char* sprName) {
    char sprFileName[64];
    const char* ext = strrchr(sprName, '.');
    if (ext && (_stricmp(ext, ".spr") == 0 || _stricmp(ext, ".sp2") == 0)) {
        strncpy(sprFileName, sprName, sizeof(sprFileName) - 1);
    } else {
        snprintf(sprFileName, sizeof(sprFileName), "%s.spr", sprName);
    }

    int sprIdx = RES_Find(sprFileName);
    if (sprIdx < 0) {
        char altName[64];
        if (ext && _stricmp(ext, ".sp2") == 0) {
            snprintf(altName, sizeof(altName), "%.*s.spr", (int)(ext - sprName), sprName);
        } else if (ext) {
            snprintf(altName, sizeof(altName), "%.*s.sp2", (int)(ext - sprName), sprName);
        } else {
            snprintf(altName, sizeof(altName), "%s.sp2", sprName);
        }
        sprIdx = RES_Find(altName);
        if (sprIdx < 0) {
            for (int i = 0; i < RES_GetCount(); i++) {
                const char* n = RES_GetName(i);
                if (n && (_stricmp(n, sprFileName) == 0 || _stricmp(n, altName) == 0)) { sprIdx = i; break; }
            }
        }
    }

    if (sprIdx < 0) return 0;

    uint32_t sprSize = RES_GetSize(sprIdx);
    uint8_t* sprData = RES_ReadAlloc(sprIdx);
    if (!sprData) return 0;

    SPRFile spr;
    if (!parseSPR(sprData, sprSize, &spr)) {
        free(sprData);
        Log_Print("RES: spr '%s' parse failed (%d bytes)\n", sprFileName, sprSize);
        return 0;
    }

    Log_Print("RES: spr '%s' (%d tiles, isAni=%d)\n", sprFileName, spr.tileCount, spr.isAni);

    for (int i = 0; i < spr.tileCount && g_game.sprTileCount < MAX_SPR_TILES; i++) {
        SPRTileDef* t = &g_game.sprTiles[g_game.sprTileCount];
        strncpy(t->name, spr.tiles[i].name, sizeof(t->name) - 1);
        strncpy(t->texture, spr.tiles[i].texture, sizeof(t->texture) - 1);
        t->srcX = spr.tiles[i].srcX;
        t->srcY = spr.tiles[i].srcY;
        t->srcW = spr.tiles[i].srcW;
        t->srcH = spr.tiles[i].srcH;
        t->u1 = spr.tiles[i].u1;
        t->v1 = spr.tiles[i].v1;
        t->u2 = spr.tiles[i].u2;
        t->v2 = spr.tiles[i].v2;
        t->flipH = spr.tiles[i].flipH;
        t->flipV = spr.tiles[i].flipV;
        t->texId = -1;

        char texName[64];
        strncpy(texName, spr.tiles[i].texture, sizeof(texName) - 1);
        for (int ci = 0; texName[ci]; ci++) {
            if (texName[ci] >= 'A' && texName[ci] <= 'Z')
                texName[ci] = (char)(texName[ci] + 32);
        }

        t->texId = loadTextureFromRES(texName);

        if (strstr(sprFileName, "go_c04") || strstr(sprFileName, "GO_C04")) {
            Log_Print("SPR: '%s' tile='%s' tex='%s' texId=%d src=(%d,%d,%d,%d) uv=(%d,%d,%d,%d)\n",
                      sprFileName, t->name, t->texture, t->texId,
                      t->srcX, t->srcY, t->srcW, t->srcH,
                      t->u1, t->v1, t->u2, t->v2);
        }

        g_game.sprTileCount++;
    }

    free(sprData);
    return spr.isAni ? spr.tileCount : 0;
}

bool Resource_LoadBGA(int resId) {
    (void)resId;
    return false;
}

static void loadAllSPRsFromRES(void) {
    int count = RES_GetCount();
    for (int i = 0; i < count; i++) {
        const char* name = RES_GetName(i);
        if (name && (hasExt(name, ".spr") || hasExt(name, ".sp2"))) {
            loadSPRFromRES(name);
        }
    }
}

bool Resource_LoadBGAByName(const char* datName) {
    Log_Print("RES: loading BGA '%s'\n", datName);

    char datPath[MAX_PATH];
    const char* paths[] = {
        "%s\\BGA\\%s.DAT",
        "%s\\assets\\BGA\\%s.DAT",
        "%s\\BGA\\%s.dat",
        "%s\\assets\\BGA\\%s.dat"
    };

    bool opened = false;
    for (int i = 0; i < 4; i++) {
        snprintf(datPath, sizeof(datPath), paths[i], g_game.currentDirectory, datName);
        if (RES_Open(datPath)) { opened = true; break; }
    }
    if (!opened) return false;

    int bgaIdx = -1;
    for (int i = 0; i < RES_GetCount(); i++) {
        const char* n = RES_GetName(i);
        if (n && hasExt(n, ".bga")) { bgaIdx = i; break; }
    }

    if (bgaIdx >= 0) {
        if (!loadBGAFromRES(datName, bgaIdx)) {
            RES_Close();
            return false;
        }

        BGAPicture* bga = &g_game.bgaPics[g_game.bgaPicCount - 1];

        for (int i = 0; i < bga->layerCount; i++) {
            BGALayer* layer = &bga->layers[i];
            const char* srcName = layer->filename;
            if (srcName[0] == '\0') continue;
            if (*srcName == '?') srcName++;
            if (layer->isSPR && layer->sprTileCount == 0) {
                layer->sprTileStart = g_game.sprTileCount;
                int aniFrames = loadSPRFromRES(srcName);
                layer->sprTileCount = g_game.sprTileCount - layer->sprTileStart;
                layer->aniFrameCount = aniFrames;
            } else if (!layer->isSPR) {
                layer->texId = loadTextureFromRES(srcName);
            }
        }
    } else {
        Log_Print("RES: no .bga in '%s', loading SPRs only\n", datName);
        loadAllSPRsFromRES();
    }

    RES_Close();
    return true;
}

bool Resource_LoadBGADirect(const char* datPath) {
    if (!RES_Open(datPath)) return false;

    int bgaIdx = -1;
    for (int i = 0; i < RES_GetCount(); i++) {
        const char* n = RES_GetName(i);
        if (n && hasExt(n, ".bga")) { bgaIdx = i; break; }
    }
    if (bgaIdx < 0) { RES_Close(); return false; }

    uint32_t bgaSize = RES_GetSize(bgaIdx);
    uint8_t* bgaData = RES_ReadAlloc(bgaIdx);
    if (!bgaData) { RES_Close(); return false; }

    bool isBGA2 = (memcmp(bgaData, "BGA2", 4) == 0);

    BGAPicture* bga = &g_game.bgaPics[g_game.bgaPicCount];
    memset(bga, 0, sizeof(BGAPicture));
    snprintf(bga->name, sizeof(bga->name), "%s", RES_GetName(bgaIdx));
    if (hasExt(bga->name, ".bga")) bga->name[strlen(bga->name) - 4] = '\0';

    int kfSize;
    int scanStart;
    if (isBGA2) {
        bga->version = 2;
        kfSize = 64;
        scanStart = 4;
    } else if (memcmp(bgaData, "BGA", 3) == 0) {
        bga->version = 0;
        kfSize = 44;
        scanStart = 4;
    } else {
        Log_Print("BGA: unknown BGA format\n");
        free(bgaData);
        RES_Close();
        return false;
    }

    while (scanStart < (int)bgaSize - 4 && bga->layerCount < MAX_BGA_LAYERS) {
        char filename[64];
        int nameStart = -1;
        int nameEnd = -1;

        for (int i = scanStart; i + 4 < (int)bgaSize; i++) {
            if (bgaData[i] == '.') {
                char ext[5];
                memcpy(ext, bgaData + i, 4);
                ext[4] = '\0';
                if (_stricmp(ext, ".spr") == 0 || _stricmp(ext, ".sp2") == 0 ||
                    _stricmp(ext, ".tga") == 0 || _stricmp(ext, ".TGA") == 0) {
                    int s = (int)i;
                    while (s > 0 && bgaData[s - 1] >= 0x20 && bgaData[s - 1] < 0x7F) s--;
                    int len = (int)(i + 4 - s);
                    if (len < 64) {
                        memcpy(filename, bgaData + s, len);
                        filename[len] = '\0';
                        nameStart = s;
                        nameEnd = i + 4;
                        break;
                    }
                }
            }
        }

        if (nameStart < 0) break;

        for (int ci = 0; filename[ci]; ci++) {
            if (filename[ci] >= 'A' && filename[ci] <= 'Z')
                filename[ci] = (char)(filename[ci] + 32);
        }

        int ptr = nameEnd;
        while (ptr < (int)bgaSize && bgaData[ptr] == 0) ptr++;
        int aligned = ((ptr + 3) / 4) * 4;

        int numKF = 0;
        int dataOff = 0;
        for (int scan = aligned; scan < (int)bgaSize - 4; scan += 4) {
            uint32_t val = *(uint32_t*)(bgaData + scan);
            if (val >= 1 && val <= 5000) {
                numKF = (int)val;
                dataOff = scan + 4;
                break;
            }
        }

        if (numKF > 0 && dataOff > 0) {
            BGALayer* layer = &bga->layers[bga->layerCount];
            strncpy(layer->filename, filename, sizeof(layer->filename) - 1);

            char* dot = strrchr(filename, '.');
            layer->isSPR = (dot && (_stricmp(dot, ".spr") == 0 || _stricmp(dot, ".sp2") == 0));

            layer->kfCount = 0;
            for (int i = 0; i < numKF && layer->kfCount < MAX_BGA_KEYFRAMES; i++) {
                uint32_t ofs = dataOff + i * kfSize;
                if (ofs + (uint32_t)kfSize > bgaSize) break;

                BGAKeyframe* kf = &layer->keyframes[layer->kfCount];

                if (isBGA2) {
                    float* kfData = (float*)(bgaData + ofs);
                    uint32_t ts = *(uint32_t*)(bgaData + ofs + 44);
                    kf->frame = (int)(ts & 0xFFFF);
                    kf->type = (int)((ts >> 16) & 0xFFFF);
                    kf->x = kfData[0];
                    kf->y = kfData[1];
                    kf->hotx = kfData[2];
                    kf->hoty = kfData[3];
                    kf->scaleX = kfData[4];
                    kf->scaleY = kfData[5];
                    kf->rotation = kfData[6];
                    kf->r = kfData[7];
                    kf->g = kfData[8];
                    kf->b = kfData[9];
                    kf->a = kfData[10];
                } else {
                    uint32_t ts = *(uint32_t*)(bgaData + ofs);
                    float* floats = (float*)(bgaData + ofs + 4);
                    kf->frame = (int)(ts & 0xFFFF);
                    kf->type = (int)((ts >> 16) & 0xFFFF);
                    kf->x = floats[0];
                    kf->y = floats[1];
                    kf->hotx = floats[2];
                    kf->hoty = floats[3];
                    kf->scaleX = floats[4];
                    kf->scaleY = floats[5];
                    
                    kf->r = floats[6];
                    kf->g = floats[7];
                    kf->b = floats[8];
                    kf->a = floats[9];
                }
                layer->kfCount++;
            }

            if (layer->kfCount > 0) {
                int maxF = layer->keyframes[layer->kfCount - 1].frame;
                if (maxF > bga->maxFrame) bga->maxFrame = maxF;
            }

            Log_Print("BGA%d direct: '%s' %s kf=%d maxFrame=%d\n",
                bga->version, layer->filename, layer->isSPR ? "SPR" : "TGA", layer->kfCount, bga->maxFrame);

            bga->layerCount++;
            scanStart = dataOff + numKF * kfSize;
        } else {
            break;
        }
    }

    free(bgaData);
    g_game.bgaPicCount++;

    for (int i = 0; i < bga->layerCount; i++) {
        BGALayer* layer = &bga->layers[i];
        const char* srcName = layer->filename;
        if (srcName[0] == '\0') continue;
        if (*srcName == '?') srcName++;
        if (layer->isSPR) {
            layer->sprTileStart = g_game.sprTileCount;
            int aniFrames = loadSPRFromRES(srcName);
            layer->sprTileCount = g_game.sprTileCount - layer->sprTileStart;
            layer->aniFrameCount = aniFrames;
        } else {
            layer->texId = loadTextureFromRES(srcName);
        }
    }

    RES_Close();
    return true;
}

bool Resource_LoadStage(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        if (strlen(line) == 0 || line[0] == ';' || line[0] == '#') continue;

        char key[64], val[192];
        if (sscanf(line, "%63[^=]=%191s", key, val) == 2) {
            if (_stricmp(key, "BGA") == 0) {
                char bgaPath[MAX_PATH];
                snprintf(bgaPath, sizeof(bgaPath), "%s\\BGA\\%s.DAT", g_game.currentDirectory, val);
                Resource_LoadBGADirect(bgaPath);
            } else if (_stricmp(key, "MUSIC") == 0) {
                Log_Print("Resource: music '%s' (not yet implemented)\n", val);
            }
        }
    }

    fclose(f);
    Log_Print("Resource: loaded stage '%s'\n", path);
    return true;
}

void Resource_ClearBGA(void)
{
    BGA_Shutdown();
}

int Resource_SwitchBGA(const char* datName)
{
    Resource_ClearBGA();
    if (!Resource_LoadBGAByName(datName))
    {
        Log_Print("Resource: failed to load BGA '%s'\n", datName);
        return -1;
    }
    BGA_Reset();
    return g_game.bgaPicCount - 1;
}
