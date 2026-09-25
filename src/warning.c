#include "pumpy.h"

static bool padHit(int player, PadButton btn) {
    return Input_IsPadHit(player, btn);
}

static void subEnter(GameState state) {
    g_game.stateFrame = 0;
    g_game.state = state;
}

void Gamestate_UpdateWarning(float dt) {
    (void)dt;
    if (Input_IsKeyHit(VK_ESCAPE)) {
        Game_ChangeState(STATE_LOGO_ENTER);
        return;
    }
    switch (g_game.state) {
    case STATE_WARNING_INIT:
        if (g_game.stateFrame > 60) subEnter(STATE_WARNING_ANIM);
        break;
    case STATE_WARNING_ANIM:
        if (g_game.bgaFrame >= g_game.bgaMaxFrame && g_game.stateFrame > 60)
            Game_ChangeState(STATE_LOGO_ENTER);
        break;
    default:
        break;
    }
}
