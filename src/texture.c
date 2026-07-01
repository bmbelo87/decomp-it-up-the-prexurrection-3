#include "pumpy.h"
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return id;
}

WCHAR g_texWPath[MAX_PATH];

static bool Texture_LoadWIC(const char* path, uint8_t** dataOut, int* wOut, int* hOut) {
    IWICImagingFactory* factory = NULL;
    IWICBitmapDecoder* decoder = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* converter = NULL;
    HRESULT hr;

    hr = CoInitializeEx(NULL, 1);
    (void)hr;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, 1,
                          &IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr)) { Log_Print("WIC: CoCreateInstance failed 0x%x\n", (unsigned)hr); goto fail; }

    MultiByteToWideChar(CP_ACP, 0, path, -1, g_texWPath, MAX_PATH);
    hr = factory->lpVtbl->CreateDecoderFromFilename(factory, g_texWPath, NULL,
        0x80000000, 1, &decoder);
    if (FAILED(hr)) { Log_Print("WIC: CreateDecoderFromFilename failed 0x%x\n", (unsigned)hr); goto fail; }

    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) goto fail;

    UINT w = 0, h = 0;
    hr = frame->lpVtbl->GetSize(frame, &w, &h);
    if (FAILED(hr) || w == 0 || h == 0) goto fail;

    hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    if (FAILED(hr)) goto fail;

    hr = converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)frame,
        &GUID_WICPixelFormat32bppRGBA, 1, NULL, 0.0f, 3);
    if (FAILED(hr)) goto fail;

    UINT stride = w * 4;
    UINT size = stride * h;
    uint8_t* pixels = (uint8_t*)malloc(size);
    if (!pixels) goto fail;

    hr = converter->lpVtbl->CopyPixels(converter, NULL, stride, size, pixels);
    if (FAILED(hr)) { free(pixels); goto fail; }

    for (UINT y = 0; y < h / 2; y++) {
        UINT sy = y * stride;
        UINT dy = (h - 1 - y) * stride;
        for (UINT x = 0; x < stride; x++) {
            uint8_t t = pixels[sy + x];
            pixels[sy + x] = pixels[dy + x];
            pixels[dy + x] = t;
        }
    }

    if (converter) converter->lpVtbl->Release(converter);
    if (frame) frame->lpVtbl->Release(frame);
    if (decoder) decoder->lpVtbl->Release(decoder);
    if (factory) factory->lpVtbl->Release(factory);

    *dataOut = pixels;
    *wOut = (int)w;
    *hOut = (int)h;
    return true;

fail:
    if (converter) converter->lpVtbl->Release(converter);
    if (frame) frame->lpVtbl->Release(frame);
    if (decoder) decoder->lpVtbl->Release(decoder);
    if (factory) factory->lpVtbl->Release(factory);
    return false;
}

void Texture_Init(void) {
}

static bool Texture_LoadFile(const char* path, uint8_t** dataOut, int* wOut, int* hOut, int* fmtOut) {
    const char* ext = strrchr(path, '.');
    if (!ext) return false;

    if (_stricmp(ext, ".tga") == 0) {
        return Texture_LoadTGA(path, dataOut, wOut, hOut, fmtOut);
    }

    if (Texture_LoadWIC(path, dataOut, wOut, hOut)) {
        *fmtOut = GL_RGBA;
        return true;
    }

    return false;
}

int Texture_Load(const char* name) {
    int idx = Texture_FindFree();
    if (idx < 0) return -1;

    char path[MAX_PATH];
    if (name[0] == '\\' || strchr(name, ':') != NULL) {
        strncpy(path, name, MAX_PATH - 1);
    } else {
        snprintf(path, sizeof(path), "%s\\%s", g_game.currentDirectory, name);
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
    int w = 0, h = 0;
    if (!Texture_LoadWIC(tmpPath, &data, &w, &h)) { remove(tmpPath); return -1; }
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
    glTexCoord2f(u1 / texW, v1 / texH);
    glVertex2f(x, yUp);
    glTexCoord2f(u2 / texW, v1 / texH);
    glVertex2f(x + w, yUp);
    glTexCoord2f(u2 / texW, v2 / texH);
    glVertex2f(x + w, yUp + h);
    glTexCoord2f(u1 / texW, v2 / texH);
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
    glTexCoord2f(0, 0);
    glVertex2f(x, yUp);
    glTexCoord2f(1, 0);
    glVertex2f(x + w, yUp);
    glTexCoord2f(1, 1);
    glVertex2f(x + w, yUp + h);
    glTexCoord2f(0, 1);
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
