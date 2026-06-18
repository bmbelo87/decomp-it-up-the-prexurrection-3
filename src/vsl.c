#include "vsl.h"
#include "df_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VSLScreen g_vsl;

static float vslFrameTimer = 0.0f;

static const uint8_t VSL_SIGNATURE[VSL_SIG_LEN] = {
    0x66, 0x61, 0x21, 0x5E, 0x60, 0x3B, 0x5F, 0x7D,
    0x3C, 0x3D, 0x20, 0x7E, 0x2E, 0x2E, 0x7E
};

bool VSL_IsVSL(const char* datPath) {
    FILE* f = fopen(datPath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0x48) { fclose(f); return false; }

    uint8_t magic[2];
    fread(magic, 1, 2, f);
    if (magic[0] != 'd' && magic[0] != 's') { fclose(f); return false; }
    if (magic[1] != 'f' && magic[1] != 'd') { fclose(f); return false; }

    fseek(f, 0x38, SEEK_SET);
    uint8_t sig[VSL_SIG_LEN];
    fread(sig, 1, VSL_SIG_LEN, f);
    fclose(f);

    if (memcmp(sig, VSL_SIGNATURE, VSL_SIG_LEN) == 0) {
        Log_Print("VSL: detected VSL signature in '%s'\n", datPath);
        return true;
    }
    return false;
}

static int vsl_find_tc(const char* name) {
    for (int i = 0; i < g_vsl.tcCount; i++) {
        if (!g_vsl.tcActive[i]) continue;
        if (_stricmp(g_vsl.tcNames[i], name) == 0) return i;
    }
    return -1;
}

static int vsl_alloc_tc_slot(void) {
    for (int i = 0; i < 500; i++) {
        if (!g_vsl.tcActive[i]) return i;
    }
    return -1;
}

typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t pos;
} TCStream;

static uint32_t tc_read_u32(TCStream* s) {
    uint32_t v = 0;
    if (s->pos + 4 <= s->size) {
        v = *(uint32_t*)(s->data + s->pos);
        s->pos += 4;
    }
    return v;
}

static float tc_read_f32(TCStream* s) {
    float v = 0;
    if (s->pos + 4 <= s->size) {
        v = *(float*)(s->data + s->pos);
        s->pos += 4;
    }
    return v;
}

static bool tc_read_name(TCStream* s, char* buf) {
    if (s->pos + 32 > s->size) return false;
    memcpy(buf, s->data + s->pos, 32);
    buf[31] = '\0';
    s->pos += 32;
    return true;
}

static int vsl_register_material_stream(const char* matName, TCStream* s) {
    int existing = vsl_find_tc(matName);
    if (existing >= 0) {
        uint32_t d8 = tc_read_u32(s);
        uint32_t d0 = tc_read_u32(s);
        if (d8 != 0) {
            uint32_t d4 = tc_read_u32(s);
            if (d4 > 200) d4 = 200;
            s->pos += d4 * 64;
        }
        uint32_t matCount = tc_read_u32(s);
        if (matCount > 256) matCount = 256;
        s->pos += matCount * 68;
        s->pos += 16;
        return existing;
    }

    int idx = vsl_alloc_tc_slot();
    if (idx < 0) return -1;

    TCMaterial* mat = &g_vsl.tcMaterials[idx];
    memset(mat, 0, sizeof(TCMaterial));
    strncpy(mat->name, matName, sizeof(mat->name) - 1);

    uint32_t d8 = tc_read_u32(s);
    uint32_t d0 = tc_read_u32(s);
    mat->hasAnimation = (d8 != 0) ? 1 : 0;
    mat->flags = d0;

    if (d8 != 0) {
        uint32_t d4 = tc_read_u32(s);
        if (d4 > 200) d4 = 200;
        if (d4 > 0 && s->pos + 64 <= s->size) {
            tc_read_name(s, mat->animName1);
            tc_read_name(s, mat->animName2);
        }
        if (d4 > 1) {
            s->pos += (d4 - 1) * 64;
        }
    }

    uint32_t matCount = tc_read_u32(s);
    if (matCount > 256) matCount = 256;
    mat->materialCount = matCount;

    if (matCount > 0) {
        mat->ambient = (float*)malloc(matCount * 4 * sizeof(float));
        mat->diffuse = (float*)malloc(matCount * 4 * sizeof(float));
        mat->specular = (float*)malloc(matCount * 4 * sizeof(float));
        mat->emissive = (float*)malloc(matCount * 4 * sizeof(float));
        mat->shininess = (float*)malloc(matCount * sizeof(float));

        if (matCount * 16 <= s->size - s->pos) memcpy(mat->ambient, s->data + s->pos, matCount * 16);
        s->pos += matCount * 16;
        if (matCount * 16 <= s->size - s->pos) memcpy(mat->diffuse, s->data + s->pos, matCount * 16);
        s->pos += matCount * 16;
        if (matCount * 16 <= s->size - s->pos) memcpy(mat->specular, s->data + s->pos, matCount * 16);
        s->pos += matCount * 16;
        if (matCount * 16 <= s->size - s->pos) memcpy(mat->emissive, s->data + s->pos, matCount * 16);
        s->pos += matCount * 16;
        if (matCount * 4 <= s->size - s->pos) memcpy(mat->shininess, s->data + s->pos, matCount * 4);
        s->pos += matCount * 4;
    }

    mat->texWidth = tc_read_u32(s);
    mat->texHeight = tc_read_u32(s);
    mat->scaleU = tc_read_f32(s);
    mat->scaleV = tc_read_f32(s);

    g_vsl.tcActive[idx] = 1;
    strncpy(g_vsl.tcNames[idx], matName, 31);
    g_vsl.tcNames[idx][31] = '\0';
    g_vsl.tcCount = (idx + 1 > g_vsl.tcCount) ? idx + 1 : g_vsl.tcCount;

    Log_Print("VSL: mat '%s' idx=%d mats=%d anim=%d anim1='%s' anim2='%s'\n", matName, idx, matCount, mat->hasAnimation, mat->animName1, mat->animName2);
    return idx;
}

static bool vsl_load_tc(const char* tcName, int meshTableIdx) {
    int resIdx = DF_Find(tcName);
    if (resIdx < 0) {
        Log_Print("VSL: TC '%s' not found\n", tcName);
        return false;
    }

    uint32_t tcSize = DF_GetSize(resIdx);
    uint8_t* tcData = DF_ReadAlloc(resIdx);
    if (!tcData) return false;

    TCStream s;
    s.data = tcData; s.size = tcSize; s.pos = 0;

    uint32_t matCount = tc_read_u32(&s);
    if (matCount > TC_MAX_SUBMESHES) matCount = TC_MAX_SUBMESHES;

    for (uint32_t i = 0; i < matCount; i++) {
        char matName[32];
        if (!tc_read_name(&s, matName)) break;
        vsl_register_material_stream(matName, &s);
    }

    uint32_t field1 = tc_read_u32(&s);
    uint32_t field2 = tc_read_u32(&s);
    uint32_t groupCount = tc_read_u32(&s);

    Log_Print("VSL: TC '%s' f1=%d f2=%d gc=%d pos=%d/%d\n", tcName, field1, field2, groupCount, s.pos, tcSize);

    if (groupCount < 1 || groupCount > VSL_MAX_MESHES) {
        Log_Print("VSL: TC '%s' bad groupCount %d\n", tcName, groupCount);
        free(tcData);
        return false;
    }

    VSLMeshTable* mt = &g_vsl.meshTables[meshTableIdx];
    strncpy(mt->name, tcName, sizeof(mt->name) - 1);
    mt->groupStart = 0;
    mt->groupCount = groupCount;
    mt->materialCount = matCount;
    mt->meshGroups = (TCMeshEntry*)calloc(groupCount, sizeof(TCMeshEntry));
    mt->meshGroupCount = (int)groupCount;
    if (!mt->meshGroups) { free(tcData); return false; }

    for (uint32_t g = 0; g < groupCount; g++) {
        if (s.pos + 4 > s.size) break;
        uint32_t subCount = tc_read_u32(&s);
        if (subCount > TC_MAX_SUBMESHES) { Log_Print("VSL: TC '%s' g%d bad subCount %d\n", tcName, g, subCount); break; }

        TCMeshEntry* me = &mt->meshGroups[g];
        me->count = (int)subCount;
        me->groups = (TCSubGroup*)calloc(subCount, sizeof(TCSubGroup));
        me->faceCounts = (int*)calloc(subCount, sizeof(int));

        for (uint32_t i = 0; i < subCount; i++) {
            if (!tc_read_name(&s, me->groups[i].name)) break;
        }

        me->totalVertices = 0;
        for (uint32_t i = 0; i < subCount; i++) {
            me->faceCounts[i] = (int)tc_read_u32(&s);
            if (me->faceCounts[i] > TC_MAX_VERTICES_PER_MESH) me->faceCounts[i] = TC_MAX_VERTICES_PER_MESH;
            me->totalVertices += me->faceCounts[i];
        }

        if (me->totalVertices > 0 && me->totalVertices < TC_MAX_VERTICES) {
            uint32_t vsize = (uint32_t)me->totalVertices * 20;
            if (s.pos + vsize <= s.size) {
                me->vertices = (TCVertex*)malloc(me->totalVertices * sizeof(TCVertex));
                if (me->vertices) {
                    for (int i = 0; i < me->totalVertices; i++) {
                        float* f = (float*)(s.data + s.pos + i * 20);
                        me->vertices[i] = (TCVertex){f[0], f[1], f[2], f[3], f[4]};
                    }
                }
            }
            s.pos += vsize;
        }

        me->animCount = (int)tc_read_u32(&s);
        if (me->animCount > TC_MAX_ANIMS) me->animCount = TC_MAX_ANIMS;
        if (me->animCount > 0) {
            me->anims = (TCAnimKeyframe*)calloc(me->animCount, sizeof(TCAnimKeyframe));
            if (me->anims) {
                for (int i = 0; i < me->animCount && s.pos + 48 <= s.size; i++) {
                    float* f = (float*)(s.data + s.pos);
                    
                    // CORREÇÃO DE MATRIZ (Transposição Row-Major -> Column-Major)
                    // Layout do arquivo (Compactado 3x4 + T?):
                    // f[0]..f[8]: Rotação/Escala (3x3)
                    // f[9]..f[11]: Translação (Tx, Ty, Tz)
                    
                    // Coluna 0
                    me->anims[i].m[0][0] = f[0]; me->anims[i].m[1][0] = f[1]; me->anims[i].m[2][0] = f[2];  me->anims[i].m[3][0] = f[9]; 
                    // Coluna 1
                    me->anims[i].m[0][1] = f[3]; me->anims[i].m[1][1] = f[4]; me->anims[i].m[2][1] = f[5];  me->anims[i].m[3][1] = f[10];
                    // Coluna 2
                    me->anims[i].m[0][2] = f[6]; me->anims[i].m[1][2] = f[7]; me->anims[i].m[2][2] = f[8];  me->anims[i].m[3][2] = f[11];
                    
                    // Última Coluna (W)
                    me->anims[i].m[0][3] = 0.0f;
                    me->anims[i].m[1][3] = 0.0f;
                    me->anims[i].m[2][3] = 0.0f;
                    me->anims[i].m[3][3] = 1.0f;

                    s.pos += 48;
                }
            }
        }

        Log_Print("VSL: TC '%s' g%d subs=%d verts=%d anims=%d pos=%d\n",
                 tcName, g, subCount, me->totalVertices, me->animCount, s.pos);
    }

    free(tcData);
    return true;
}

static int vsl_load_texture(const char* name) {
    char lower[64];
    strncpy(lower, name, 63); lower[63] = '\0';
    for (int i = 0; lower[i]; i++) {
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] + 32);
    }
    char pngName[64];
    char* dot = strrchr(lower, '.');
    if (dot && (_stricmp(dot, ".bmp") == 0 || _stricmp(dot, ".tga") == 0)) {
        strncpy(pngName, lower, dot - lower); pngName[dot - lower] = '\0'; strcat(pngName, ".png");
    } else {
        strncpy(pngName, lower, 63);
    }
    for (int i = 0; i < g_game.textureCount; i++) {
        if (g_game.textures[i].inUse && _stricmp(g_game.textures[i].name, pngName) == 0) return i;
    }
    int resIdx = DF_Find(pngName);
    if (resIdx < 0) resIdx = DF_Find(name);
    if (resIdx < 0) resIdx = DF_Find(lower);
    static int texLogCount = 0;
    if (resIdx >= 0) {
        if (texLogCount < 50) { Log_Print("VSL: tex '%s' found in DF as '%s' idx=%d\n", name, pngName, resIdx); texLogCount++; }
        uint8_t* d = DF_ReadAlloc(resIdx);
        if (d) {
            int texId = Texture_LoadFromMemoryColorKey(d, DF_GetSize(resIdx), pngName);
            if (texId >= 0) {
                strncpy(g_game.textures[texId].name, pngName, sizeof(g_game.textures[0].name) - 1);
            }
            if (texLogCount < 50) { Log_Print("VSL: tex DF load result=%d for '%s'\n", texId, pngName); texLogCount++; }
            free(d);
            return texId;
        }
        if (texLogCount < 50) { Log_Print("VSL: tex DF_ReadAlloc FAILED for '%s'\n", pngName); texLogCount++; }
        return -1;
    }
    char basePath[MAX_PATH];
    snprintf(basePath, sizeof(basePath), "assets\\%s", pngName);
    int result = Texture_Load(basePath);
    if (texLogCount < 30) { Log_Print("VSL: tex '%s' DF not found, tried '%s' result=%d\n", name, basePath, result); texLogCount++; }
    return result;
}

static int vsl_find_ifl_cache(const char* name) {
    for (int i = 0; i < g_vsl.iflCacheCount; i++) {
        if (_stricmp(g_vsl.iflCache[i].name, name) == 0) return i;
    }
    return -1;
}

static void vsl_load_ifl_textures(const char* tn) {
    if (!tn || tn[0] == '\0') return;
    int resIdx = DF_Find(tn);
    if (resIdx < 0) return;
    bool isIFL = (strlen(tn) >= 4 && _stricmp(tn + strlen(tn) - 4, ".ifl") == 0);
    if (isIFL) {
        uint32_t iflSize = DF_GetSize(resIdx);
        uint8_t* iflData = DF_ReadAlloc(resIdx);
        if (!iflData) return;

        int cacheIdx = vsl_find_ifl_cache(tn);
        if (cacheIdx < 0 && g_vsl.iflCacheCount < VSL_MAX_IFL_CACHE) {
            cacheIdx = g_vsl.iflCacheCount++;
            strncpy(g_vsl.iflCache[cacheIdx].name, tn, 31);
            g_vsl.iflCache[cacheIdx].name[31] = '\0';
            g_vsl.iflCache[cacheIdx].frameCount = 0;
        }

        char* ptr = (char*)iflData;
        char* end = (char*)iflData + iflSize;
        int frameIdx = 0;
        while (ptr < end && frameIdx < VSL_MAX_IFL_FRAMES) {
            char line[256]; int len = 0;
            while (ptr < end && *ptr != '\n' && *ptr != '\r' && len < 255) line[len++] = *ptr++;
            line[len] = '\0';
            while (ptr < end && (*ptr == '\n' || *ptr == '\r')) ptr++;
            if (line[0] == '\0' || line[0] == ';') continue;

            char pngLine[64];
            char* dot = strrchr(line, '.');
            if (dot && (_stricmp(dot, ".bmp") == 0 || _stricmp(dot, ".tga") == 0)) {
                strncpy(pngLine, line, dot - line); pngLine[dot - line] = '\0'; strcat(pngLine, ".png");
            } else {
                strncpy(pngLine, line, 63);
            }

            if (cacheIdx >= 0 && frameIdx < VSL_MAX_IFL_FRAMES) {
                strncpy(g_vsl.iflCache[cacheIdx].frameNames[frameIdx], pngLine, 31);
                g_vsl.iflCache[cacheIdx].frameNames[frameIdx][31] = '\0';
            }
            frameIdx++;
            vsl_load_texture(line);
        }

        if (cacheIdx >= 0) {
            g_vsl.iflCache[cacheIdx].frameCount = frameIdx; // CORRIGIDO AQUI
            Log_Print("VSL: IFL '%s' cached with %d frames\n", tn, g_vsl.iflCache[cacheIdx].frameCount);
        }

        free(iflData);
    } else {
        vsl_load_texture(tn);
    }
}

bool VSL_Load(const char* datPath) {
    VSL_Shutdown();
    if (!DF_Open(datPath)) { Log_Print("VSL: failed to open '%s'\n", datPath); return false; }

    int vslIdx = DF_Find("main.vsl");
    if (vslIdx < 0) { Log_Print("VSL: main.vsl not found\n"); DF_Close(); return false; }

    uint32_t vslSize = DF_GetSize(vslIdx);
    uint8_t* vslData = DF_ReadAlloc(vslIdx);
    if (!vslData) { DF_Close(); return false; }

    uint32_t pos = 0;
    g_vsl.materialCount = *(uint32_t*)(vslData + pos); pos += 4;
    if (g_vsl.materialCount > VSL_MAX_MATERIALS) g_vsl.materialCount = VSL_MAX_MATERIALS;
    Log_Print("VSL: %d materials\n", g_vsl.materialCount);

    for (int i = 0; i < g_vsl.materialCount; i++) {
        if (pos + 32 > vslSize) break;
        memcpy(g_vsl.materialNames[i], vslData + pos, 32);
        g_vsl.materialNames[i][31] = '\0';
        for (int j = 0; g_vsl.materialNames[i][j]; j++) {
            if (g_vsl.materialNames[i][j] >= 'A' && g_vsl.materialNames[i][j] <= 'Z')
                g_vsl.materialNames[i][j] = (char)(g_vsl.materialNames[i][j] + 32);
        }
        pos += 32;
    }

    g_vsl.frameCount = *(uint32_t*)(vslData + pos); pos += 4;
    if (g_vsl.frameCount > 0) {
        g_vsl.frames = (VSLFrame*)malloc(g_vsl.frameCount * sizeof(VSLFrame));
        if (g_vsl.frames && pos + g_vsl.frameCount * 12 <= vslSize) {
            memcpy(g_vsl.frames, vslData + pos, g_vsl.frameCount * 12);
        }
    }
    Log_Print("VSL: %d frames\n", g_vsl.frameCount);

    g_vsl.meshTableCount = 0;
    for (int i = 0; i < g_vsl.materialCount && g_vsl.meshTableCount < VSL_MAX_MESHES; i++) {
        vsl_load_tc(g_vsl.materialNames[i], g_vsl.meshTableCount);
        g_vsl.meshTableCount++;
    }

    for (int i = 0; i < g_vsl.materialCount; i++) {
        char iflName[64];
        strncpy(iflName, g_vsl.materialNames[i], 31); iflName[31] = '\0';
        char* dot = strrchr(iflName, '.');
        if (dot) { strncpy(dot, ".ifl", 4); dot[4] = '\0'; vsl_load_ifl_textures(iflName); }
    }

    for (int i = 0; i < g_vsl.tcCount; i++) {
        if (!g_vsl.tcActive[i]) continue;
        TCMaterial* mat = &g_vsl.tcMaterials[i];
        if (mat->hasAnimation && mat->animName1[0]) {
            size_t len = strlen(mat->animName1);
            if (len >= 4 && _stricmp(mat->animName1 + len - 4, ".ifl") == 0)
                vsl_load_ifl_textures(mat->animName1);
        }
    }

    free(vslData);
    g_vsl.active = true;
    Log_Print("VSL: load complete, %d mesh tables, %d frames (DF kept open)\n", g_vsl.meshTableCount, g_vsl.frameCount);
    return true;
}

static int vslRenderCalled = 0;
static int vslVertLog = 0;
static int vslAnimLog = 0;

void VSL_Render(int frame) {
    if (!g_vsl.active || frame < 0 || frame >= g_vsl.frameCount) return;
    VSLFrame* fr = &g_vsl.frames[frame];

    // Log de Debug a cada 30 frames
    static int lastFrameLog = -1;
    if (abs(frame - lastFrameLog) >= 30 || frame == 0) {
        Log_Print("VSL: frame=%d mi=[%d,%d,%d] ai=[%d,%d,%d]\n",
            frame, fr->meshIdx[0], fr->meshIdx[1], fr->meshIdx[2],
            fr->animIdx[0], fr->animIdx[1], fr->animIdx[2]);
        lastFrameLog = frame;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluPerspective(73.74, 640.0 / 480.0, 0.01, 15.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    gluLookAt(0, 0, 3.2, 0, 0, -1, 0, 1, 0);

    // Blend normal (herdado pelo render, mas garantimos aqui)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);

    for (int layer = 0; layer < 3; layer++) {
        int mi = fr->meshIdx[layer];
        int ai = fr->animIdx[layer];
        if (mi < 0 || mi >= g_vsl.materialCount) continue;

        VSLMeshTable* mt = &g_vsl.meshTables[mi];
        for (int g = 0; g < mt->meshGroupCount; g++) {
            TCMeshEntry* me = &mt->meshGroups[g];
            if (me->totalVertices <= 0 || !me->vertices) continue;

            for (int sg = 0; sg < me->count; sg++) {
                TCSubGroup* grp = &me->groups[sg];
                int tIdx = vsl_find_tc(grp->name);
                TCMaterial* mat = (tIdx >= 0) ? &g_vsl.tcMaterials[tIdx] : NULL;
                GLuint texId = 0;

                // Lógica de Textura (reavalia IFL a cada frame)
                if (mat) {
                    const char* texName = mat->name;
                    if (mat->hasAnimation && mat->animName1[0]) {
                        int iflIdx = vsl_find_ifl_cache(mat->animName1);
                        if (iflIdx >= 0) {
                            VSLAIFLCache* ic = &g_vsl.iflCache[iflIdx];
                            int iflFrame = (ai >= 0) ? ai % ic->frameCount : 0;
                            if (iflFrame >= 0 && iflFrame < ic->frameCount)
                                texName = ic->frameNames[iflFrame];
                            else
                                texName = mat->animName1;
                        } else {
                            texName = mat->animName1;
                        }
                    }
                    texId = vsl_load_texture(texName);
                    if (vslRenderCalled < 50) {
                        Log_Print("VSL: texLOAD frame=%d grp='%s' texName='%s' ai=%d texId=%d\n", frame, grp->name, texName, ai, texId);
                        vslRenderCalled++;
                    }
                }

                // Garante que sem textura não desenha "Fantasma"
                if (texId < 0 || texId >= MAX_TEXTURES || !g_game.textures[texId].inUse) {
                    glDisable(GL_TEXTURE_2D);
                } else {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, g_game.textures[texId].id);
                }

                // Configuração de Material (original usa glColor4fv com cor do AMBIENT, não diffuse)
                if (mat && mat->materialCount > 0 && mat->ambient) {
                    int mii = (ai >= 0 && mat->materialCount > 1) ? ai % mat->materialCount : 0;
                    glColor4fv(&mat->ambient[mii*4]);
                } else {
                    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                }

                // Original: blend herdado do caller (não seta durante VSL)

                if (me->animCount > 0 && ai >= 0 && ai < me->animCount) {
                    if (frame < 10) {
                        float* m = &me->anims[ai].m[0][0];
                        Log_Print("Frame %d AI %d Mat: [%.2f, %.2f, %.2f, %.2f] Trans=[%.2f, %.2f, %.2f]\n", 
                            frame, ai, m[0], m[1], m[2], m[3], m[12], m[13], m[14]);
                    }
                }

                glPushMatrix();
                // Aplica a Matriz de Animação (com correção de Z rotation)
                if (me->animCount > 0 && ai >= 0 && ai < me->animCount) {
                    float m[16];
                    memcpy(m, &me->anims[ai].m[0][0], sizeof(m));
                    m[1] = -m[1];   // Inverte seno Z rotation
                    m[4] = -m[4];   // Inverte seno Z rotation
                    glMultMatrixf(m);
                }

                glBegin(GL_TRIANGLES);
                int sv = 0;
                for (int v = 0; v < sg; v++) sv += me->faceCounts[v];
                int vc = me->faceCounts[sg];
                
                for (int v = sv; v < sv + vc && v < me->totalVertices; v++) {
                    glTexCoord2f(me->vertices[v].u, 1.0f - me->vertices[v].v);
                    glVertex3f(me->vertices[v].x, me->vertices[v].y, me->vertices[v].z);
                }
                glEnd();
                glPopMatrix();
            }
        }
    }

    // Restore State
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_MODELVIEW);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_AMBIENT, (float[]){0.2f,0.2f,0.2f,1.0f});
    glMaterialfv(GL_FRONT, GL_DIFFUSE, (float[]){0.8f,0.8f,0.8f,1.0f});
    glMaterialfv(GL_FRONT, GL_SPECULAR, (float[]){0,0,0,1});
    glMaterialfv(GL_FRONT, GL_EMISSION, (float[]){0,0,0,1});
}

void VSL_AdvanceFrame(float dt) {
    vslFrameTimer += dt;
    if (vslFrameTimer >= (1.0f / 60.0f)) vslFrameTimer -= (1.0f / 60.0f);
}

void VSL_Shutdown(void) {
    if (g_vsl.frames) { free(g_vsl.frames); g_vsl.frames = NULL; }
    g_vsl.frameCount = 0;
    for (int i = 0; i < g_vsl.meshTableCount; i++) {
        VSLMeshTable* mt = &g_vsl.meshTables[i];
        for (int g = 0; g < mt->meshGroupCount; g++) {
            TCMeshEntry* me = &mt->meshGroups[g];
            if (me->groups) { free(me->groups); me->groups = NULL; }
            if (me->faceCounts) { free(me->faceCounts); me->faceCounts = NULL; }
            if (me->vertices) { free(me->vertices); me->vertices = NULL; }
            if (me->anims) { free(me->anims); me->anims = NULL; }
        }
        if (mt->meshGroups) { free(mt->meshGroups); mt->meshGroups = NULL; }
    }
    for (int i = 0; i < 500; i++) {
        if (!g_vsl.tcActive[i]) continue;
        TCMaterial* m = &g_vsl.tcMaterials[i];
        if (m->ambient) { free(m->ambient); m->ambient = NULL; }
        if (m->diffuse) { free(m->diffuse); m->diffuse = NULL; }
        if (m->specular) { free(m->specular); m->specular = NULL; }
        if (m->emissive) { free(m->emissive); m->emissive = NULL; }
        if (m->shininess) { free(m->shininess); m->shininess = NULL; }
    }
    memset(&g_vsl, 0, sizeof(g_vsl));
    vslRenderCalled = 0;
    vslVertLog = 0;
    vslAnimLog = 0;
    DF_Close();
    Log_Print("VSL: shutdown\n");
}