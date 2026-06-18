#include "pumpy.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

// XOR table for .AUD decryption (from Ghidra 0x00443758, 1024 bytes)
static const uint8_t g_audXorTable[1024] = {
    56,30,183,73,105,12,1,136,165,200,96,0,26,59,3,145,76,41,199,203,187,119,216,20,
    168,17,155,233,237,151,255,171,185,197,164,12,87,50,160,206,144,53,108,55,10,224,
    102,166,141,206,26,243,236,157,46,234,220,93,158,74,133,163,23,8,195,86,36,24,232,
    190,39,148,77,236,113,110,111,200,82,18,118,23,162,242,140,74,57,205,80,126,69,58,
    212,113,42,151,137,155,37,253,48,136,171,232,71,174,99,13,140,245,121,109,251,120,
    86,82,18,193,176,239,114,7,207,192,207,79,191,170,152,139,213,158,64,232,175,54,
    18,16,76,186,67,114,123,218,243,45,45,45,60,125,164,113,205,89,219,135,8,25,149,
    152,92,94,94,175,144,199,6,204,46,124,109,240,71,104,54,178,45,37,42,83,254,83,92,
    57,208,93,189,169,250,48,134,157,242,225,181,223,146,96,236,36,28,98,171,39,14,2,
    13,77,215,139,128,130,4,209,153,28,203,217,189,199,40,33,43,197,80,222,189,90,197,
    226,52,58,190,255,187,242,57,34,124,108,238,131,26,191,233,52,129,84,217,210,65,
    157,49,104,249,30,165,103,165,130,245,86,156,218,228,111,173,169,240,201,113,92,
    105,206,115,35,68,157,100,167,156,67,32,64,115,73,10,52,10,36,72,235,124,93,104,
    244,254,41,188,150,240,229,196,193,68,135,255,13,117,163,167,200,213,107,213,216,
    22,107,166,255,106,106,181,247,56,78,186,164,61,65,117,90,84,198,125,103,9,125,
    247,215,23,196,148,132,227,11,15,1,32,171,219,77,79,38,147,172,21,80,245,193,149,
    147,1,231,101,131,182,160,177,4,100,168,244,134,168,198,33,225,119,74,161,240,221,
    110,99,73,7,105,131,198,176,224,13,228,144,154,166,143,212,184,4,91,116,72,249,
    182,63,94,214,8,252,31,59,46,47,5,230,207,236,174,194,174,102,243,35,6,178,219,88,
    175,56,18,152,149,16,108,214,231,102,127,233,195,81,159,172,123,174,21,101,118,17,
    51,246,252,147,31,30,92,210,225,231,27,5,155,60,222,4,172,140,3,243,253,44,250,81,
    128,87,83,67,184,163,74,126,57,97,175,103,188,146,116,159,166,89,223,5,222,123,36,
    164,25,137,170,220,153,20,35,3,76,72,69,50,63,129,100,109,3,34,133,9,192,61,108,
    55,98,216,207,230,168,184,80,197,53,250,129,202,82,225,119,62,167,167,40,188,90,
    49,47,29,186,183,253,143,202,146,72,63,185,137,227,65,146,55,170,9,6,70,126,64,
    247,15,229,43,115,109,202,130,241,83,116,201,88,28,138,244,119,118,211,170,27,42,
    112,62,154,150,68,120,234,179,52,141,39,66,183,133,122,40,232,228,184,173,22,61,
    95,204,20,249,145,28,19,73,179,194,8,221,163,127,172,14,204,220,7,16,248,47,234,
    131,165,179,238,116,59,111,19,43,208,241,179,106,53,192,203,237,70,87,221,32,107,
    159,218,121,67,56,39,61,50,89,24,176,152,188,239,19,95,44,44,65,27,95,237,17,237,
    201,117,93,122,55,115,26,182,200,194,212,114,143,241,162,136,99,114,69,226,38,25,
    250,91,12,84,2,2,87,98,196,191,214,238,130,5,161,162,142,239,37,118,224,21,125,
    231,88,16,235,254,63,135,95,180,71,238,0,41,60,185,217,102,1,48,142,204,132,169,
    78,202,190,19,209,169,77,221,248,121,148,222,211,244,248,89,40,211,210,128,186,17,
    96,215,128,153,135,15,35,151,215,11,41,132,161,177,176,44,123,234,193,139,162,43,
    120,66,189,38,85,112,218,248,11,106,104,97,229,209,64,161,66,54,191,205,62,96,82,
    24,2,66,173,12,78,154,185,158,195,33,203,81,100,30,69,84,235,148,246,90,22,6,68,
    212,241,98,252,0,101,32,195,14,62,226,239,177,107,144,133,7,112,27,213,75,190,149,
    37,117,38,24,11,226,143,75,127,246,34,173,58,150,129,21,153,15,205,71,85,235,9,
    160,10,254,217,79,42,147,223,219,124,49,227,199,251,141,121,29,53,139,97,214,70,
    79,91,201,134,198,46,134,141,224,88,48,247,99,196,178,29,228,81,230,51,180,111,
    209,158,105,142,127,187,159,210,140,110,145,156,180,230,126,155,0,47,75,211,85,
    180,70,97,75,78,246,251,23,31,138,76,181,29,25,20,252,194,181,208,142,103,14,91,
    54,34,223,60,242,122,86,50,110,112,251,59,22,227,216,187,137,49,33,253,154,183,
    138,151,120,51,208,178,51,182,245,145,156,122,206,160,150,177,220,138,58,31,249,
    192,229,101,136,85,132,94,233
};

static uint8_t bit_reverse(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r = (r << 1) | (b & 1);
        b >>= 1;
    }
    return r;
}

// Decrypt .AUD data in-place. Returns the decrypted data size, or 0 on failure.
static uint32_t Audio_DecryptAUD(const uint8_t* fileData, uint32_t fileSize,
                                  uint8_t** outData, uint32_t* outSize) {
    if (fileSize < 140) return 0;
    if (memcmp(fileData, "ENC1", 4) != 0) return 0;

    uint32_t dataSize = *(uint32_t*)(fileData + 0x84);
    uint32_t skip = *(uint32_t*)(fileData + 0x88);
    uint32_t dataOff = 140 + skip;

    if (dataOff + 4 > fileSize) return 0;
    uint32_t seed = *(uint32_t*)(fileData + dataOff);
    uint32_t encSize = fileSize - dataOff - 4;
    if (encSize == 0) return 0;

    uint8_t* buf = (uint8_t*)malloc(encSize);
    if (!buf) return 0;
    memcpy(buf, fileData + dataOff + 4, encSize);

    for (uint32_t i = 0; i < encSize; i++) {
        uint8_t br = bit_reverse(buf[i]);
        uint32_t idx = (seed + i) & 0x3FF;
        buf[i] = br ^ g_audXorTable[idx];
    }

    *outData = buf;
    *outSize = encSize;
    return encSize;
}

static int Audio_FindFree(void) {
    int i;
    for (i = 0; i < MAX_SOUNDS; i++) {
        if (!g_game.sounds[i].inUse) return i;
    }
    return -1;
}

bool Audio_Init(void) {
    HRESULT hr = DirectSoundCreate(NULL, &g_game.pDS, NULL);
    if (FAILED(hr)) {
        Log_Print("Audio: DirectSoundCreate failed (0x%08lx)\n", hr);
        return false;
    }

    HWND hWnd = g_game.hWnd ? g_game.hWnd : GetDesktopWindow();
    hr = IDirectSound_SetCooperativeLevel(g_game.pDS, hWnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        Log_Print("Audio: SetCooperativeLevel failed (0x%08lx)\n", hr);
    }

    DSBUFFERDESC bufDesc = {0};
    bufDesc.dwSize = sizeof(DSBUFFERDESC);
    bufDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    bufDesc.dwBufferBytes = 0;
    bufDesc.lpwfxFormat = NULL;

    hr = IDirectSound_CreateSoundBuffer(g_game.pDS, &bufDesc, &g_game.pPrimaryBuffer, NULL);
    if (SUCCEEDED(hr) && g_game.pPrimaryBuffer) {
        WAVEFORMATEX wfx = {0};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = 44100;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        IDirectSoundBuffer_SetFormat(g_game.pPrimaryBuffer, &wfx);
    }

    Log_Print("Audio: DirectSound initialized\n");
    return true;
}

int Audio_LoadWAV(const char* name, const uint8_t* data, DWORD size) {
    int idx = Audio_FindFree();
    if (idx < 0) return -1;

    if (size < 44) return -1;
    const uint8_t* p = data;
    if (p[0] != 'R' || p[1] != 'I' || p[2] != 'F' || p[3] != 'F') return -1;
    if (p[8] != 'W' || p[9] != 'A' || p[10] != 'V' || p[11] != 'E') return -1;

    WAVEFORMATEX wfx = {0};
    const uint8_t* fmtChunk = p + 12;
    int foundFmt = 0, foundData = 0;
    DWORD dataOffset = 0, dataSize = 0;

    while (fmtChunk + 8 < p + size) {
        DWORD chunkSize = fmtChunk[4] | (fmtChunk[5] << 8) | (fmtChunk[6] << 16) | (fmtChunk[7] << 8);
        if (fmtChunk[0] == 'f' && fmtChunk[1] == 'm' && fmtChunk[2] == 't' && fmtChunk[3] == ' ') {
            if (chunkSize >= 16) {
                wfx.wFormatTag = fmtChunk[8] | (fmtChunk[9] << 8);
                wfx.nChannels = fmtChunk[10] | (fmtChunk[11] << 8);
                wfx.nSamplesPerSec = fmtChunk[12] | (fmtChunk[13] << 8) | (fmtChunk[14] << 16) | (fmtChunk[15] << 8);
                wfx.nAvgBytesPerSec = fmtChunk[16] | (fmtChunk[17] << 8) | (fmtChunk[18] << 16) | (fmtChunk[19] << 8);
                wfx.nBlockAlign = fmtChunk[20] | (fmtChunk[21] << 8);
                wfx.wBitsPerSample = fmtChunk[22] | (fmtChunk[23] << 8);
                foundFmt = 1;
            }
        } else if (fmtChunk[0] == 'd' && fmtChunk[1] == 'a' && fmtChunk[2] == 't' && fmtChunk[3] == 'a') {
            dataOffset = (DWORD)(fmtChunk - p) + 8;
            dataSize = chunkSize;
            foundData = 1;
            break;
        }
        fmtChunk += 8 + chunkSize;
        if (chunkSize & 1) fmtChunk++;
    }

    if (!foundFmt || !foundData) return -1;

    DSBUFFERDESC bufDesc = {0};
    bufDesc.dwSize = sizeof(DSBUFFERDESC);
    bufDesc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS;
    bufDesc.dwBufferBytes = dataSize;
    bufDesc.lpwfxFormat = &wfx;

    LPDIRECTSOUNDBUFFER pBuf = NULL;
    HRESULT hr = IDirectSound_CreateSoundBuffer(g_game.pDS, &bufDesc, &pBuf, NULL);
    if (FAILED(hr)) return -1;

    void* ptr1, *ptr2;
    DWORD bytes1, bytes2;
    hr = IDirectSoundBuffer_Lock(pBuf, 0, dataSize, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (SUCCEEDED(hr)) {
        memcpy(ptr1, p + dataOffset, bytes1);
        if (ptr2 && bytes2 > 0) {
            memcpy(ptr2, p + dataOffset + bytes1, bytes2);
        }
        IDirectSoundBuffer_Unlock(pBuf, ptr1, bytes1, ptr2, bytes2);
    }

    g_game.sounds[idx].buffer = pBuf;
    g_game.sounds[idx].inUse = true;
    strncpy(g_game.sounds[idx].name, name, sizeof(g_game.sounds[idx].name) - 1);
    g_game.soundCount++;

    Log_Print("Audio: loaded '%s' (%lu bytes, %lu Hz, %dch)\n",
              name, dataSize, wfx.nSamplesPerSec, wfx.nChannels);
    return idx;
}

int Audio_LoadFromResource(const char* name, int resId) {
    HRSRC hRes = FindResourceA(g_game.hInstance, MAKEINTRESOURCEA(resId), "WAVE");
    if (!hRes) return -1;
    HGLOBAL hGlob = LoadResource(g_game.hInstance, hRes);
    if (!hGlob) return -1;
    const uint8_t* data = (const uint8_t*)LockResource(hGlob);
    DWORD size = SizeofResource(g_game.hInstance, hRes);
    return Audio_LoadWAV(name, data, size);
}

void Audio_Play(int id, bool loop) {
    if (id < 0 || id >= MAX_SOUNDS || !g_game.sounds[id].inUse) return;
    IDirectSoundBuffer_SetCurrentPosition(g_game.sounds[id].buffer, 0);
    IDirectSoundBuffer_Play(g_game.sounds[id].buffer, 0, 0, loop ? DSBPLAY_LOOPING : 0);
}

void Audio_Stop(int id) {
    if (id < 0 || id >= MAX_SOUNDS || !g_game.sounds[id].inUse) return;
    IDirectSoundBuffer_Stop(g_game.sounds[id].buffer);
    IDirectSoundBuffer_SetCurrentPosition(g_game.sounds[id].buffer, 0);
}

void Audio_StopAll(void) {
    int i;
    for (i = 0; i < MAX_SOUNDS; i++) {
        if (g_game.sounds[i].inUse) {
            Audio_Stop(i);
        }
    }
}

void Audio_SetVolume(int id, long volume) {
    if (id < 0 || id >= MAX_SOUNDS || !g_game.sounds[id].inUse) return;
    IDirectSoundBuffer_SetVolume(g_game.sounds[id].buffer, volume);
}

void Audio_Shutdown(void) {
    int i;
    for (i = 0; i < MAX_SOUNDS; i++) {
        if (g_game.sounds[i].inUse && g_game.sounds[i].buffer) {
            IDirectSoundBuffer_Release(g_game.sounds[i].buffer);
        }
    }
    if (g_game.pPrimaryBuffer) {
        IDirectSoundBuffer_Release(g_game.pPrimaryBuffer);
        g_game.pPrimaryBuffer = NULL;
    }
    if (g_game.pDS) {
        IDirectSound_Release(g_game.pDS);
        g_game.pDS = NULL;
    }
    memset(g_game.sounds, 0, sizeof(g_game.sounds));
    g_game.soundCount = 0;
    Log_Print("Audio: shutdown\n");
}

bool BGM_Load(const char* path) {
    BGM_Shutdown();

    FILE* f = fopen(path, "rb");
    if (!f) {
        Log_Print("BGM: failed to open '%s'\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    DWORD fileSize = (DWORD)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(fileSize);
    if (!data) {
        fclose(f);
        return false;
    }
    fread(data, 1, fileSize, f);
    fclose(f);

    // Verificar se é WAV
    if (fileSize >= 44 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
        Log_Print("BGM: loading WAV file '%s'\n", path);
        free(data);
        return BGM_LoadWAV(path);
    }

    // Tentar decodificar como MP3
    Log_Print("BGM: loading MP3 file '%s'\n", path);
    free(data);
    return BGM_LoadMP3(path);
}

bool BGM_LoadWAV(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        Log_Print("BGM: failed to open '%s'\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    DWORD fileSize = (DWORD)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(fileSize);
    if (!data) {
        fclose(f);
        return false;
    }
    fread(data, 1, fileSize, f);
    fclose(f);

    if (fileSize < 44 || data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F' ||
        data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') {
        Log_Print("BGM: '%s' is not a valid WAV file\n", path);
        free(data);
        return false;
    }

    WAVEFORMATEX wfx = {0};
    const uint8_t* p = data;
    const uint8_t* fmtChunk = p + 12;
    int foundFmt = 0, foundData = 0;
    DWORD dataOffset = 0, dataSize = 0;

    while (fmtChunk + 8 < p + fileSize) {
        DWORD chunkSize = fmtChunk[4] | (fmtChunk[5] << 8) | (fmtChunk[6] << 16) | (fmtChunk[7] << 24);
        if (fmtChunk[0] == 'f' && fmtChunk[1] == 'm' && fmtChunk[2] == 't' && fmtChunk[3] == ' ') {
            if (chunkSize >= 16) {
                wfx.wFormatTag = fmtChunk[8] | (fmtChunk[9] << 8);
                wfx.nChannels = fmtChunk[10] | (fmtChunk[11] << 8);
                wfx.nSamplesPerSec = fmtChunk[12] | (fmtChunk[13] << 8) | (fmtChunk[14] << 16) | (fmtChunk[15] << 24);
                wfx.nAvgBytesPerSec = fmtChunk[16] | (fmtChunk[17] << 8) | (fmtChunk[18] << 16) | (fmtChunk[19] << 24);
                wfx.nBlockAlign = fmtChunk[20] | (fmtChunk[21] << 8);
                wfx.wBitsPerSample = fmtChunk[22] | (fmtChunk[23] << 8);
                foundFmt = 1;
            }
        } else if (fmtChunk[0] == 'd' && fmtChunk[1] == 'a' && fmtChunk[2] == 't' && fmtChunk[3] == 'a') {
            dataOffset = (DWORD)(fmtChunk - p) + 8;
            dataSize = chunkSize;
            foundData = 1;
            break;
        }
        fmtChunk += 8 + chunkSize;
        if (chunkSize & 1) fmtChunk++;
    }

    if (!foundFmt || !foundData) {
        Log_Print("BGM: '%s' missing fmt or data chunk\n", path);
        free(data);
        return false;
    }

    DSBUFFERDESC bufDesc = {0};
    bufDesc.dwSize = sizeof(DSBUFFERDESC);
    bufDesc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS;
    bufDesc.dwBufferBytes = dataSize;
    bufDesc.lpwfxFormat = &wfx;

    LPDIRECTSOUNDBUFFER pBuf = NULL;
    HRESULT hr = IDirectSound_CreateSoundBuffer(g_game.pDS, &bufDesc, &pBuf, NULL);
    if (FAILED(hr)) {
        Log_Print("BGM: CreateSoundBuffer failed (0x%08lx)\n", hr);
        free(data);
        return false;
    }

    void* ptr1, *ptr2;
    DWORD bytes1, bytes2;
    hr = IDirectSoundBuffer_Lock(pBuf, 0, dataSize, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (SUCCEEDED(hr)) {
        memcpy(ptr1, p + dataOffset, bytes1);
        if (ptr2 && bytes2 > 0) {
            memcpy(ptr2, p + dataOffset + bytes1, bytes2);
        }
        IDirectSoundBuffer_Unlock(pBuf, ptr1, bytes1, ptr2, bytes2);
    }

    free(data);

    g_game.bgm.buffer = pBuf;
    g_game.bgm.playing = false;
    g_game.bgm.looping = false;
    g_game.bgm.useMCI = false;
    g_game.bgm.dataSize = dataSize;
    strncpy(g_game.bgm.name, path, sizeof(g_game.bgm.name) - 1);

    Log_Print("BGM: loaded WAV '%s' (%lu bytes, %lu Hz, %dch)\n",
              path, dataSize, wfx.nSamplesPerSec, wfx.nChannels);
    return true;
}

bool BGM_LoadMP3(const char* path) {
    Log_Print("BGM_LoadMP3: starting for '%s'\n", path);
    Log_Flush();
    
    drmp3 mp3;
    if (!drmp3_init_file(&mp3, path, NULL)) {
        Log_Print("BGM: failed to decode MP3 '%s'\n", path);
        Log_Flush();
        return false;
    }

    drmp3_uint64 totalFrames = drmp3_get_pcm_frame_count(&mp3);
    drmp3_uint16 channels = mp3.channels;
    drmp3_uint32 sampleRate = mp3.sampleRate;

    Log_Print("BGM: MP3 has %llu frames, %u Hz, %u channels\n",
              totalFrames, sampleRate, channels);
    Log_Flush();

    // Verificar overflow - DWORD é 32-bit (max 4GB)
    drmp3_uint64 totalBytes = totalFrames * channels * sizeof(drmp3_int16);
    if (totalBytes > 0xFFFFFFFF) {
        Log_Print("BGM: MP3 too large (%llu bytes > 4GB)\n", totalBytes);
        Log_Flush();
        drmp3_uninit(&mp3);
        return false;
    }

    Log_Print("BGM: allocating %llu bytes\n", totalBytes);
    Log_Flush();
    
    drmp3_int16* pcmData = (drmp3_int16*)malloc((size_t)totalBytes);
    if (!pcmData) {
        Log_Print("BGM: failed to allocate %llu bytes for MP3\n", totalBytes);
        Log_Flush();
        drmp3_uninit(&mp3);
        return false;
    }

    Log_Print("BGM: reading PCM frames...\n");
    Log_Flush();
    
    drmp3_uint64 framesRead = drmp3_read_pcm_frames_s16(&mp3, totalFrames, pcmData);
    drmp3_uninit(&mp3);

    Log_Print("BGM: read %llu frames\n", framesRead);
    Log_Flush();

    if (framesRead == 0) {
        Log_Print("BGM: failed to read PCM frames from '%s'\n", path);
        Log_Flush();
        free(pcmData);
        return false;
    }

    DWORD dataSize = (DWORD)(framesRead * channels * sizeof(drmp3_int16));
    Log_Print("BGM: PCM data size: %lu bytes\n", dataSize);
    Log_Flush();

    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = channels;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    DSBUFFERDESC bufDesc = {0};
    bufDesc.dwSize = sizeof(DSBUFFERDESC);
    bufDesc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS;
    bufDesc.dwBufferBytes = dataSize;
    bufDesc.lpwfxFormat = &wfx;

    LPDIRECTSOUNDBUFFER pBuf = NULL;
    HRESULT hr = IDirectSound_CreateSoundBuffer(g_game.pDS, &bufDesc, &pBuf, NULL);
    if (FAILED(hr)) {
        Log_Print("BGM: CreateSoundBuffer failed for MP3 (0x%08lx)\n", hr);
        Log_Flush();
        free(pcmData);
        return false;
    }

    void* ptr1, *ptr2;
    DWORD bytes1, bytes2;
    hr = IDirectSoundBuffer_Lock(pBuf, 0, dataSize, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (SUCCEEDED(hr)) {
        memcpy(ptr1, pcmData, bytes1);
        if (ptr2 && bytes2 > 0) {
            memcpy(ptr2, (uint8_t*)pcmData + bytes1, bytes2);
        }
        IDirectSoundBuffer_Unlock(pBuf, ptr1, bytes1, ptr2, bytes2);
    } else {
        Log_Print("BGM: Lock buffer failed (0x%08lx)\n", hr);
        Log_Flush();
        IDirectSoundBuffer_Release(pBuf);
        free(pcmData);
        return false;
    }

    free(pcmData);

    g_game.bgm.buffer = pBuf;
    g_game.bgm.playing = false;
    g_game.bgm.looping = false;
    g_game.bgm.useMCI = false;
    g_game.bgm.dataSize = dataSize;
    strncpy(g_game.bgm.name, path, sizeof(g_game.bgm.name) - 1);

    Log_Print("BGM: loaded MP3 '%s' (%lu bytes PCM, %lu Hz, %dch)\n",
              path, dataSize, sampleRate, channels);
    Log_Flush();
    return true;
}

bool BGM_LoadAUD(int songId, bool preview) {
    char path[MAX_PATH];
    const char* exts[] = { ".mp3", ".MP3", ".wav", ".WAV" };

    if (preview) {
        for (int ei = 0; ei < 4; ei++) {
            snprintf(path, sizeof(path), "%s\\AUDIO\\D%d%s",
                     g_game.currentDirectory, songId, exts[ei]);
            if (BGM_Load(path)) return true;
        }
        for (int ei = 0; ei < 4; ei++) {
            snprintf(path, sizeof(path), "%s\\AUDIO\\%d%s",
                     g_game.currentDirectory, songId, exts[ei]);
            if (BGM_Load(path)) return true;
        }
    } else {
        for (int ei = 0; ei < 4; ei++) {
            snprintf(path, sizeof(path), "%s\\AUDIO\\%d%s",
                     g_game.currentDirectory, songId, exts[ei]);
            if (BGM_Load(path)) return true;
        }
        for (int ei = 0; ei < 4; ei++) {
            snprintf(path, sizeof(path), "%s\\AUDIO\\D%d%s",
                     g_game.currentDirectory, songId, exts[ei]);
            if (BGM_Load(path)) return true;
        }
    }

    snprintf(path, sizeof(path), "%s\\AUDIO\\%d.AUD", g_game.currentDirectory, songId);
    return BGM_LoadAUDDirect(path);
}

static bool dshow_load(const char* mp3path);
bool BGM_LoadAUDDirect(const char* path) {
    Log_Print("BGM: loading .AUD '%s'\n", path);

    BGM_Shutdown();

    FILE* f = fopen(path, "rb");
    if (!f) { Log_Print("BGM: failed to open '%s'\n", path); return false; }

    fseek(f, 0, SEEK_END);
    uint32_t fileSize = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* fileData = (uint8_t*)malloc(fileSize);
    if (!fileData) { fclose(f); return false; }
    fread(fileData, 1, fileSize, f);
    fclose(f);

    uint8_t* decData = NULL;
    uint32_t decSize = 0;
    if (!Audio_DecryptAUD(fileData, fileSize, &decData, &decSize)) {
        Log_Print("BGM: failed to decrypt .AUD '%s'\n", path);
        free(fileData);
        return false;
    }
    free(fileData);

    Log_Print("BGM: decrypted %lu bytes from .AUD\n", decSize);

    g_game.bgm.mciPath[0] = '\0';

    char tmpPath[MAX_PATH];
    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    snprintf(tmpPath, sizeof(tmpPath), "%spumpy_bgm_%04x.mp3",
             tmpDir, (unsigned)GetCurrentThreadId());

    FILE* ftmp = fopen(tmpPath, "wb");
    if (!ftmp) {
        Log_Print("BGM: failed to create temp file '%s'\n", tmpPath);
        free(decData); return false;
    }
    fwrite(decData, 1, decSize, ftmp);
    fclose(ftmp);

    // Try DirectShow first (faithful to original CLSID_FilterGraph + RenderFile)
    strncpy(g_game.bgm.name, path, sizeof(g_game.bgm.name) - 1);
    if (dshow_load(tmpPath)) {
        free(decData);
        g_game.bgm.playing = false;
        Log_Print("BGM: loaded via DirectShow\n");
        return true;
    }

    Log_Print("BGM: DirectShow failed, using MCI\n");
    free(decData);

    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "open \"%s\" type MPEGVideo alias pumpybgm", tmpPath);
    MCIERROR mciErr = mciSendStringA(cmd, NULL, 0, NULL);
    if (mciErr != 0) {
        Log_Print("BGM: MCI open failed for '%s' (err=%u)\n", tmpPath, mciErr);
        DeleteFileA(tmpPath);
        return false;
    }

    strncpy(g_game.bgm.mciPath, tmpPath, sizeof(g_game.bgm.mciPath) - 1);
    g_game.bgm.useMCI = true;
    g_game.bgm.playing = false;
    g_game.bgm.looping = false;
    g_game.bgm.buffer = NULL;

    mciSendStringA("set pumpybgm time format milliseconds", NULL, 0, NULL);
    char buf[32] = {0};
    if (mciSendStringA("status pumpybgm length", buf, sizeof(buf), NULL) == 0) {
        g_game.bgm.durationMs = (uint32_t)atol(buf);
        Log_Print("BGM: duration %lu ms\n", g_game.bgm.durationMs);
    } else {
        g_game.bgm.durationMs = 0;
    }

    Log_Print("BGM: loaded .AUD via MCI '%s'\n", path);
    return true;
}

// ---- DirectShow (fields stored here, NOT in BGM struct, to avoid layout shift) ----
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*,const GUID*,void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *AddFilter)(void*,void*,LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *RemoveFilter)(void*,void*);
    HRESULT (STDMETHODCALLTYPE *EnumFilters)(void*,void**);
    HRESULT (STDMETHODCALLTYPE *FindFilterByName)(void*,LPCWSTR,void**);
    HRESULT (STDMETHODCALLTYPE *ConnectDirect)(void*,void*,void*,const void*);
    HRESULT (STDMETHODCALLTYPE *Reconnect)(void*,void*);
    HRESULT (STDMETHODCALLTYPE *Disconnect)(void*,void*);
    HRESULT (STDMETHODCALLTYPE *SetDefaultSyncSource)(void*);
    HRESULT (STDMETHODCALLTYPE *Connect)(void*,void*,void*);
    HRESULT (STDMETHODCALLTYPE *Render)(void*,void*);
    HRESULT (STDMETHODCALLTYPE *RenderFile)(void*,LPCWSTR,LPCWSTR);
} VtblGraph;

typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*,const GUID*,void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetTypeInfoCount)(void*,UINT*);
    HRESULT (STDMETHODCALLTYPE *GetTypeInfo)(void*,UINT,LCID,void**);
    HRESULT (STDMETHODCALLTYPE *GetIDsOfNames)(void*,const GUID*,OLECHAR**,UINT,LCID,DISPID*);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void*,DISPID,const GUID*,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,UINT*);
    HRESULT (STDMETHODCALLTYPE *Run)(void*);
    HRESULT (STDMETHODCALLTYPE *Stop)(void*);
    HRESULT (STDMETHODCALLTYPE *GetState)(void*,LONG,LONG*);
} VtblControl;

typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*,const GUID*,void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetTypeInfoCount)(void*,UINT*);
    HRESULT (STDMETHODCALLTYPE *GetTypeInfo)(void*,UINT,LCID,void**);
    HRESULT (STDMETHODCALLTYPE *GetIDsOfNames)(void*,const GUID*,OLECHAR**,UINT,LCID,DISPID*);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void*,DISPID,const GUID*,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,UINT*);
    HRESULT (STDMETHODCALLTYPE *get_Duration)(void*,double*);
    HRESULT (STDMETHODCALLTYPE *put_CurrentPosition)(void*,double);
    HRESULT (STDMETHODCALLTYPE *get_CurrentPosition)(void*,double*);
} VtblPos;

static void* ds_graph  = NULL;
static void* ds_ctrl   = NULL;
static void* ds_pos    = NULL;
static bool  ds_active = false;

static bool dshow_load(const char* mp3path)
{
    void* pG = NULL, *pC = NULL, *pP = NULL;

    // {E436EBB3-524F-11CE-9F53-0020AF0BA770}
    GUID clsid = {0xE436EBB3,0x524F,0x11CE,{0x9F,0x53,0x00,0x20,0xAF,0x0B,0xA7,0x70}};
    // IID_IGraphBuilder = {56A868A9-0AD4-11CE-B03A-0020AF0BA770}
    GUID iidGB = {0x56A868A9,0x0AD4,0x11CE,{0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70}};
    // IID_IMediaControl = {56A868B1-0AD4-11CE-B03A-0020AF0BA770}
    GUID iidMC = {0x56A868B1,0x0AD4,0x11CE,{0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70}};
    // IID_IMediaPosition = {56A868B2-0AD4-11CE-B03A-0020AF0BA770}
    GUID iidMP = {0x56A868B2,0x0AD4,0x11CE,{0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70}};

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    Log_Print("DS: CoCreateInstance\n");
    HRESULT hr = CoCreateInstance(&clsid, NULL, 1, &iidGB, &pG);
    if (FAILED(hr)) { Log_Print("DS: CoCreateInstance failed hr=0x%08lx\n", hr); return false; }

    hr = ((VtblGraph*)(*(void**)pG))->QueryInterface(pG, &iidMC, &pC);
    if (FAILED(hr)) { ((VtblGraph*)(*(void**)pG))->Release(pG); return false; }

    hr = ((VtblGraph*)(*(void**)pG))->QueryInterface(pG, &iidMP, &pP);
    if (FAILED(hr)) { ((VtblGraph*)(*(void**)pG))->Release(pG); ((VtblGraph*)(*(void**)pC))->Release(pC); return false; }

    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, mp3path, -1, wpath, MAX_PATH);

    hr = ((VtblGraph*)(*(void**)pG))->RenderFile(pG, wpath, NULL);
    if (FAILED(hr)) {
        Log_Print("DS: RenderFile failed 0x%08lx\n", hr);
        ((VtblGraph*)(*(void**)pP))->Release(pP);
        ((VtblGraph*)(*(void**)pC))->Release(pC);
        ((VtblGraph*)(*(void**)pG))->Release(pG);
        return false;
    }

    // Run graph immediately (original starts playback at RenderFile time)
    ((VtblControl*)(*(void**)pC))->Run(pC);
    Log_Print("DS: Run OK\n");

    ds_graph  = pG;
    ds_ctrl   = pC;
    ds_pos    = pP;
    ds_active = true;
    Log_Print("DS: loaded OK\n");
    return true;
}

static void dshow_stop(void) {
    if (ds_ctrl) ((VtblControl*)(*(void**)ds_ctrl))->Stop(ds_ctrl);
    if (ds_pos)  ((VtblGraph*)(*(void**)ds_pos))->Release(ds_pos);
    if (ds_ctrl) ((VtblGraph*)(*(void**)ds_ctrl))->Release(ds_ctrl);
    if (ds_graph)((VtblGraph*)(*(void**)ds_graph))->Release(ds_graph);
    ds_graph = ds_ctrl = ds_pos = NULL;
    ds_active = false;
}

void BGM_Play(bool loop) {
    if (ds_active) {
        // Graph already running (started in dshow_load)
        g_game.bgm.playing = true;
        g_game.bgm.looping = loop;
        return;
    }
    if (g_game.bgm.useMCI) {
        if (g_game.bgm.playing) BGM_Stop();
        mciSendStringA("seek pumpybgm to start", NULL, 0, NULL);
        mciSendStringA(loop ? "play pumpybgm repeat" : "play pumpybgm", NULL, 0, NULL);
        g_game.bgm.playing = true;
        g_game.bgm.looping = loop;
        return;
    }

    if (!g_game.bgm.buffer) return;
    if (g_game.bgm.playing) BGM_Stop();
    IDirectSoundBuffer_SetCurrentPosition(g_game.bgm.buffer, 0);
    HRESULT hr = IDirectSoundBuffer_Play(g_game.bgm.buffer, 0, 0, loop ? DSBPLAY_LOOPING : 0);
    if (SUCCEEDED(hr)) {
        g_game.bgm.playing = true;
        g_game.bgm.looping = loop;
    } else {
        g_game.bgm.playing = false;
    }
}

void BGM_Stop(void) {
    if (ds_active) { dshow_stop(); g_game.bgm.playing = false; return; }
    if (g_game.bgm.useMCI) {
        mciSendStringA("stop pumpybgm", NULL, 0, NULL);
        mciSendStringA("close pumpybgm", NULL, 0, NULL);
        g_game.bgm.playing = false;
        return;
    }

    if (!g_game.bgm.buffer || !g_game.bgm.playing) return;
    IDirectSoundBuffer_Stop(g_game.bgm.buffer);
    IDirectSoundBuffer_SetCurrentPosition(g_game.bgm.buffer, 0);
    g_game.bgm.playing = false;
}

uint32_t BGM_GetPositionMs(void) {
    if (ds_pos) {
        double pos = 0;
        if (SUCCEEDED(((VtblPos*)(*(void**)ds_pos))->get_CurrentPosition(ds_pos, &pos)))
            return (uint32_t)(pos * 1000.0);
    }
    return 0;
}

bool BGM_IsDSActive(void) { return ds_active; }

bool BGM_IsPlaying(void) {
    if (ds_active) return g_game.bgm.playing;
    if (g_game.bgm.useMCI) {
        char result[32] = {0};
        mciSendStringA("status pumpybgm mode", result, sizeof(result), NULL);
        return (_stricmp(result, "playing") == 0);
    }
    return g_game.bgm.playing;
}

void BGM_SetVolume(long volume) {
    if (g_game.bgm.useMCI) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "setaudio pumpybgm volume to %ld", volume);
        mciSendStringA(cmd, NULL, 0, NULL);
        return;
    }
    if (!g_game.bgm.buffer) return;
    IDirectSoundBuffer_SetVolume(g_game.bgm.buffer, volume);
}

void BGM_Shutdown(void) {
    if (ds_active) { dshow_stop(); g_game.bgm.playing = false; g_game.bgm.name[0] = '\0'; return; }
    if (g_game.bgm.useMCI) {
        mciSendStringA("stop pumpybgm", NULL, 0, NULL);
        mciSendStringA("close pumpybgm", NULL, 0, NULL);
        if (g_game.bgm.mciPath[0]) {
            DeleteFileA(g_game.bgm.mciPath);
            g_game.bgm.mciPath[0] = '\0';
        }
        g_game.bgm.useMCI = false;
        g_game.bgm.playing = false;
        g_game.bgm.name[0] = '\0';
        return;
    }

    if (g_game.bgm.buffer) {
        IDirectSoundBuffer_Stop(g_game.bgm.buffer);
        IDirectSoundBuffer_Release(g_game.bgm.buffer);
        g_game.bgm.buffer = NULL;
    }
    g_game.bgm.playing = false;
    g_game.bgm.looping = false;
    g_game.bgm.name[0] = '\0';
}
