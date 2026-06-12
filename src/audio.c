#include "pumpy.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

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
        // Preview: try D prefix (short preview) first, fall back to no prefix
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
        // Full song: try no prefix first, then D prefix
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
    Log_Print("BGM: .AUD format not supported: %s\n", path);
    return false;
}

void BGM_Play(bool loop) {
    if (!g_game.bgm.buffer) return;

    if (g_game.bgm.playing) {
        BGM_Stop();
    }

    IDirectSoundBuffer_SetCurrentPosition(g_game.bgm.buffer, 0);
    HRESULT hr = IDirectSoundBuffer_Play(g_game.bgm.buffer, 0, 0, loop ? DSBPLAY_LOOPING : 0);
    if (SUCCEEDED(hr)) {
        g_game.bgm.playing = true;
        g_game.bgm.looping = loop;
    }
}

void BGM_Stop(void) {
    if (!g_game.bgm.buffer || !g_game.bgm.playing) return;

    IDirectSoundBuffer_Stop(g_game.bgm.buffer);
    IDirectSoundBuffer_SetCurrentPosition(g_game.bgm.buffer, 0);
    g_game.bgm.playing = false;
}

void BGM_SetVolume(long volume) {
    if (!g_game.bgm.buffer) return;
    IDirectSoundBuffer_SetVolume(g_game.bgm.buffer, volume);
}

void BGM_Shutdown(void) {
    if (g_game.bgm.buffer) {
        IDirectSoundBuffer_Stop(g_game.bgm.buffer);
        IDirectSoundBuffer_Release(g_game.bgm.buffer);
        g_game.bgm.buffer = NULL;
    }
    g_game.bgm.playing = false;
    g_game.bgm.looping = false;
    g_game.bgm.name[0] = '\0';
}
