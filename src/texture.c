#include "pumpy.h"
#include "zlibinflate.h"

#ifndef Z_OK
#define Z_OK 0
#endif
#ifndef Z_STREAM_END
#define Z_STREAM_END 1
#endif

/* GL_CLAMP_TO_EDGE é OpenGL 1.2; o gl.h antigo de algumas plataformas cobre 1.1. */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

static int Texture_FindFree(void) {
    int i;
    for (i = 0; i < MAX_TEXTURES; i++) {
        if (!g_game.textures[i].inUse) return i;
    }
    return -1;
}

static bool Texture_LoadTGA(const char* path, uint8_t** dataOut, int* wOut, int* hOut, int* fmtOut) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    uint8_t header[18];
    if (fread(header, 1, 18, f) != 18) { fclose(f); return false; }

    int width = header[12] | (header[13] << 8);
    int height = header[14] | (header[15] << 8);
    int bpp = header[16];

    if (width <= 0 || height <= 0 || (bpp != 24 && bpp != 32)) {
        fclose(f);
        return false;
    }

    int channels = bpp / 8;
    int dataSize = width * height * channels;
    uint8_t* pixels = (uint8_t*)malloc(dataSize);
    if (!pixels) { fclose(f); return false; }

    if (header[2] == 10) {
        int px = 0;
        while (px < dataSize) {
            uint8_t chunkHeader;
            if (fread(&chunkHeader, 1, 1, f) != 1) break;
            if (chunkHeader & 0x80) {
                int count = (chunkHeader & 0x7F) + 1;
                uint8_t pixel[4];
                fread(pixel, 1, channels, f);
                int p;
                for (p = 0; p < count * channels && px < dataSize; p++) {
                    pixels[px++] = pixel[p % channels];
                }
            } else {
                int count = (chunkHeader & 0x7F) + 1;
                int bytes = count * channels;
                fread(pixels + px, 1, bytes, f);
                px += bytes;
            }
        }
    } else {
        fread(pixels, 1, dataSize, f);
    }
    fclose(f);

    int y;
    for (y = 0; y < height / 2; y++) {
        int sy = y * width * channels;
        int dy = (height - 1 - y) * width * channels;
        int i;
        for (i = 0; i < width * channels; i++) {
            uint8_t t = pixels[sy + i];
            pixels[sy + i] = pixels[dy + i];
            pixels[dy + i] = t;
        }
    }

    if (channels == 3) {
        uint8_t* rgba = (uint8_t*)malloc(width * height * 4);
        int i;
        for (i = 0; i < width * height; i++) {
            rgba[i*4+0] = pixels[i*3+2];
            rgba[i*4+1] = pixels[i*3+1];
            rgba[i*4+2] = pixels[i*3+0];
            rgba[i*4+3] = 0xFF;
        }
        free(pixels);
        pixels = rgba;
        channels = 4;
    } else if (channels == 4) {
        int i;
        for (i = 0; i < width * height; i++) {
            uint8_t t = pixels[i*4+0];
            pixels[i*4+0] = pixels[i*4+2];
            pixels[i*4+2] = t;
        }
    }

    *dataOut = pixels;
    *wOut = width;
    *hOut = height;
    *fmtOut = GL_RGBA;
    return true;
}

static GLuint Texture_CreateGL(uint8_t* data, int width, int height) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);
    /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); */
    GLint flt = g_game.gfxTexFilter ? GL_NEAREST : GL_LINEAR;  /* GRAPHICS SETTINGS */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flt);
    /* GL_CLAMP_TO_EDGE: previne UV wrap em sprites cujas coordenadas excedem [0,1] por alguns pixels.
     * Exemplos confirmados via análise do ARROW542.SP2:
     *   - combo_ sprite: u2=258/256=1.007 → sem clamp, mostra borda esquerda do MISS (wrap S)
     *   - dec00 dígitos 5-9: vEnd=258/256=1.007 → sem clamp, mostra linha PERFECT no fundo (wrap T)
     * Com GL_CLAMP_TO_EDGE, UV > 1.0 usa o pixel da borda (transparente nessas texturas). */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

/* ------------------------------------------------------------------ PNG -- */

static uint32_t png_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int png_paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* Decode a PNG file into upper-left origin 32bpp RGBA. Handles gray/RGB/
 * palette/gray-alpha/RGBA at 8 and 16 bits (non-interlaced), using the
 * codebase's own inflate implementation. */
static bool Texture_LoadPNG(const char* path, uint8_t** dataOut, int* wOut, int* hOut) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 33) { fclose(f); return false; }

    uint8_t* filebuf = (uint8_t*)malloc((size_t)sz);
    if (!filebuf) { fclose(f); return false; }
    size_t got = fread(filebuf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(filebuf); return false; }

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (memcmp(filebuf, sig, 8) != 0) { free(filebuf); return false; }

    uint32_t width = 0, height = 0;
    int bitDepth = 0, colorType = 0;
    uint8_t* palette = NULL; size_t paletteLen = 0;
    uint8_t* tRNS = NULL;    size_t tRNSLen = 0;
    size_t idatTotal = 0;
    bool sawIhdr = false;

    size_t off = 8;
    while (off + 12 <= (size_t)sz) {
        uint32_t len = png_be32(filebuf + off);
        const uint8_t* type = filebuf + off + 4;
        size_t d = off + 8;
        if (d + len > (size_t)sz) break;

        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            width  = png_be32(filebuf + d);
            height = png_be32(filebuf + d + 4);
            bitDepth  = filebuf[d + 8];
            colorType = filebuf[d + 9];
            sawIhdr = true;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (len > 0) {
                palette = (uint8_t*)malloc(len);
                memcpy(palette, filebuf + d, len);
                paletteLen = len;
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (len > 0) {
                tRNS = (uint8_t*)malloc(len);
                memcpy(tRNS, filebuf + d, len);
                tRNSLen = len;
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idatTotal += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        off = d + len + 4; /* CRC */
    }

    if (!sawIhdr || width == 0 || height == 0 || idatTotal == 0) {
        free(filebuf); free(palette); free(tRNS);
        return false;
    }

    int channels;
    switch (colorType) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: free(filebuf); free(palette); free(tRNS); return false;
    }

    if (bitDepth != 8 && bitDepth != 16) {
        /* Only 8/16-bit supported (palette and gray at 1/2/4 bits are not). */
        free(filebuf); free(palette); free(tRNS);
        return false;
    }
    if (colorType == 3 && bitDepth != 8) {
        free(filebuf); free(palette); free(tRNS);
        return false;
    }

    /* Build the concatenated IDAT stream. */
    uint8_t* comp = (uint8_t*)malloc(idatTotal);
    if (!comp) { free(filebuf); free(palette); free(tRNS); return false; }
    size_t ci = 0;
    off = 8;
    while (off + 12 <= (size_t)sz) {
        uint32_t len = png_be32(filebuf + off);
        const uint8_t* type = filebuf + off + 4;
        size_t d = off + 8;
        if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(comp + ci, filebuf + d, len);
            ci += len;
        }
        if (memcmp(type, "IEND", 4) == 0) break;
        off = d + len + 4;
    }
    free(filebuf);

    int bytesPerSample = (bitDepth == 16) ? 2 : 1;
    int rowStore = (int)width * channels * bytesPerSample;
    uint32_t rawSize = ((uint32_t)rowStore + 1) * height;
    uint8_t* raw = (uint8_t*)malloc(rawSize);
    if (!raw) { free(comp); free(palette); free(tRNS); return false; }
    uint32_t rawLen = rawSize;
    int r = zlib_decompress(comp, (uint32_t)idatTotal, raw, &rawLen);
    free(comp);
    if (r != Z_OK && r != Z_STREAM_END) {
        free(raw); free(palette); free(tRNS);
        return false;
    }
    if (rawLen != rawSize) {
        free(raw); free(palette); free(tRNS);
        return false;
    }

    int bpp = channels * bytesPerSample;
    uint8_t* rgba = (uint8_t*)malloc((size_t)width * height * 4);
    uint8_t* cur  = (uint8_t*)malloc((size_t)rowStore);
    uint8_t* prev = (uint8_t*)calloc(1, (size_t)rowStore);
    if (!rgba || !cur || !prev) {
        free(rgba); free(cur); free(prev); free(raw); free(palette); free(tRNS);
        return false;
    }

    const uint8_t* src = raw;
    for (uint32_t y = 0; y < height; y++) {
        uint8_t ft = *src++;
        for (int i = 0; i < rowStore; i++) {
            uint8_t L  = (i >= bpp) ? cur[i - bpp]     : 0;
            uint8_t U  = prev[i];
            uint8_t LU = (i >= bpp) ? prev[i - bpp]    : 0;
            uint8_t v;
            switch (ft) {
            case 0: v = src[i]; break;
            case 1: v = (uint8_t)(src[i] + L); break;
            case 2: v = (uint8_t)(src[i] + U); break;
            case 3: v = (uint8_t)(src[i] + (((int)L + (int)U) >> 1)); break;
            case 4: v = (uint8_t)(src[i] + png_paeth(L, U, LU)); break;
            default:
                free(rgba); free(cur); free(prev); free(raw); free(palette); free(tRNS);
                return false;
            }
            cur[i] = v;
        }

        uint8_t* dst = rgba + (size_t)y * width * 4;
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t* sp = cur + (size_t)x * bpp;
            uint32_t pr, pg, pb, pa;
            switch (colorType) {
            case 0: pr = pg = pb = sp[0]; pa = 255; break;
            case 2: pr = sp[0]; pg = sp[1]; pb = sp[2]; pa = 255; break;
            case 3: {
                if (sp[0] * 3 + 2 >= paletteLen) { pr = pg = pb = 0; }
                else { pr = palette[sp[0]*3]; pg = palette[sp[0]*3+1]; pb = palette[sp[0]*3+2]; }
                pa = (tRNS && sp[0] < tRNSLen) ? tRNS[sp[0]] : 255;
                break;
            }
            case 4: pr = pg = pb = sp[0]; pa = sp[bytesPerSample]; break;
            default: pr = sp[0]; pg = sp[1]; pb = sp[2]; pa = sp[3]; break;
            }
            dst[x*4 + 0] = (uint8_t)pr;
            dst[x*4 + 1] = (uint8_t)pg;
            dst[x*4 + 2] = (uint8_t)pb;
            dst[x*4 + 3] = (uint8_t)pa;
        }

        memcpy(prev, cur, (size_t)rowStore);
        src += rowStore;
    }

    free(cur); free(prev); free(raw); free(palette); free(tRNS);
    *dataOut = rgba;
    *wOut = (int)width;
    *hOut = (int)height;
    return true;
}

/* ------------------------------------------------------------------ BMP -- */

static uint16_t bmp_le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t bmp_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Decode a BMP file into upper-left origin 32bpp RGBA. Supports 8-bit
 * (palette), 24-bit and 32-bit uncompressed (BI_RGB) bitmaps. */
static bool Texture_LoadBMP(const char* path, uint8_t** dataOut, int* wOut, int* hOut) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 54) { fclose(f); return false; }

    uint8_t* filebuf = (uint8_t*)malloc((size_t)sz);
    if (!filebuf) { fclose(f); return false; }
    size_t got = fread(filebuf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(filebuf); return false; }

    if (filebuf[0] != 'B' || filebuf[1] != 'M') { free(filebuf); return false; }

    uint32_t dataOff = bmp_le32(filebuf + 10);
    int width  = (int)bmp_le32(filebuf + 18);
    int height = (int)bmp_le32(filebuf + 22);
    uint16_t planes   = bmp_le16(filebuf + 26);
    uint16_t bpp      = bmp_le16(filebuf + 28);
    uint32_t compr    = bmp_le32(filebuf + 30);
    uint32_t colors   = bmp_le32(filebuf + 46);

    if (width <= 0 || height == 0 || planes != 1 || compr != 0 ||
        (bpp != 8 && bpp != 24 && bpp != 32)) {
        free(filebuf);
        return false;
    }

    bool bottomUp = height > 0;
    int absH = height > 0 ? height : -height;

    /* 8-bit palette lives after the 40-byte header. */
    uint8_t* pal = NULL;
    if (bpp == 8) {
        uint32_t n = colors ? colors : 256;
        uint32_t palOff = 54;
        if (n * 4 > (uint32_t)(sz - (long)palOff)) n = (uint32_t)((sz - (long)palOff) / 4);
        pal = (uint8_t*)malloc(n * 4);
        memcpy(pal, filebuf + palOff, n * 4);
        (void)n;
    }

    int stride = ((int)width * (int)bpp + 31) / 32 * 4;
    int pad = stride - (int)width * (int)bpp / 8;

    uint8_t* rgba = (uint8_t*)malloc((size_t)width * absH * 4);
    if (!rgba) { free(pal); free(filebuf); return false; }

    for (int y = 0; y < absH; y++) {
        int srcRow = bottomUp ? (absH - 1 - y) : y;
        const uint8_t* row = filebuf + dataOff + (size_t)srcRow * stride;
        uint8_t* dst = rgba + (size_t)y * width * 4;
        for (int x = 0; x < width; x++) {
            if (bpp == 8) {
                uint8_t idx = row[x];
                uint8_t* c = (pal && idx * 4 + 3 < 256 * 4) ? pal + idx * 4 : NULL;
                if (c) { dst[x*4+0] = c[2]; dst[x*4+1] = c[1]; dst[x*4+2] = c[0]; dst[x*4+3] = c[3]; }
                else   { dst[x*4+0] = dst[x*4+1] = dst[x*4+2] = idx; dst[x*4+3] = 255; }
            } else if (bpp == 24) {
                const uint8_t* p = row + (size_t)x * 3;
                dst[x*4+0] = p[2]; dst[x*4+1] = p[1]; dst[x*4+2] = p[0]; dst[x*4+3] = 255;
            } else { /* 32 */
                const uint8_t* p = row + (size_t)x * 4;
                dst[x*4+0] = p[2]; dst[x*4+1] = p[1]; dst[x*4+2] = p[0]; dst[x*4+3] = p[3];
            }
        }
        (void)pad;
    }

    free(pal);
    free(filebuf);
    *dataOut = rgba;
    *wOut = width;
    *hOut = absH;
    return true;
}

void Texture_Init(void) {
}

/* Reaplica o filtro (TEXTURE FILTER do Service Menu) em todas as texturas carregadas. */
void Texture_ApplyFilterAll(void) {
    GLint flt = g_game.gfxTexFilter ? GL_NEAREST : GL_LINEAR;
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (!g_game.textures[i].inUse) continue;
        glBindTexture(GL_TEXTURE_2D, g_game.textures[i].id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flt);
    }
}

static bool Texture_LoadFile(const char* path, uint8_t** dataOut, int* wOut, int* hOut, int* fmtOut) {
    const char* ext = strrchr(path, '.');
    if (!ext) return false;

    if (_stricmp(ext, ".png") == 0) {
        if (Texture_LoadPNG(path, dataOut, wOut, hOut)) {
            *fmtOut = GL_RGBA;
            return true;
        }
        return false;
    }

    if (_stricmp(ext, ".bmp") == 0) {
        if (Texture_LoadBMP(path, dataOut, wOut, hOut)) {
            *fmtOut = GL_RGBA;
            return true;
        }
        return false;
    }

    if (_stricmp(ext, ".tga") == 0) {
        return Texture_LoadTGA(path, dataOut, wOut, hOut, fmtOut);
    }

    return false;
}

int Texture_Load(const char* name) {
    int idx = Texture_FindFree();
    if (idx < 0) return -1;

    char path[MAX_PATH];
    if (name[0] == '/' || name[0] == '\\' || strchr(name, ':') != NULL) {
        strncpy(path, name, MAX_PATH - 1);
    } else {
        snprintf(path, sizeof(path), "%s/%s", g_game.currentDirectory, name);
    }

    uint8_t* data = NULL;
    int w = 0, h = 0, fmt = 0;

    if (!Texture_LoadFile(path, &data, &w, &h, &fmt)) {
        return -1;
    }

    Texture* t = &g_game.textures[idx];
    t->id = Texture_CreateGL(data, w, h);
    t->width = w;
    t->height = h;
    t->format = fmt;
    t->inUse = true;
    t->lastFrame = g_game.frameCounter;
    strncpy(t->name, name, sizeof(t->name) - 1);

    free(data);
    g_game.textureCount++;
    const char* shortName = strrchr(name, '\\');
    shortName = shortName ? shortName + 1 : name;
    Log_Print("Texture: loaded '%s' (%dx%d) id=%d\n", shortName, w, h, idx);
    return idx;
}

int Texture_LoadFromMemory(const uint8_t* buf, uint32_t bufSize, const char* debugName) {
    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);

    char tmpName[MAX_PATH];
    snprintf(tmpName, sizeof(tmpName), "_tmp_%s", debugName);

    char tmpPath[MAX_PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s%s", tmpDir, tmpName);

    FILE* f = fopen(tmpPath, "wb");
    if (!f) return -1;
    fwrite(buf, 1, bufSize, f);
    fclose(f);

    int id = Texture_Load(tmpPath);
    remove(tmpPath);
    return id;
}

int Texture_LoadFromMemoryColorKey(const uint8_t* buf, uint32_t bufSize, const char* debugName) {
    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    char tmpName[MAX_PATH];
    snprintf(tmpName, sizeof(tmpName), "_tmp_%s", debugName);
    char tmpPath[MAX_PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s%s", tmpDir, tmpName);
    FILE* f = fopen(tmpPath, "wb");
    if (!f) return -1;
    fwrite(buf, 1, bufSize, f);
    fclose(f);

    uint8_t* data = NULL;
    int w = 0, h = 0, fmt = 0;
    if (!Texture_LoadFile(tmpPath, &data, &w, &h, &fmt)) { remove(tmpPath); return -1; }
    remove(tmpPath);

    int stride = w * 4;
    for (int i = 0; i < h * stride; i += 4) {
        uint8_t r = data[i];
        uint8_t g = data[i+1];
        uint8_t b = data[i+2];
        uint8_t maxc = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
        if (maxc > 0) {
            data[i] = (uint8_t)((r * 255 + maxc / 2) / maxc);
            data[i+1] = (uint8_t)((g * 255 + maxc / 2) / maxc);
            data[i+2] = (uint8_t)((b * 255 + maxc / 2) / maxc);
        }
        data[i+3] = maxc;
    }

    int idx = Texture_FindFree();
    if (idx < 0) { free(data); return -1; }

    Texture* t = &g_game.textures[idx];
    t->id = Texture_CreateGL(data, w, h);
    t->width = w;
    t->height = h;
    t->format = GL_RGBA;
    t->inUse = true;
    t->lastFrame = g_game.frameCounter;
    strncpy(t->name, debugName, sizeof(t->name) - 1);
    free(data);
    g_game.textureCount++;
    Log_Print("Texture: loaded CK '%s' (%dx%d) id=%d\n", debugName, w, h, idx);
    return idx;
}

void Texture_Unload(int id) {
    if (id < 0 || id >= MAX_TEXTURES || !g_game.textures[id].inUse) return;
    Texture* t = &g_game.textures[id];
    glDeleteTextures(1, &t->id);
    memset(t, 0, sizeof(Texture));
    g_game.textureCount--;
}

void Texture_Bind(int id) {
    if (id >= 0 && id < MAX_TEXTURES && g_game.textures[id].inUse) {
        glBindTexture(GL_TEXTURE_2D, g_game.textures[id].id);
        g_game.textures[id].lastFrame = g_game.frameCounter;
    }
}

int Texture_GetWidth(int id) {
    if (id < 0 || id >= MAX_TEXTURES || !g_game.textures[id].inUse) return 0;
    return g_game.textures[id].width;
}

int Texture_GetHeight(int id) {
    if (id < 0 || id >= MAX_TEXTURES || !g_game.textures[id].inUse) return 0;
    return g_game.textures[id].height;
}

void Texture_DrawUV(int id, float x, float y, float w, float h,
                     float u1, float v1, float u2, float v2, float r, float g, float b, float alpha) {
    if (id < 0 || id >= MAX_TEXTURES || !g_game.textures[id].inUse) return;
    Texture* t = &g_game.textures[id];
    float yUp = 480.0f - y - h;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glColor4f(r, g, b, alpha);
    float texW = (float)t->width;
    float texH = (float)t->height;
    if (texW <= 0) texW = 256.0f;
    if (texH <= 0) texH = 256.0f;
    glBegin(GL_QUADS);
    glTexCoord2f(u1 / texW, v2 / texH);
    glVertex2f(x, yUp);
    glTexCoord2f(u2 / texW, v2 / texH);
    glVertex2f(x + w, yUp);
    glTexCoord2f(u2 / texW, v1 / texH);
    glVertex2f(x + w, yUp + h);
    glTexCoord2f(u1 / texW, v1 / texH);
    glVertex2f(x, yUp + h);
    glEnd();
}

void Texture_Draw(int id, float x, float y, float scaleX, float scaleY, float alpha) {
    if (id < 0 || id >= MAX_TEXTURES || !g_game.textures[id].inUse) return;
    Texture* t = &g_game.textures[id];
    float w = t->width * scaleX;
    float h = t->height * scaleY;
    float yUp = 480.0f - y - h;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1);
    glVertex2f(x, yUp);
    glTexCoord2f(1, 1);
    glVertex2f(x + w, yUp);
    glTexCoord2f(1, 0);
    glVertex2f(x + w, yUp + h);
    glTexCoord2f(0, 0);
    glVertex2f(x, yUp + h);
    glEnd();
}

void Texture_Shutdown(void) {
    int i;
    for (i = 0; i < MAX_TEXTURES; i++) {
        if (g_game.textures[i].inUse) {
            glDeleteTextures(1, &g_game.textures[i].id);
        }
    }
    memset(g_game.textures, 0, sizeof(g_game.textures));
    g_game.textureCount = 0;
    Log_Print("Texture: shutdown\n");
}
