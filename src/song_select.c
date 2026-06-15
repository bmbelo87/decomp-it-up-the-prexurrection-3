#include "pumpy.h"

static int prevSongId = -1;
static int previewState = 0;
static int selectedState = 0; // 0=not selected, 1=selected (CN 1x)

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
        int next = g_game.selectedModeIndex + 1;
        if (next >= db->modeCount) next = 0;
        g_game.selectedModeIndex = next;
        g_game.selectedSongIndex = 0;
        prevSongId = -1;
    }

    if (Input_IsPadHit(0, PAD_UL)) {
        selectedState = 0;
        stopPreview();
        int prev = g_game.selectedModeIndex - 1;
        if (prev < 0) prev = db->modeCount - 1;
        g_game.selectedModeIndex = prev;
        g_game.selectedSongIndex = 0;
        prevSongId = -1;
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
    }

    if (Input_IsPadHit(0, PAD_DL)) {
        selectedState = 0;
        stopPreview();
        g_game.selectedSongIndex--;
        if (g_game.selectedSongIndex < 0)
            g_game.selectedSongIndex = songCount - 1;
        prevSongId = -1;
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

    SongDB* db = &g_game.songDB;
    SongMode* mode = &db->modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;

    Font_DrawStringCentered(320, 440, mode->name, 1.0f, 1.0f, 0.0f, 1.0f);

    if (previewState) {
        char buf[64];
        snprintf(buf, sizeof(buf), "D%d.AUD", prevSongId);
        Font_DrawStringCentered(320, 410, buf, 0.5f, 1.0f, 0.5f, 1.0f);
    }

    if (selectedState) {
        Font_DrawStringCentered(320, 380, "SELECTED", 0.0f, 1.0f, 0.0f, 1.0f);
    }

    if (songCount == 0) return;

    int x = 80;
    int lineH = 22;
    int visible = 15;
    int half = visible / 2;

    for (int i = 0; i < visible; i++) {
        int idx = (g_game.selectedSongIndex - half + i) % songCount;
        if (idx < 0) idx += songCount;

        int sid = mode->songIds[idx];
        int si = Song_FindByID(db, sid);
        if (si < 0) continue;

        SongEntry* e = &db->songs[si];
        bool selected = (idx == g_game.selectedSongIndex);

        float r = 0.5f, g = 0.5f, b = 0.5f;
        if (selected) { r = 1.0f; g = 1.0f; b = 1.0f; }

        int y = 340 - i * lineH;

        char buf[128];
        int diff = mode->difficulties[idx];
        snprintf(buf, sizeof(buf), "%s", e->title);
        Font_DrawString(x, y, buf, r, g, b, 1.0f);

        snprintf(buf, sizeof(buf), "Lv.%d", diff);
        Font_DrawString(580, y, buf, r, g, b, 1.0f);
    }
}
