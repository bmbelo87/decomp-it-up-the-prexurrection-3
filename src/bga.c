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

static BGAKeyframe* interpolate_layer(BGALayer* layer, int frameNum, float* outAnimT) {
    static BGAKeyframe result;
    if (outAnimT) *outAnimT = 0.0f;
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
            return NULL;
        }
        return NULL;
    }

    BGAKeyframe* a = &layer->keyframes[segIdx];
    BGAKeyframe* b = &layer->keyframes[segIdx + 1];
    int span = b->frame - a->frame;
    float t = (span > 0) ? (float)(frameNum - a->frame) / span : 0.0f;

    result = lerp_kf(a, b, t);
    /* blendMode vem do keyframe A (inicio do segmento atual) — igual ao original:
       BGA_RenderLayer usa *(short*)(pfVar1 - 4) onde pfVar1 aponta para B, logo A+0x30 */
    result.blendMode = layer->keyframes[segIdx].blendMode;

    if (a->type == 0) result.a = 0;
    else if (b->type == 0 && t >= 1.0f) result.a = 0;

    if (outAnimT) *outAnimT = t;
    return &result;
}

static bool parseBGA2(const uint8_t* bgaData, uint32_t bgaSize, BGAPicture* pic) {
    if (bgaSize < 4 || memcmp(bgaData, "BGA2", 4) != 0) return false;

    memset(pic, 0, sizeof(BGAPicture));
    pic->version = 2;

    int entryPos = 16;
    while (entryPos + 64 + 4 <= (int)bgaSize && pic->layerCount < MAX_BGA_LAYERS) {
        char filename[64];
        memcpy(filename, bgaData + entryPos, 64);
        filename[63] = '\0';

        int nameLen = (int)strlen(filename);
        int count = *(int*)(bgaData + entryPos + 64);
        /* Original FUN_00401200: if (*piVar2 < 0) { *piVar2 = 1; } */
        if (count < 0) count = 1;

        if (nameLen == 0 && count == 0) {
            entryPos += 68;
            continue;
        }

        for (int ci = 0; filename[ci]; ci++) {
            if (filename[ci] >= 'A' && filename[ci] <= 'Z')
                filename[ci] = (char)(filename[ci] + 32);
        }

        BGALayer* layer = &pic->layers[pic->layerCount];
        strncpy(layer->filename, filename, sizeof(layer->filename) - 1);

        char* dot = strrchr(filename, '.');
        layer->isSPR = (dot && (_stricmp(dot, ".spr") == 0 || _stricmp(dot, ".sp2") == 0));

        if (pic->layerCount == 0) {
            strncpy(pic->name, filename, sizeof(pic->name) - 1);
        }

        int dataOff = entryPos + 68;
        int readCount = count;
        if (readCount > MAX_BGA_KEYFRAMES)
            readCount = MAX_BGA_KEYFRAMES;
        layer->kfCount = 0;

        for (int i = 0; i < readCount && layer->kfCount < MAX_BGA_KEYFRAMES; i++) {
            uint32_t ofs = dataOff + i * 64;
            if (ofs + 64 > bgaSize) break;

            uint32_t ts = *(uint32_t*)(bgaData + ofs + 0x2c);
            BGAKeyframe* kf = &layer->keyframes[layer->kfCount];

            /* BGA2: frame é int16_t signed (valores negativos = "ativo desde antes do frame 0") */
            kf->frame = (int)(int16_t)(ts & 0xFFFF);
            kf->type = (int)((ts >> 16) & 0xFFFF);
            kf->x = *(float*)(bgaData + ofs + 0x00);
            kf->y = *(float*)(bgaData + ofs + 0x04);
            kf->hotx = *(float*)(bgaData + ofs + 0x08);
            kf->hoty = *(float*)(bgaData + ofs + 0x0c);
            kf->scaleX = *(float*)(bgaData + ofs + 0x10);
            kf->scaleY = *(float*)(bgaData + ofs + 0x14);
            kf->rotation = *(float*)(bgaData + ofs + 0x18);
            kf->r = *(float*)(bgaData + ofs + 0x1c);
            kf->g = *(float*)(bgaData + ofs + 0x20);
            kf->b = *(float*)(bgaData + ofs + 0x24);
            kf->a = *(float*)(bgaData + ofs + 0x28);
            kf->blendMode = (int)*(int16_t*)(bgaData + ofs + 0x30);
            kf->z_order = (int)*(int32_t*)(bgaData + ofs + 0x34);

            layer->kfCount++;
        }

        if (layer->kfCount > 0) {
            int maxF = layer->keyframes[layer->kfCount - 1].frame;
            if (maxF > pic->maxFrame) pic->maxFrame = maxF;
        }

        Log_Print("BGA2: '%s' %s kf=%d maxFrame=%d\n",
            filename, layer->isSPR ? "SPR" : "TGA", layer->kfCount, pic->maxFrame);

        pic->layerCount++;
        entryPos += 68 + count * 64;
    }

    if (pic->layerCount > 0) {
        return true;
    }
    return false;
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

    // BGA v0 header: 4 bytes magic + 12 bytes unknown = 16 bytes total
    // Fixed-record format: [64 bytes name][4 bytes count][count * 44 bytes keyframes]
    // Matches original EXE's sequential file read (FUN_00401020)
    BGAPicture* pic = &g_game.bgaPics[g_game.bgaPicCount];
    memset(pic, 0, sizeof(BGAPicture));
    pic->version = 0;

    int entryPos = 16; // skip 16-byte header
    while (entryPos + 64 + 4 <= (int)bgaSize && pic->layerCount < MAX_BGA_LAYERS) {
        char filename[64];
        memcpy(filename, bgaData + entryPos, 64);
        filename[63] = '\0';

        int nameLen = (int)strlen(filename);
        if (nameLen == 0) {
            int count = *(int*)(bgaData + entryPos + 64);
            entryPos += 68 + count * 44;
            continue;
        }

        int count = *(int*)(bgaData + entryPos + 64);
        if (count == 0) {
            entryPos += 68;
            continue;
        }

        for (int ci = 0; filename[ci]; ci++) {
            if (filename[ci] >= 'A' && filename[ci] <= 'Z')
                filename[ci] = (char)(filename[ci] + 32);
        }

        BGALayer* layer = &pic->layers[pic->layerCount];
        strncpy(layer->filename, filename, sizeof(layer->filename) - 1);

        char* dot = strrchr(filename, '.');
        layer->isSPR = (dot && (_stricmp(dot, ".spr") == 0 || _stricmp(dot, ".sp2") == 0));

        if (pic->layerCount == 0) {
            strncpy(pic->name, filename, sizeof(pic->name) - 1);
        }

        int dataOff = entryPos + 68;
        layer->kfCount = 0;
        for (int i = 0; i < count && layer->kfCount < MAX_BGA_KEYFRAMES; i++) {
            uint32_t ofs = dataOff + i * 44;
            if (ofs + 44 > bgaSize) break;

            int16_t frame = *(int16_t*)(bgaData + ofs);
            int16_t spriteID = *(int16_t*)(bgaData + ofs + 2);
            float* floats = (float*)(bgaData + ofs + 4);
            BGAKeyframe* kf = &layer->keyframes[layer->kfCount];

            kf->frame = frame;
            kf->type = (spriteID != 0) ? 1 : 0;
            kf->x = floats[0];
            kf->y = floats[1];
            kf->hotx = floats[2];
            kf->hoty = floats[3];
            kf->scaleX = floats[4];
            kf->scaleY = floats[4];
            kf->rotation = floats[5];
            kf->r = floats[6];
            kf->g = floats[7];
            kf->b = floats[8];
            kf->a = floats[9];
            kf->blendMode = 0;

            layer->kfCount++;
        }

        if (layer->kfCount > 0) {
            int maxF = layer->keyframes[layer->kfCount - 1].frame;
            if (maxF > pic->maxFrame) pic->maxFrame = maxF;
        }

        Log_Print("BGA0: '%s' %s kf=%d maxFrame=%d\n",
            filename, layer->isSPR ? "SPR" : "TGA", layer->kfCount, pic->maxFrame);

        pic->layerCount++;
        entryPos += 68 + count * 44;
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

static void drawTileQuad(SPRTileDef* tile, float offsetX, float offsetY) {
    GLuint glTexId = (tile->texId >= 0 && tile->texId < MAX_TEXTURES && g_game.textures[tile->texId].inUse) ? g_game.textures[tile->texId].id : 0;
    /* DBG: loga hall.spr uma vez na primeira chamada */
    if (strstr(tile->name, "hall.spr")) {
        static int hallDbgOnce = 0;
        if (!hallDbgOnce) {
            hallDbgOnce = 1;
            Log_Print("HALL drawTileQuad: texId=%d glTexId=%u inUse=%d u1=%.3f u2=%.3f v1=%.3f v2=%.3f srcX=%d srcY=%d srcW=%d srcH=%d\n",
                tile->texId, glTexId,
                (tile->texId >= 0 && tile->texId < MAX_TEXTURES) ? g_game.textures[tile->texId].inUse : -1,
                tile->u1, tile->u2, tile->v1, tile->v2, tile->srcX, tile->srcY, tile->srcW, tile->srcH);
        }
    }
    if (!glTexId) {
        if (strstr(tile->name, "hall.spr")) {
            Log_Print("HALL drawTileQuad: EARLY RETURN glTexId=0 texId=%d\n", tile->texId);
        }
        return;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, glTexId);

    float u1 = tile->u1;
    float v1 = tile->v1;
    float u2 = tile->u2;
    float v2 = tile->v2;

    glBegin(GL_QUADS);
    glTexCoord2f(u1, v1);
    glVertex2f(offsetX + (float)tile->srcX, 480.0f - (offsetY + (float)tile->srcY));
    glTexCoord2f(u2, v1);
    glVertex2f(offsetX + (float)(tile->srcX + tile->srcW), 480.0f - (offsetY + (float)tile->srcY));
    glTexCoord2f(u2, v2);
    glVertex2f(offsetX + (float)(tile->srcX + tile->srcW), 480.0f - (offsetY + (float)(tile->srcY + tile->srcH)));
    glTexCoord2f(u1, v2);
    glVertex2f(offsetX + (float)tile->srcX, 480.0f - (offsetY + (float)(tile->srcY + tile->srcH)));
    glEnd();
}

static void renderSPTile(SPRTileDef* tile, float posX, float posY, float scaleX, float scaleY, float hotX, float hotY, float rotation, float r, float g, float b, float alpha) {
    bool isHall = tile && strstr(tile->name, "hall.spr");
    if (!tile || tile->texId < 0) {
        if (isHall) Log_Print("HALL renderSPTile: RETURN tile=%p texId=%d\n", (void*)tile, tile ? tile->texId : -99);
        return;
    }
    if (alpha <= 0.01f) {
        if (isHall) Log_Print("HALL renderSPTile: RETURN alpha=%.4f\n", alpha);
        return;
    }

    float sx = scaleX;
    float sy = scaleY;
    if (sy == 0.0f) sy = sx;
    if (sx == 0.0f) sx = 1.0f;

    float destW = (float)tile->srcW * sx;
    float destH = (float)tile->srcH * sy;
    /* Usar valor absoluto: scaleX/scaleY negativo = flip horizontal/vertical (não bloquear) */
    if (fabsf(destW) <= 0.001f || fabsf(destH) <= 0.001f) {
        if (isHall) Log_Print("HALL renderSPTile: RETURN destW=%.1f destH=%.1f sx=%.3f sy=%.3f\n", destW, destH, sx, sy);
        return;
    }

    GLuint glTexId = (tile->texId >= 0 && tile->texId < MAX_TEXTURES && g_game.textures[tile->texId].inUse) ? g_game.textures[tile->texId].id : 0;
    if (!glTexId) {
        if (isHall) Log_Print("HALL renderSPTile: RETURN glTexId=0 texId=%d inUse=%d\n",
            tile->texId, (tile->texId >= 0 && tile->texId < MAX_TEXTURES) ? g_game.textures[tile->texId].inUse : -1);
        return;
    }
    if (isHall) {
        static int hallSpTileOnce = 0;
        if (!hallSpTileOnce) {
            hallSpTileOnce = 1;
            Log_Print("HALL renderSPTile: OK alpha=%.3f sx=%.3f sy=%.3f glTexId=%u posX=%.1f posY=%.1f\n",
                alpha, sx, sy, glTexId, posX, posY);
        }
    }

    glPushMatrix();
    glTranslatef(0.0f, 480.0f, 0.0f);
    glTranslatef(posX, -posY, 0.0f);
    glTranslatef(hotX, -hotY, 0.0f);
    if (rotation != 0.0f)
        glRotatef(rotation, 0.0f, 0.0f, 1.0f);
    glScalef(sx, sy, 1.0f);
    glTranslatef(-hotX, hotY, 0.0f);
    glTranslatef(0.0f, -480.0f, 0.0f);

    glColor4f(r, g, b, alpha);
    drawTileQuad(tile, 0.0f, 0.0f);

    glPopMatrix();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
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

static void renderOneLayer(BGALayer* layer, BGAKeyframe* state, int picVersion, float animT) {
    if (!state || state->type == 0 || state->a <= 0.01f) return;
    float alpha = state->a;
    float renderR = state->r, renderG = state->g, renderB = state->b;

    glEnable(GL_BLEND);
    /* FUN_00401450 no original: 5 modos de blend
       0=alpha normal, 1=additive, 2=multiply, 3=dst_color, 4=inv_dst_color */
    switch (state->blendMode) {
        case 1:  glBlendFunc(GL_SRC_ALPHA, GL_ONE);                        break; /* additive    */
        case 2:  glBlendFunc(GL_ZERO, GL_SRC_COLOR);                       break; /* multiply    */
        case 3:  glBlendFunc(GL_DST_COLOR, GL_SRC_ALPHA);                  break; /* dst_color   */
        case 4:  glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break; /* inv_dst  */
        default: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);        break; /* alpha normal */
    }

    if (layer->isSPR) {
        if (layer->patCols > 0 && layer->patRows > 0 && layer->sprTileCount > 0) {
            int cellW = g_game.sprTiles[layer->sprTileStart].srcW;
            int cellH = g_game.sprTiles[layer->sprTileStart].srcH;
            int patOffset = Math_ROUND(layer->sprTileCount * animT);

            float sx = state->scaleX;
            float sy = state->scaleY;
            if (sy == 0.0f) sy = sx;
            if (sx == 0.0f) sx = 1.0f;

            // Apply full transform (position + hotspot + scale + rotation) ONCE for entire grid
            glPushMatrix();
            glTranslatef(0.0f, 480.0f, 0.0f);
            glTranslatef(state->x, -state->y, 0.0f);
            glTranslatef(state->hotx, -state->hoty, 0.0f);
            if (state->rotation != 0.0f)
                glRotatef(state->rotation, 0.0f, 0.0f, 1.0f);
            glScalef(sx, sy, 1.0f);
            glTranslatef(-state->hotx, state->hoty, 0.0f);
            glTranslatef(0.0f, -480.0f, 0.0f);
            glColor4f(renderR, renderG, renderB, alpha);

            if (layer->patFlags == 0) {
                int tileAccum = 0;
                for (int r = 0; r < layer->patRows; r++) {
                    for (int c = 0; c < layer->patCols; c++) {
                        int idx = (patOffset + tileAccum + c) % layer->sprTileCount;
                        int tileIdx = layer->sprTileStart + idx;
                        if (tileIdx < 0 || tileIdx >= g_game.sprTileCount) continue;
                        SPRTileDef* tile = &g_game.sprTiles[tileIdx];
                        drawTileQuad(tile, (float)(c * cellW), (float)(r * cellH));
                    }
                    tileAccum += layer->patCols;
                }
            } else {
                int tileAccum = 0;
                for (int c = 0; c < layer->patCols; c++) {
                    for (int r = 0; r < layer->patRows; r++) {
                        int idx = (patOffset + tileAccum + r) % layer->sprTileCount;
                        int tileIdx = layer->sprTileStart + idx;
                        if (tileIdx < 0 || tileIdx >= g_game.sprTileCount) continue;
                        SPRTileDef* tile = &g_game.sprTiles[tileIdx];
                        drawTileQuad(tile, (float)(c * cellW), (float)(r * cellH));
                    }
                    tileAccum += layer->patCols;
                }
            }
            glPopMatrix();
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        } else if (layer->aniFrameCount > 1 && layer->sprTileCount > 1) {
            int aniIdx = Math_ROUND(layer->aniFrameCount * animT);
            if (aniIdx < 0) aniIdx = 0;
            if (aniIdx >= layer->sprTileCount) aniIdx = layer->sprTileCount - 1;
            int tileIdx = layer->sprTileStart + aniIdx;
            if (tileIdx >= 0 && tileIdx < g_game.sprTileCount) {
                SPRTileDef* tile = &g_game.sprTiles[tileIdx];
                renderSPTile(tile, state->x, state->y, state->scaleX, state->scaleY, state->hotx, state->hoty, state->rotation, renderR, renderG, renderB, alpha);
            }
        } else {
            for (int t = layer->sprTileCount - 1; t >= 0; t--) {
                int tileIdx = layer->sprTileStart + t;
                if (tileIdx < 0 || tileIdx >= g_game.sprTileCount) continue;
                SPRTileDef* tile = &g_game.sprTiles[tileIdx];
                renderSPTile(tile, state->x, state->y, state->scaleX, state->scaleY, state->hotx, state->hoty, state->rotation, renderR, renderG, renderB, alpha);
            }
        }
    } else if (layer->texId >= 0) {
        Texture* tex = &g_game.textures[layer->texId];
        if (!tex->inUse) {
            /* early return: reseta blend func para nao vazar modo aditivo/multiply */
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            return;
        }
        float sx = state->scaleX;
        float sy = state->scaleY;
        if (sy == 0.0f) sy = sx;
        if (sx == 0.0f) sx = 1.0f;
        glPushMatrix();
        glTranslatef(0.0f, 480.0f, 0.0f);
        glTranslatef(state->x, -state->y, 0.0f);
        glTranslatef(state->hotx, -state->hoty, 0.0f);
        if (state->rotation != 0.0f)
            glRotatef(state->rotation, 0.0f, 0.0f, 1.0f);
        glScalef(sx, sy, 1.0f);
        glTranslatef(-state->hotx, state->hoty, 0.0f);
        glTranslatef(0.0f, -480.0f, 0.0f);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex->id);
        glColor4f(renderR, renderG, renderB, alpha);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 1);
        glVertex2f(0, 480.0f);
        glTexCoord2f(1, 1);
        glVertex2f((float)tex->width, 480.0f);
        glTexCoord2f(1, 0);
        glVertex2f((float)tex->width, 480.0f - (float)tex->height);
        glTexCoord2f(0, 0);
        glVertex2f(0, 480.0f - (float)tex->height);
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

    float animT = 0.0f;
    BGAKeyframe* state = interpolate_layer(layer, frame, &animT);

    /* DBG: loga estado de hall.spr nas transições importantes */
    if (strstr(layer->filename, "hall")) {
        static int lastHallFrame = -99;
        /* loga na primeira vez e depois em frames chave */
        int doLog = (lastHallFrame == -99) ||
                    (frame <= 120 && frame != lastHallFrame && (frame % 10 == 0)) ||
                    (frame == 56) || (frame == 57) || (frame == 172);
        if (doLog) {
            lastHallFrame = frame;
            Log_Print("HALL DBG frame=%d: kfCount=%d sprTileStart=%d sprTileCount=%d aniFC=%d state=%s alpha=%.3f type=%d\n",
                frame, layer->kfCount, layer->sprTileStart, layer->sprTileCount, layer->aniFrameCount,
                state ? "OK" : "NULL",
                state ? state->a : -1.0f,
                state ? state->type : -1);
        }
    }

    if (!state) return;

    if (state->type == 0 || state->a <= 0.01f) return;
    renderOneLayer(layer, state, pic->version, animT);

    /* Reset estado GL apos cada layer — igual ao original: FUN_00401450(0) apenas
       reseta a blend FUNC para modo normal, mantendo GL_BLEND habilitado (o jogo
       assume blend sempre ativo). Desativa textura e reseta cor para nao vazar. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void BGA_SetEventFrame(int bgaIndex, int frame) {
    if (bgaIndex < 0 || bgaIndex >= g_game.bgaPicCount) return;
    BGAPicture* pic = &g_game.bgaPics[bgaIndex];

    for (int i = 0; i < pic->layerCount; i++) {
        BGA_SetEventLayer(bgaIndex, frame, i);
    }
}

void BGA_Render(int bgaIndex, int frame) {
    bool isMenu = (g_game.state == STATE_MENU_ENTER ||
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