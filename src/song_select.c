#include "pumpy.h"
#include <string.h>

static int prevSongId = -1;
static int previewState = 0;
static int selectedState = 0;
static int g_cdTexIds[45];
static bool g_cdLoaded = false;
static float g_songAnimPos = 0.0f;
static int g_songAnimCounter = 0;

static int g_carrosselFrame = 588;
static int g_carrosselDir = 0;
static int g_carrosselTarget = 588;
static bool g_carrosselIntro = true;
static int g_introFrame = 0;

static const int g_slotFrameOffset[7] = { -48, -32, -16, 0, +16, +32, +48 };

static const int g_modeOrder[7] = {0, 1, 5, 3, 4, 2, 6};
static const char* g_modeNames[7] = {"NORMAL","HARD","CRAZY","HALFDOUBLE","DIVISION","DOUBLE","NIGHTMARE"};

typedef struct {
    int frame;
    float x, sx, alpha;
} BoxKeyframe;

static const BoxKeyframe g_boxKF[7] = {
    {540, -235.0f, 0.50f, 0.0f},
    {556, -175.0f, 0.65f, 0.5f},
    {572, -110.0f, 0.80f, 1.0f},
    {588,    0.0f, 1.00f, 1.0f},
    {604,  110.0f, 0.80f, 1.0f},
    {620,  175.0f, 0.65f, 0.5f},
    {636,  235.0f, 0.50f, 0.0f},
};

static void EvalBoxFrame(float frame, float* outX, float* outSc, float* outAlpha) {
    if (frame <= 540.0f) { *outX = -235.0f; *outSc = 0.50f; *outAlpha = 0.0f; return; }
    if (frame >= 636.0f) { *outX =  235.0f; *outSc = 0.50f; *outAlpha = 0.0f; return; }
    for (int i = 0; i < 6; i++) {
        if (frame >= g_boxKF[i].frame && frame <= g_boxKF[i+1].frame) {
            float t = (frame - g_boxKF[i].frame) / (float)(g_boxKF[i+1].frame - g_boxKF[i].frame);
            *outX     = g_boxKF[i].x     + t * (g_boxKF[i+1].x     - g_boxKF[i].x);
            *outSc    = g_boxKF[i].sx    + t * (g_boxKF[i+1].sx    - g_boxKF[i].sx);
            *outAlpha = g_boxKF[i].alpha + t * (g_boxKF[i+1].alpha  - g_boxKF[i].alpha);
            return;
        }
    }
}

static int g_cdSongs[45][2] = {
    {108,109},{112,205},{212,301},{312,318},{320,321},
    {401,402},{403,404},{405,413},{503,505},{508,516},
    {701,703},{704,705},{707,711},{712,714},{719,720},
    {721,722},{730,733},{734,735},{736,801},{802,803},
    {804,805},{806,807},{808,809},{810,811},{812,813},
    {814,815},{816,817},{818,819},{820,911},{916,921},
    {101,104},{202,203},{204,212},{302,303},{305,306},
    {310,311},{312,414},{501,504},{507,517},{902,906},
    {913,915},{922,102},{821,822},{823,824},{825,826}
};

static int findCdForSong(int songId, int* outHalf) {
    for (int i = 0; i < 45; i++) {
        if (g_cdSongs[i][0] == songId) { *outHalf = 0; return i; }
        if (g_cdSongs[i][1] == songId) { *outHalf = 1; return i; }
    }
    return -1;
}

static void loadCdTextures(void) {
    if (g_cdLoaded) return;
    char datPath[MAX_PATH];
    snprintf(datPath, sizeof(datPath), "%s\\BGA\\90.DAT", g_game.currentDirectory);
    if (!RES_Open(datPath)) { g_cdLoaded = true; return; }
    for (int i = 0; i < 45; i++) {
        char name[32];
        snprintf(name, sizeof(name), "CD%02d.png", i + 1);
        g_cdTexIds[i] = loadTextureFromRES(name);
    }
    RES_Close();
    g_cdLoaded = true;
}

void SongSelect_Reset(void) {
    prevSongId = -1;
    previewState = 0;
    selectedState = 0;
    g_game.selectedSongIndex = 0;
    g_game.songSelectHighlighted = 0;
    g_game.previewSongId = -1;
    g_game.selectedModeIndex = Song_FindMode(&g_game.songDB, "EASY");
    if (g_game.selectedModeIndex < 0 && g_game.songDB.modeCount > 0)
        g_game.selectedModeIndex = 0;
    g_songAnimCounter = 0;
    g_songAnimPos = 0.0f;
    g_carrosselFrame = 588;
    g_carrosselDir = 0;
    g_carrosselTarget = 588;
    g_carrosselIntro = true;
    g_introFrame = 0;
    loadCdTextures();
}

static void stopPreview(void) {
    if (previewState) {
        BGM_Stop();
        previewState = 0;
    }
}

static void playPreview(int songId) {
    stopPreview();
    if (songId < 0) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\AUDIO\\D%d.AUD", g_game.currentDirectory, songId);
    if (BGM_LoadAUDDirect(path)) {
        BGM_Play(false);
        previewState = 1;
    }
    prevSongId = songId;
}

void Gamestate_UpdateSongSelect(float dt) {
    if (g_game.state == STATE_SONG_SELECT_B) {
        float a = g_game.globalColorA - dt;
        if (a <= 0.0f) {
            Render_SetGlobalColor(0, 0, 0, 0);
            if (g_game.stateFrame > 60) {
                g_game.state = STATE_SONG_SELECT;
                g_game.stateFrame = 0;
                previewState = 0;
                prevSongId = -1;
                selectedState = 0;
            }
        } else {
            Render_SetGlobalColor(0, 0, 0, a);
        }
        return;
    }

    SongDB* db = &g_game.songDB;

    if (Input_IsPadHit(0, PAD_UR)) {
        selectedState = 0;
        stopPreview();
        int curDisp = -1;
        for (int i = 0; i < 7; i++)
            if (g_modeOrder[i] == g_game.selectedModeIndex) { curDisp = i; break; }
        if (curDisp >= 0) {
            curDisp = (curDisp + 1) % 7;
            g_game.selectedModeIndex = g_modeOrder[curDisp];
        }
        g_game.selectedSongIndex = 0;
        prevSongId = -1;
        g_songAnimCounter = 0;
        g_carrosselIntro = true;
        g_introFrame = 0;
        g_carrosselFrame = 588;
        g_carrosselDir = 0;
        g_carrosselTarget = 588;
    }

    if (Input_IsPadHit(0, PAD_UL)) {
        selectedState = 0;
        stopPreview();
        int curDisp = -1;
        for (int i = 0; i < 7; i++)
            if (g_modeOrder[i] == g_game.selectedModeIndex) { curDisp = i; break; }
        if (curDisp >= 0) {
            curDisp = (curDisp + 5) % 7;
            g_game.selectedModeIndex = g_modeOrder[curDisp];
        }
        g_game.selectedSongIndex = 0;
        prevSongId = -1;
        g_songAnimCounter = 0;
        g_carrosselIntro = true;
        g_introFrame = 0;
        g_carrosselFrame = 588;
        g_carrosselDir = 0;
        g_carrosselTarget = 588;
    }

    SongMode* mode = &db->modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;
    if (songCount == 0) return;

    if (Input_IsPadHit(0, PAD_DR)) {
        selectedState = 0;
        stopPreview();
        g_game.selectedSongIndex++;
        if (g_game.selectedSongIndex >= songCount)
            g_game.selectedSongIndex = 0;
        prevSongId = -1;
        if (g_carrosselDir == 0) {
            g_carrosselTarget = g_carrosselFrame - 16;
            g_carrosselDir = -1;
        }
    }

    if (Input_IsPadHit(0, PAD_DL)) {
        selectedState = 0;
        stopPreview();
        g_game.selectedSongIndex--;
        if (g_game.selectedSongIndex < 0)
            g_game.selectedSongIndex = songCount - 1;
        prevSongId = -1;
        if (g_carrosselDir == 0) {
            g_carrosselTarget = g_carrosselFrame + 16;
            g_carrosselDir = +1;
        }
    }

    // Avanca o frame do carrossel — velocidade 1 = mais lento
    if (g_carrosselDir != 0) {
        g_carrosselFrame += g_carrosselDir * 1;

        if (g_carrosselDir == +1 && g_carrosselFrame >= g_carrosselTarget) {
            g_carrosselFrame = 588;
            g_carrosselTarget = 588;
            g_carrosselDir = 0;
        } else if (g_carrosselDir == -1 && g_carrosselFrame <= g_carrosselTarget) {
            g_carrosselFrame = 588;
            g_carrosselTarget = 588;
            g_carrosselDir = 0;
        }
    }

    int songId = mode->songIds[g_game.selectedSongIndex];
    if (songId != prevSongId)
        playPreview(songId);

    if (Input_IsPadHit(0, PAD_C)) {
        if (selectedState) {
            stopPreview();
            g_game.selectedDifficulty = mode->difficulties[g_game.selectedSongIndex];
            selectedState = 0;
            Loading_Enter(songId);
        } else {
            selectedState = 1;
        }
    }
}

void Gamestate_RenderSongSelect(void) {
    if (g_game.state != STATE_SONG_SELECT && g_game.state != STATE_SONG_SELECT_B)
        return;
    if (g_game.state != STATE_SONG_SELECT) return;

    if (g_game.bgaPicCount > 0) {
        int f = g_game.bgaFrame;
        BGA_SetEventLayer(0, f % 60, 0);

        BGA_SetEventLayer(0, 0, 0x1a);
        BGA_SetEventLayer(0, 0, 0x24);

        // Boxes animando via Entry 11 — todos os 7 slots com frame offset proprio
        for (int si = 0; si < 7; si++) {
            int slotFrame = g_carrosselFrame + g_slotFrameOffset[si];
            if (slotFrame < 540) slotFrame = 540;
            if (slotFrame > 636) slotFrame = 636;
            BGA_SetEventLayer(0, slotFrame, 0x0b);
        }
    }

    SongDB* db = &g_game.songDB;
    SongMode* mode = &db->modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;
    if (songCount == 0) return;

    // CDs: 7 slots usando os mesmos offsets dos boxes
    // Ordem de render: slot 0(esq-fora), 6(dir-fora), 1(esq2), 5(dir2), 2(esq1), 4(dir1), 3(centro)
    int renderOrder[7] = { 0, 6, 1, 5, 2, 4, 3 };

    for (int ri = 0; ri < 7; ri++) {
        int si = renderOrder[ri];

        float slotFrame = (float)(g_carrosselFrame + g_slotFrameOffset[si]);
        float bx, bsc, balpha;
        EvalBoxFrame(slotFrame, &bx, &bsc, &balpha);

        if (balpha < 0.01f) continue;

        float screenX = 320.0f + bx;

        // slot 3 = centro = selectedSongIndex, offsets negativos = anteriores
        int slotOffset = si - 3;
        int idx = (g_game.selectedSongIndex + slotOffset + songCount) % songCount;

        int sid = mode->songIds[idx];
        int si2 = Song_FindByID(db, sid);
        if (si2 < 0) continue;

        int cdHalf = 0;
        int cdIdx = findCdForSong(sid, &cdHalf);
        int texId = (cdIdx >= 0) ? g_cdTexIds[cdIdx] : -1;

        float tw = 256.0f * bsc;
        float th = 128.0f * bsc;

        if (texId >= 0) {
            float u1 = 0.0f, v1 = (1.0f - (float)cdHalf) * 128.0f;
            float u2 = 256.0f, v2 = v1 + 128.0f;
            Texture_DrawUV(texId,
                screenX - tw * 0.5f,
                240.0f  - th * 0.5f,
                tw, th,
                u1, v1, u2, v2,
                1.0f, 1.0f, 1.0f, balpha);
        }
    }

    if (previewState) {
        char buf[64];
        snprintf(buf, sizeof(buf), "D%d.AUD", prevSongId);
        Font_DrawStringCentered(320, 410, buf, 0.5f, 1.0f, 0.5f, 1.0f);
    }

    if (selectedState) {
        Font_DrawStringCentered(320, 380, "SELECTED", 0.0f, 1.0f, 0.0f, 1.0f);
    }
}