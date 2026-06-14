#include "pumpy.h"

extern int g_menuSelection;
static int bga_activePic = -1;

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static BGAKeyframe lerp_kf(BGAKeyframe* a, BGAKeyframe* b, float t) {
    BGAKeyframe r;
    r.x = lerp(a->x, b->x, t);
    r.y = lerp(a->y, b->y, t);
    r.hotx = a->hotx;
    r.hoty = a->hoty;
    r.scaleX = lerp(a->scaleX, b->scaleX, t);
    r.scaleY = lerp(a->scaleY, b->scaleY, t);
    if (a->scaleY == 0.0f && b->scaleY == 0.0f) r.scaleY = r.scaleX;
    else if (a->scaleY == 0.0f) r.scaleY = b->scaleY;
    else if (b->scaleY == 0.0f) r.scaleY = a->scaleY;
    r.rotation = lerp(a->rotation, b->rotation, t);
    r.r = lerp(a->r, b->r, t);
    r.g = lerp(a->g, b->g, t);
    r.b = lerp(a->b, b->b, t);
    r.a = lerp(a->a, b->a, t);
    r.frame = a->frame;
    r.type = a->type;
    r.z_order = b->z_order;
    return r;
}

static BGAKeyframe* interpolate_layer(BGALayer* layer, int frameNum) {
    static BGAKeyframe result;
    if (layer->kfCount == 0) return NULL;
    
    if (frameNum < layer->keyframes[0].frame) return NULL;
    
    int segIdx = -1;
    for (int i = 0; i < layer->kfCount - 1; i++) {
        if (layer->keyframes[i].frame <= frameNum && frameNum <= layer->keyframes[i + 1].frame) {
            segIdx = i;
            if (frameNum < layer->keyframes[i + 1].frame) break;
        }
    }

    if (segIdx < 0) {
        if (frameNum >= layer->keyframes[layer->kfCount - 1].frame) {
            return &layer->keyframes[layer->kfCount - 1];
        }
        return NULL;
    }

    BGAKeyframe* a = &layer->keyframes[segIdx];
    BGAKeyframe* b = &layer->keyframes[segIdx + 1];
    int span = b->frame - a->frame;
    float t = (span > 0) ? (float)(frameNum - a->frame) / span : 0.0f;

    result = lerp_kf(a, b, t);

    if (a->type == 0) result.a = 0;
    else if (b->type == 0 && t >= 1.0f) result.a = 0;

    return &result;
}

#pragma pack(push, 1)
typedef struct {
    float xPos;
    float yPos;
    float hotx;
    float hoty;
    float scaleX;
    float scaleY;
    float rotation;
    float r;
    float g;
    float b;
    float a;
    uint32_t timestamp;
    uint32_t z_order;
} BGA2KFRaw;
#pragma pack(pop)

static bool parseBGA2(const uint8_t* bgaData, uint32_t bgaSize, BGAPicture* pic) {
    if (bgaSize < 4 || memcmp(bgaData, "BGA2", 4) != 0) return false;

    memset(pic, 0, sizeof(BGAPicture));
    pic->version = 2;

    char text[8664];
    memcpy(text, bgaData, bgaSize < 8664 ? bgaSize : 8664);
    text[bgaSize < 8664 ? bgaSize : 8663] = '\0';

    char filename[64];
    int nameStart = -1;
    int nameEnd = -1;

    for (uint32_t i = 4; i + 4 < bgaSize && i < 0x200; i++) {
        char c = bgaData[i];
        if (c == '.' && (i + 4 < bgaSize)) {
            char ext[5];
            memcpy(ext, bgaData + i, 4);
            ext[4] = '\0';
            if (_stricmp(ext, ".spr") == 0 || _stricmp(ext, ".sp2") == 0 ||
                _stricmp(ext, ".tga") == 0 || _stricmp(ext, ".TGA") == 0) {
                int start = (int)i;
                while (start > 0 && bgaData[start - 1] >= 0x20 && bgaData[start - 1] < 0x7F)
                    start--;
                int len = (int)(i + 4 - start);
                if (len < 64) {
                    memcpy(filename, bgaData + start, len);
                    filename[len] = '\0';
                    nameStart = start;
                    nameEnd = i + 4;
                    break;
                }
            }
        }
    }

    if (nameStart < 0) return false;

    for (int ci = 0; filename[ci]; ci++) {
        if (filename[ci] >= 'A' && filename[ci] <= 'Z')
            filename[ci] = (char)(filename[ci] + 32);
    }

    strncpy(pic->name, filename, sizeof(pic->name) - 1);

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

    if (numKF == 0 || dataOff == 0) {
        Log_Print("BGA2: no keyframes found for '%s'\n", filename);
        return false;
    }

    BGALayer* layer = &pic->layers[pic->layerCount];
    strncpy(layer->filename, filename, sizeof(layer->filename) - 1);

    char* ext2 = strrchr(filename, '.');
    layer->isSPR = (ext2 && (_stricmp(ext2, ".spr") == 0 || _stricmp(ext2, ".sp2") == 0));

    int kfSize = 64;
    layer->kfCount = 0;
    for (int i = 0; i < numKF && layer->kfCount < MAX_BGA_KEYFRAMES; i++) {
        uint32_t ofs = dataOff + i * kfSize;
        if (ofs + kfSize > bgaSize) break;

        BGA2KFRaw* raw = (BGA2KFRaw*)(bgaData + ofs);
        BGAKeyframe* kf = &layer->keyframes[layer->kfCount];

        uint32_t ts = raw->timestamp;
        kf->frame = (int)(ts & 0xFFFF);
        kf->type = (int)((ts >> 16) & 0xFFFF);
        kf->x = raw->xPos;
        kf->y = raw->yPos;
        kf->hotx = raw->hotx;
        kf->hoty = raw->hoty;
        kf->scaleX = raw->scaleX;
        kf->scaleY = raw->scaleY;
        kf->rotation = raw->rotation;
        kf->r = raw->r;
        kf->g = raw->g;
        kf->b = raw->b;
        kf->a = raw->a;
        kf->z_order = (int)raw->z_order;

        layer->kfCount++;
    }

        if (layer->kfCount > 0) {
            int maxF = layer->keyframes[layer->kfCount - 1].frame;
            if (maxF > pic->maxFrame) pic->maxFrame = maxF;
        }

    Log_Print("BGA2: layer '%s' %s kf=%d maxFrame=%d\n",
        filename, layer->isSPR ? "SPR" : "TGA", layer->kfCount, pic->maxFrame);

    pic->layerCount++;
    return true;
}

static bool parseBGA0(const uint8_t* bgaData, uint32_t bgaSize);

bool BGA_Load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    uint32_t size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(size);
    if (!data) { fclose(f); return false; }
    fread(data, 1, size, f);
    fclose(f);

    if (size < 4) { free(data); return false; }

    if (memcmp(data, "BGA2", 4) == 0) {
        BGAPicture* pic = &g_game.bgaPics[g_game.bgaPicCount];
        if (parseBGA2(data, size, pic)) {
            g_game.bgaPicCount++;
            bga_activePic = g_game.bgaPicCount - 1;
        }
    } else if (memcmp(data, "BGA", 3) == 0) {
        if (parseBGA0(data, size)) {
            bga_activePic = g_game.bgaPicCount - 1;
        }
    }

    free(data);
    return g_game.bgaPicCount > 0;
}

bool BGA_LoadFromMemory(const uint8_t* data, uint32_t size, bool isBGA2) {
    if (!data || size < 4) return false;
    if (!isBGA2) return false;

    BGAPicture* pic = &g_game.bgaPics[g_game.bgaPicCount];
    if (parseBGA2(data, size, pic)) {
        g_game.bgaPicCount++;
        bga_activePic = g_game.bgaPicCount - 1;
        return true;
    }
    return false;
}

static bool parseBGA0(const uint8_t* bgaData, uint32_t bgaSize) {
    if (bgaSize < 4 || memcmp(bgaData, "BGA", 3) != 0) return false;

    BGAPicture* pic = &g_game.bgaPics[g_game.bgaPicCount];
    memset(pic, 0, sizeof(BGAPicture));
    pic->version = 0;

    int scanStart = 4;
    while (scanStart < (int)bgaSize - 4 && pic->layerCount < MAX_BGA_LAYERS) {
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

        if (pic->layerCount == 0) {
            strncpy(pic->name, filename, sizeof(pic->name) - 1);
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
            BGALayer* layer = &pic->layers[pic->layerCount];
            strncpy(layer->filename, filename, sizeof(layer->filename) - 1);

            char* dot = strrchr(filename, '.');
            layer->isSPR = (dot && (_stricmp(dot, ".spr") == 0 || _stricmp(dot, ".sp2") == 0));

            layer->kfCount = 0;
            for (int i = 0; i < numKF && layer->kfCount < MAX_BGA_KEYFRAMES; i++) {
                uint32_t ofs = dataOff + i * 44;
                if (ofs + 44 > bgaSize) break;

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

                layer->kfCount++;
            }

            if (layer->kfCount > 0) {
                int maxF = layer->keyframes[layer->kfCount - 1].frame;
                if (maxF > pic->maxFrame) pic->maxFrame = maxF;
            }

            Log_Print("BGA0: layer '%s' %s kf=%d maxFrame=%d\n",
                filename, layer->isSPR ? "SPR" : "TGA", layer->kfCount, pic->maxFrame);

            pic->layerCount++;
            scanStart = dataOff + numKF * 44;
        } else {
            break;
        }
    }

    if (pic->layerCount > 0) {
        g_game.bgaPicCount++;
        return true;
    }
    return false;
}

void BGA_Reset(void) {
    bga_activePic = -1;
    if (g_game.bgaPicCount > 0) {
        int maxF = 0;
        for (int p = 0; p < g_game.bgaPicCount; p++) {
            if (g_game.bgaPics[p].maxFrame > maxF)
                maxF = g_game.bgaPics[p].maxFrame;
        }
        g_game.bgaMaxFrame = maxF;
    }
    g_game.bgaFrame = 0;
    g_game.bgaTimer = 0;
    Log_Print("BGA: reset, maxFrame=%d\n", g_game.bgaMaxFrame);
}

static void renderSPTile(SPRTileDef* tile, float posX, float posY, float scaleX, float scaleY, float hotX, float hotY, float rotation, float r, float g, float b, float alpha, int version) {
    if (!tile || tile->texId < 0) return;
    if (alpha <= 0.01f) return;

    float sx = scaleX;
    float sy = scaleY;
    if (sy == 0.0f) sy = sx;
    if (sx == 0.0f) sx = 1.0f;

    float destW = (float)tile->srcW * sx;
    float destH = (float)tile->srcH * sy;
    if (destW <= 0 || destH <= 0) return;

    if (rotation != 0.0f) {
        glPushMatrix();
        glTranslatef(posX, posY, 0.0f);
        glTranslatef(hotX, hotY, 0.0f);
        glRotatef(-rotation, 0.0f, 0.0f, 1.0f);
        glScalef(sx, sy, 1.0f);
        glTranslatef(-hotX, -hotY, 0.0f);

        GLuint glTexId = (tile->texId >= 0 && tile->texId < MAX_TEXTURES && g_game.textures[tile->texId].inUse) ? g_game.textures[tile->texId].id : 0;
        glBindTexture(GL_TEXTURE_2D, glTexId);
        glColor4f(r, g, b, alpha);

        float uvV1 = (float)(256 - tile->v1) / 256.0f;
        float uvV2 = (float)(256 - tile->v2) / 256.0f;

        glBegin(GL_QUADS);
        glTexCoord2f((float)tile->u1 / 256.0f, uvV1);
        glVertex2f((float)tile->srcX, (float)tile->srcY);
        glTexCoord2f((float)tile->u2 / 256.0f, uvV1);
        glVertex2f((float)(tile->srcX + tile->srcW), (float)tile->srcY);
        glTexCoord2f((float)tile->u2 / 256.0f, uvV2);
        glVertex2f((float)(tile->srcX + tile->srcW), (float)(tile->srcY + tile->srcH));
        glTexCoord2f((float)tile->u1 / 256.0f, uvV2);
        glVertex2f((float)tile->srcX, (float)(tile->srcY + tile->srcH));
        glEnd();

        glPopMatrix();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        float drawX = hotX + (tile->srcX + posX - hotX) * sx;
        float drawY = hotY + (tile->srcY + posY - hotY) * sy;
        float uvV1 = (float)(256 - tile->v1);
        float uvV2 = (float)(256 - tile->v2);
        Texture_DrawUV(tile->texId, drawX, drawY, destW, destH,
                       (float)tile->u1, uvV1,
                       (float)tile->u2, uvV2, r, g, b, alpha);
    }
}

bool layerMatchesDirection(BGALayer* layer, int sel) {
    if (sel == 1)
        return strstr(layer->filename, "05.") || strstr(layer->filename, "10.");
    if (sel == 2)
        return strstr(layer->filename, "07.") || strstr(layer->filename, "11.");
    if (sel == 3) {
        if (strstr(layer->filename, "12.")) return true;
        if (strstr(layer->filename, "06.") && layer->kfCount > 0)
            return fabs(layer->keyframes[0].rotation - 90.0f) < 1.0f;
        return false;
    }
    if (sel == 4) {
        if (strstr(layer->filename, "13.")) return true;
        if (strstr(layer->filename, "06.") && layer->kfCount > 0)
            return fabs(layer->keyframes[0].rotation - 180.0f) < 1.0f;
        return false;
    }
    return false;
}

bool isMenuArrowLayer(BGALayer* layer) {
    return strstr(layer->filename, "05.") || strstr(layer->filename, "07.") ||
           (strstr(layer->filename, "06.") && layer->kfCount > 0);
}

bool isMenuTextLayer(BGALayer* layer) {
    return strstr(layer->filename, "10.") || strstr(layer->filename, "11.") ||
           strstr(layer->filename, "12.") || strstr(layer->filename, "13.");
}

bool isMenuCenterLayer(BGALayer* layer) {
    return strstr(layer->filename, "15.") || strstr(layer->filename, "16.");
}

bool isMenuOverlayLayer(BGALayer* layer) {
    return isMenuArrowLayer(layer) || isMenuTextLayer(layer) || isMenuCenterLayer(layer);
}

int findBGALoopStart(void) {
    int loopStart = 999999;
    for (int p = 0; p < g_game.bgaPicCount; p++) {
        BGAPicture* pic = &g_game.bgaPics[p];
        for (int l = 0; l < pic->layerCount; l++) {
            BGALayer* layer = &pic->layers[l];
            if (isMenuOverlayLayer(layer)) continue;
            for (int k = 0; k < layer->kfCount; k++) {
                BGAKeyframe* kf = &layer->keyframes[k];
                if (kf->type != 0 && kf->a > 0.01f) {
                    if (kf->frame < loopStart) loopStart = kf->frame;
                    break;
                }
            }
        }
    }
    return (loopStart > 999990) ? 0 : loopStart;
}

int findBGALoopEnd(void) {
    int firstHidden = 999999;
    for (int p = 0; p < g_game.bgaPicCount; p++) {
        BGAPicture* pic = &g_game.bgaPics[p];
        for (int l = 0; l < pic->layerCount; l++) {
            BGALayer* layer = &pic->layers[l];
            if (isMenuOverlayLayer(layer)) continue;
            int layerFirstHidden = 999999;
            for (int k = 0; k < layer->kfCount; k++) {
                BGAKeyframe* kf = &layer->keyframes[k];
                if (kf->type == 0 || kf->a <= 0.01f) {
                    layerFirstHidden = kf->frame;
                    break;
                }
            }
            if (layerFirstHidden == 0) continue;
            if (layerFirstHidden < firstHidden) firstHidden = layerFirstHidden;
        }
    }
    if (firstHidden > 999990) return g_game.bgaMaxFrame;
    return firstHidden - 1;
}

static void renderOneLayer(BGALayer* layer, BGAKeyframe* state, int picVersion) {
    if (!state || state->type == 0 || state->a <= 0.01f) return;
    float alpha = state->a;
    float renderR = state->r, renderG = state->g, renderB = state->b;

    if (layer->isSPR) {
            if (layer->aniFrameCount > 1) {
            int aniIdx = (g_game.frameCounter / 2) % layer->aniFrameCount;
            if (aniIdx < layer->sprTileCount) {
                int tileIdx = layer->sprTileStart + aniIdx;
                if (tileIdx >= 0 && tileIdx < g_game.sprTileCount) {
                    SPRTileDef* tile = &g_game.sprTiles[tileIdx];
                    renderSPTile(tile, state->x, state->y, state->scaleX, state->scaleY, state->hotx, state->hoty, state->rotation, renderR, renderG, renderB, alpha, picVersion);
                }
            }
        } else {
            for (int t = layer->sprTileCount - 1; t >= 0; t--) {
                int tileIdx = layer->sprTileStart + t;
                if (tileIdx < 0 || tileIdx >= g_game.sprTileCount) continue;
                SPRTileDef* tile = &g_game.sprTiles[tileIdx];
                renderSPTile(tile, state->x, state->y, state->scaleX, state->scaleY, state->hotx, state->hoty, state->rotation, renderR, renderG, renderB, alpha, picVersion);
            }
        }
    } else if (layer->texId >= 0) {
        Texture* tex = &g_game.textures[layer->texId];
        if (!tex->inUse) return;
        float sclY = (state->scaleY != 0.0f) ? state->scaleY : state->scaleX;
        glPushMatrix();
        glTranslatef(state->x + state->hotx, state->y + state->hoty, 0.0f);
        glScalef(state->scaleX, sclY, 1.0f);
        glBindTexture(GL_TEXTURE_2D, tex->id);
        glColor4f(renderR, renderG, renderB, alpha);
        glBegin(GL_QUADS);
        float hx = state->hotx;
        float hy = state->hoty;
        glTexCoord2f(0, 1); glVertex2f(0 - hx, 0 - hy);
        glTexCoord2f(1, 1); glVertex2f(tex->width - hx, 0 - hy);
        glTexCoord2f(1, 0); glVertex2f(tex->width - hx, tex->height - hy);
        glTexCoord2f(0, 0); glVertex2f(0 - hx, tex->height - hy);
        glEnd();
        glPopMatrix();
        glColor4f(1, 1, 1, 1);
    }
}

void BGA_SetEventLayer(int bgaIndex, int frame, int layerIdx) {
    if (bgaIndex < 0 || bgaIndex >= g_game.bgaPicCount) return;
    BGAPicture* pic = &g_game.bgaPics[bgaIndex];
    if (layerIdx < 0 || layerIdx >= pic->layerCount) return;

    BGALayer* layer = &pic->layers[layerIdx];
    if (layer->kfCount == 0) return;

    BGAKeyframe* state = interpolate_layer(layer, frame);
    if (!state) return;

    if (state->type == 0 || state->a <= 0.01f) return;
    renderOneLayer(layer, state, pic->version);
}

void BGA_SetEventFrame(int bgaIndex, int frame) {
    if (bgaIndex < 0 || bgaIndex >= g_game.bgaPicCount) return;
    BGAPicture* pic = &g_game.bgaPics[bgaIndex];

    for (int i = 0; i < pic->layerCount; i++) {
        BGA_SetEventLayer(bgaIndex, frame, i);
    }
}

void BGA_Render(int bgaIndex, int frame) {
    bool isMenu = (g_game.state == STATE_RESET_FLOW ||
                   g_game.state == STATE_MENU_FADE_IN ||
                   g_game.state == STATE_MENU_IDLE ||
                   g_game.state == STATE_MENU_INPUT_WAIT ||
                   g_game.state == STATE_MENU_ENTER ||
                   g_game.state == STATE_MENU_INPUT);

    if (isMenu) {
        Gamestate_RenderMenu(bgaIndex, frame);
    } else {
        BGA_SetEventFrame(bgaIndex, frame);
    }
}

void BGA_Shutdown(void) {
    g_game.sprTileCount = 0;
    memset(g_game.sprTiles, 0, sizeof(g_game.sprTiles));
    bga_activePic = -1;
    g_game.bgaPicCount = 0;
    g_game.bgaFrame = 0;
    g_game.bgaMaxFrame = 0;
    g_game.bgaTimer = 0;
    g_game.bgaLoop = false;
    g_game.bgaLoopStart = 0;
    g_game.bgaLoopEnd = 0;
    memset(g_game.bgaPics, 0, sizeof(g_game.bgaPics));
    Log_Print("BGA: shutdown\n");
}