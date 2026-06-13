#include "pumpy.h"

static int g_menuOption = 0;
int g_menuSelection = 0; // 0=none, 1=UL(Start), 2=UR(Options), 3=DL(Credits), 4=DR(Exit)

static bool padHit(int player, PadButton btn) {
    return Input_IsPadHit(player, btn);
}

static void subEnter(GameState state) {
    g_game.stateFrame = 0;
    g_game.state = state;
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

void Gamestate_RenderMenu(void) {
    if (g_game.state != STATE_MENU_INPUT && g_game.state != STATE_MENU_IDLE &&
        g_game.state != STATE_MENU_TRANSITION && g_game.state != STATE_MENU_FADE) return;
}
