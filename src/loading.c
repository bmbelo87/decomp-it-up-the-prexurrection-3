#include "pumpy.h"

#define LOADING_DURATION_MS 3550
#define LOADING_FADE_MS 48

static int g_pnzTexId = -1;
static int g_loadingSongId = -1;
static int g_loadingTimer = 0;

void Loading_Enter(int songId) {
    g_loadingSongId = songId;
    g_loadingTimer = LOADING_DURATION_MS;
    g_pnzTexId = -1;

    // Decrementa stage count (exceto no bonus que nao altera)
    if (g_game.stageCount > 0)
        g_game.stageCount--;
    else
        g_game.isBonusSong = true;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\TITLE\\T%d.pnz", g_game.currentDirectory, songId);
    Log_Print("Loading: loading PNZ '%s'\n", path);

    g_pnzTexId = Resource_LoadPNZ(path);
    if (g_pnzTexId < 0) {
        Log_Print("Loading: PNZ not found for song %d\n", songId);
    }

    g_game.state = STATE_LOADING_PNZ;
    g_game.stateFrame = 0;
    Render_SetGlobalColor(0, 0, 0, 0);
}

void Loading_Update(float dt) {
    if (g_game.state == STATE_LOADING_PNZ) {
        int ms = (int)(dt * 1000.0f);
        if (ms < 1) ms = 1;
        g_loadingTimer -= ms;

        if (g_loadingTimer <= 0) {
            g_loadingTimer = LOADING_FADE_MS;
            g_game.state = STATE_LOADING_PNZ_B;
            g_game.stateFrame = 0;
        }
        return;
    }

    if (g_game.state == STATE_LOADING_PNZ_B) {
        int ms = (int)(dt * 1000.0f);
        if (ms < 1) ms = 1;
        g_loadingTimer -= ms;

        if (g_loadingTimer <= 0) {
            Resource_ClearBGA();

            char bgaPath[MAX_PATH];
            snprintf(bgaPath, sizeof(bgaPath), "%s\\BGA\\%d.DAT", g_game.currentDirectory, g_loadingSongId);
            Log_Print("Loading: loading BGA '%s'\n", bgaPath);
            Resource_LoadBGADirect(bgaPath);
            g_game.bgaLoop = false;
            BGA_Reset();

            char audioPath[MAX_PATH];
            snprintf(audioPath, sizeof(audioPath), "%s\\AUDIO\\%d.AUD", g_game.currentDirectory, g_loadingSongId);
            Log_Print("Loading: loading AUD '%s'\n", audioPath);
            if (BGM_LoadAUDDirect(audioPath))
                BGM_Play(false);

            g_game.songSelectHighlighted = g_game.selectedSongIndex;

            g_game.state = STATE_GAMEPLAY;
            Gameplay_Start(g_loadingSongId);
            g_game.stateFrame = 0;
            g_game.bgaFrame = 0;
            Render_SetGlobalColor(0, 0, 0, 0);

            if (g_pnzTexId >= 0) {
                Texture_Unload(g_pnzTexId);
                g_pnzTexId = -1;
            }
        }
        return;
    }
}

void Loading_Render(void) {
    if (g_game.state != STATE_LOADING_PNZ && g_game.state != STATE_LOADING_PNZ_B)
        return;

    if (g_pnzTexId >= 0) {
        int tw = Texture_GetWidth(g_pnzTexId);
        int th = Texture_GetHeight(g_pnzTexId);

        float scaleX = 640.0f / (float)tw;
        float scaleY = 480.0f / (float)th;
        float scale = scaleX < scaleY ? scaleX : scaleY;
        float dw = tw * scale;
        float dh = th * scale;
        float dx = (640.0f - dw) / 2.0f;
        float dy = (480.0f - dh) / 2.0f;

        Texture_Draw(g_pnzTexId, dx, dy, scale, scale, 1.0f);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Loading %d...", g_loadingSongId);
        Font_DrawStringCentered(320, 240, buf, 0.5f, 0.5f, 0.5f, 1.0f);
    }
}

bool Loading_IsActive(void) {
    return g_game.state == STATE_LOADING_PNZ || g_game.state == STATE_LOADING_PNZ_B;
}
