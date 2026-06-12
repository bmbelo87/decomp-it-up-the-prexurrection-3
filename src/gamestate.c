#include "pumpy.h"

static int g_menuOption = 0;
int g_menuSelection = 0; // 0=none, 1=UL(Start), 2=UR(Options), 3=DL(Credits), 4=DR(Exit)

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
    if (Input_IsKeyHit(VK_ESCAPE)) {
        Game_ChangeState(STATE_LOGO_ANIM);
        return;
    }
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
    if (Input_IsKeyHit(VK_ESCAPE)) {
        Game_ChangeState(STATE_LOGO_ANIM);
        return;
    }
    switch (g_game.state) {
    case STATE_LOGO_ANIM:
        if (padHit(0, PAD_C)) {
            Game_ChangeState(STATE_LOGO_SKIP);
            return;
        }
        if (g_game.bgaFrame >= g_game.bgaMaxFrame && g_game.stateFrame > 60)
            Game_ChangeState(STATE_MENU_TRANSITION);
        break;
    case STATE_LOGO_SKIP:
        if (g_game.stateFrame > 15)
            Game_ChangeState(STATE_MENU_INPUT);
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
            g_menuSelection = 0;
            subEnter(STATE_MENU_IDLE);
        }
        break;
    case STATE_MENU_IDLE:
        if (g_game.stateFrame > 60) {
            g_game.input.padPrevState[0] = 0;
            g_game.input.padPrevState[1] = 0;
            g_game.input.padState[0] = 0;
            g_game.input.padState[1] = 0;
            subEnter(STATE_MENU_INPUT);
        }
        break;
    case STATE_MENU_FADE:
        g_game.fadeAlpha += dt * 1.0f;
        if (g_game.fadeAlpha >= 1.0f) {
            g_game.fadeAlpha = 0.0f;
            Game_ChangeState(STATE_SONG_SELECT_B);
        }
        break;
    case STATE_MENU_INPUT:
        if (g_menuSelection == 0) {
            if (padHit(0, PAD_UL)) { g_menuSelection = 1; return; }
            if (padHit(0, PAD_UR)) { g_menuSelection = 2; return; }
            if (padHit(0, PAD_DL)) { g_menuSelection = 3; return; }
            if (padHit(0, PAD_DR)) { g_menuSelection = 4; return; }
        } else {
            if (padHit(0, PAD_UL)) {
                if (g_menuSelection == 1) { g_game.fadeAlpha = 0.0f; Game_ChangeState(STATE_MENU_FADE); return; }
                g_menuSelection = 1; return;
            }
            if (padHit(0, PAD_UR)) {
                if (g_menuSelection == 2) { return; }
                g_menuSelection = 2; return;
            }
            if (padHit(0, PAD_DL)) {
                if (g_menuSelection == 3) { return; }
                g_menuSelection = 3; return;
            }
            if (padHit(0, PAD_DR)) {
                if (g_menuSelection == 4) { Game_ChangeState(STATE_EXIT); return; }
                g_menuSelection = 4; return;
            }
        }
        break;
    default:
        break;
    }
}

void Gamestate_UpdateSongSelect(float dt) {
    (void)dt;

    if (g_game.state == STATE_SONG_SELECT_B) {
        if (g_game.stateFrame > 60) {
            g_game.state = STATE_SONG_SELECT;
        }
        return;
    }

    if (Input_IsKeyHit(VK_ESCAPE)) {
        g_menuSelection = 0;
        Game_ChangeState(STATE_MENU_INPUT);
    }
}

void Gamestate_RenderMenu(void) {
    if (g_game.state != STATE_MENU_INPUT && g_game.state != STATE_MENU_IDLE &&
        g_game.state != STATE_MENU_TRANSITION && g_game.state != STATE_MENU_FADE) return;
}

void Gamestate_RenderSongSelect(void) {
    if (g_game.state != STATE_SONG_SELECT && g_game.state != STATE_SONG_SELECT_B) return;

    Font_DrawStringCentered(g_game.screenWidth / 2, g_game.screenHeight / 2,
        "Song Select", 1.0f, 1.0f, 1.0f, 1.0f);
}
