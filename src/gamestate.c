#include "pumpy.h"

static int g_menuOption = 0;
static const char* MENU_OPTIONS[] = { "START GAME", "OPTIONS", "CREDITS", "EXIT" };
#define MENU_OPTION_COUNT 4

void Gamestate_Enter(GameState state) {
    g_game.stateFrame = 0;
    g_game.state = state;
    Log_Print("Gamestate: entered %s\n", State_ToString(state));
}

void Gamestate_Exit(void) {
    Log_Print("Gamestate: exited %s\n", State_ToString(g_game.state));
}

static void subEnter(GameState state) {
    g_game.stateFrame = 0;
    g_game.state = state;
}

static bool padHit(int player, PadButton btn) {
    return Input_IsPadHit(player, btn);
}

void Gamestate_UpdateWarning(float dt) {
    (void)dt;
    switch (g_game.state) {
    case STATE_INIT_WARNING:
        if (g_game.stateFrame > 60) subEnter(STATE_WARNING_ANIM);
        break;
    case STATE_WARNING_ANIM:
        if (g_game.bgaFrame >= g_game.bgaMaxFrame && g_game.stateFrame > 60)
            Game_ChangeState(STATE_LOGO_ANIM);
        break;
    default:
        break;
    }
}

void Gamestate_UpdateLogo(float dt) {
    (void)dt;
    switch (g_game.state) {
    case STATE_LOGO_ANIM:
        if (g_game.stateFrame > 60) subEnter(STATE_LOGO_WAIT);
        break;
    case STATE_LOGO_WAIT:
        if (g_game.stateFrame > 180) Game_ChangeState(STATE_MENU_TRANSITION);
        break;
    default:
        break;
    }
}

void Gamestate_UpdateMenu(float dt) {
    (void)dt;
    switch (g_game.state) {
    case STATE_MENU_TRANSITION:
        if (g_game.stateFrame > 30) {
            g_menuOption = 0;
            subEnter(STATE_MENU_IDLE);
        }
        break;
    case STATE_MENU_IDLE:
        if (g_game.stateFrame > 60) subEnter(STATE_MENU_INPUT);
        break;
    case STATE_MENU_INPUT:
        if (padHit(0, PAD_UL)) {
            Game_ChangeState(STATE_SONG_SELECT);
        } else if (padHit(0, PAD_UR)) {
            Log_Print("Menu: OPTIONS (not implemented)\n");
        } else if (padHit(0, PAD_DL)) {
            Log_Print("Menu: CREDITS (not implemented)\n");
        } else if (padHit(0, PAD_DR)) {
            Game_ChangeState(STATE_EXIT);
        } else if (Input_IsKeyHit(VK_ESCAPE)) {
            Game_ChangeState(STATE_EXIT);
        }
        break;
    default:
        break;
    }
}

void Gamestate_UpdateSongSelect(float dt) {
    (void)dt;

    if (g_game.selectedModeIndex < 0 || g_game.selectedModeIndex >= g_game.songDB.modeCount) {
        g_game.selectedModeIndex = 0;
    }

    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;
    if (songCount == 0) return;

    if (g_game.songSelectHighlighted < 0 || g_game.songSelectHighlighted >= songCount) {
        g_game.songSelectHighlighted = 0;
    }

    if (g_game.state == STATE_SONG_SELECT_B) {
        if (g_game.stateFrame > 20) {
            g_game.state = STATE_SONG_SELECT;
        }
        return;
    }

    int oldHighlighted = g_game.songSelectHighlighted;
    int oldMode = g_game.selectedModeIndex;

    // DL = previous song
    if (padHit(0, PAD_DL)) {
        g_game.songSelectHighlighted--;
        if (g_game.songSelectHighlighted < 0) {
            g_game.songSelectHighlighted = songCount - 1;
        }
    }
    // DR = next song
    if (padHit(0, PAD_DR)) {
        g_game.songSelectHighlighted++;
        if (g_game.songSelectHighlighted >= songCount) {
            g_game.songSelectHighlighted = 0;
        }
    }

    // UL = previous mode (wraps around)
    if (padHit(0, PAD_UL)) {
        g_game.selectedModeIndex--;
        if (g_game.selectedModeIndex < 0) {
            g_game.selectedModeIndex = g_game.songDB.modeCount - 1;
        }
        g_game.songSelectHighlighted = 0;
    }
    // UR = next mode (wraps around)
    if (padHit(0, PAD_UR)) {
        g_game.selectedModeIndex++;
        if (g_game.selectedModeIndex >= g_game.songDB.modeCount) {
            g_game.selectedModeIndex = 0;
        }
        g_game.songSelectHighlighted = 0;
    }

    // C = select song
    if (padHit(0, PAD_C) || Input_IsKeyHit(VK_SPACE) || Input_IsKeyHit(VK_RETURN)) {
        if (g_game.songSelectHighlighted >= 0 && g_game.songSelectHighlighted < songCount) {
            mode = &g_game.songDB.modes[g_game.selectedModeIndex];
            int songId = mode->songIds[g_game.songSelectHighlighted];
            int songIdx = Song_FindByID(&g_game.songDB, songId);
            if (songIdx >= 0 && g_game.songDB.songs[songIdx].hasChart) {
                g_game.selectedSongIndex = songIdx;
                Game_ChangeState(STATE_GAMEPLAY_PREP);
            }
        }
    }

    if (Input_IsKeyHit(VK_ESCAPE)) {
        Game_ChangeState(STATE_MENU_INPUT);
    }
}

void Gamestate_RenderMenu(void) {
    if (g_game.state != STATE_MENU_INPUT && g_game.state != STATE_MENU_IDLE &&
        g_game.state != STATE_MENU_TRANSITION) return;

    glColor3f(1.0f, 1.0f, 1.0f);
    Font_DrawStringCentered(g_game.screenWidth / 2, 120,
        "PUMP IT UP - PREX3", 1.0f, 1.0f, 1.0f, 1.0f);

    const char* labels[] = { "UL - START GAME", "UR - OPTIONS", "DL - CREDITS", "DR - EXIT" };
    int startY = 220;
    for (int i = 0; i < MENU_OPTION_COUNT; i++) {
        int y = startY + i * 30;
        Font_DrawStringCentered(g_game.screenWidth / 2, y,
            labels[i], 0.7f, 0.7f, 0.7f, 1.0f);
    }
}

void Gamestate_RenderSongSelect(void) {
    if (g_game.state != STATE_SONG_SELECT && g_game.state != STATE_SONG_SELECT_B) return;

    if (g_game.selectedModeIndex < 0 || g_game.selectedModeIndex >= g_game.songDB.modeCount) {
        return;
    }

    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;

    if (g_game.songSelectHighlighted < 0 || g_game.songSelectHighlighted >= songCount) {
        g_game.songSelectHighlighted = 0;
    }

    glColor3f(1.0f, 1.0f, 1.0f);
    Font_DrawStringCentered(g_game.screenWidth / 2, 20,
        "SONG SELECT", 1.0f, 1.0f, 1.0f, 1.0f);

    char modeBuf[64];
    snprintf(modeBuf, sizeof(modeBuf), "MODE: %s", mode->name);
    Font_DrawStringCentered(g_game.screenWidth / 2, 42,
        modeBuf, 0.3f, 0.8f, 1.0f, 1.0f);

    char instr[128];
    snprintf(instr, sizeof(instr),
        "UL/UR: mode   DL/DR: select   C: play   ESC: back");
    Font_DrawString(g_game.screenWidth / 2 - 200, g_game.screenHeight - 20,
        instr, 0.5f, 0.5f, 0.5f, 1.0f);

    // Centered 5-song list: current selection is middle (index 2 of 5)
    // Show: cur-2, cur-1, cur, cur+1, cur+2
    int startX = 120;
    int centerY = 200;
    int lineH = 25;
    int visible = 5;
    int centerIdx = visible / 2;

    for (int i = 0; i < visible; i++) {
        int offset = i - centerIdx;
        int idx = (g_game.songSelectHighlighted + offset) % songCount;
        if (idx < 0) idx += songCount;

        float alpha = 1.0f;
        float scale = 1.0f;
        if (offset == 0) {
            scale = 1.2f;
            alpha = 1.0f;
        } else if (offset == 1 || offset == -1) {
            scale = 1.0f;
            alpha = 0.7f;
        } else {
            scale = 0.8f;
            alpha = 0.4f;
        }

        int songId = mode->songIds[idx];
        int diff = mode->difficulties[idx];
        int songIdx = Song_FindByID(&g_game.songDB, songId);
        bool canPlay = false;
        const char* title = "???";

        if (songIdx >= 0 && songIdx < g_game.songDB.songCount) {
            title = g_game.songDB.songs[songIdx].title;
            canPlay = g_game.songDB.songs[songIdx].hasChart;
        }

        int y = centerY + offset * lineH;

        if (offset == 0) {
            Render_Rect(startX - 4, y - 10,
                g_game.screenWidth - startX * 2 + 8, lineH + 4,
                60, 60, 120, 180);
        }

        char itemBuf[128];
        if (canPlay)
            snprintf(itemBuf, sizeof(itemBuf), "%s  [%d]", title, diff);
        else
            snprintf(itemBuf, sizeof(itemBuf), "%s  [NO CHART]", title);

        Font_DrawString(startX, y,
            itemBuf,
            (offset == 0) ? 1.0f : (canPlay ? 0.7f : 0.3f),
            (offset == 0) ? 1.0f : (canPlay ? 0.7f : 0.3f),
            canPlay ? ((offset == 0) ? 1.0f : 0.7f) : 0.3f,
            alpha);
    }

    char posBuf[32];
    snprintf(posBuf, sizeof(posBuf), "%d/%d",
        g_game.songSelectHighlighted + 1, songCount);
    Font_DrawStringCentered(g_game.screenWidth / 2, centerY + 3 * lineH + 10,
        posBuf, 0.5f, 0.5f, 0.5f, 1.0f);
}
