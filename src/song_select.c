#include "pumpy.h"

void Gamestate_UpdateSongSelect(float dt) {

    if (g_game.state == STATE_SONG_SELECT_B) {
        float a = g_game.globalColorA - dt * 1.0f;
        if (a <= 0.0f) {
            Render_SetGlobalColor(0, 0, 0, 0);
            if (g_game.stateFrame > 60) g_game.state = STATE_SONG_SELECT;
        } else {
            Render_SetGlobalColor(0, 0, 0, a);
        }
        return;
    }

    if (Input_IsKeyHit(VK_ESCAPE)) {
        g_menuSelection = 0;
        Game_ChangeState(STATE_MENU_INPUT);
    }
}

void Gamestate_RenderSongSelect(void) {
    if (g_game.state != STATE_SONG_SELECT && g_game.state != STATE_SONG_SELECT_B) return;

    Font_DrawStringCentered(g_game.screenWidth / 2, g_game.screenHeight / 2,
        "Song Select", 1.0f, 1.0f, 1.0f, 1.0f);
}
