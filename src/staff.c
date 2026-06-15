#include "pumpy.h"

static float staffAccumulator = 0.0f;
static uint32_t staffFrame = 0;
static uint32_t staffMaxFrame = 0;

void Staff_Enter(void) {
    Resource_LoadBGADirect("BGA\\STAFF.DAT");
    glColor3f(1.0f, 1.0f, 1.0f);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\AUDIO\\84.AUD", g_game.currentDirectory);
    BGM_Stop();
    BGM_LoadAUDDirect(path);
    if (g_game.bgm.durationMs > 0)
        staffMaxFrame = (uint32_t)(g_game.bgm.durationMs / 1000.0f * 60.0f);
    else
        staffMaxFrame = 3840;
    Log_Print("Staff: %lu ms = %lu frames\n", g_game.bgm.durationMs, staffMaxFrame);
    BGM_Play(false);
    g_game.state = STATE_STAFF;
    g_game.stateFrame = 0;
    g_game.bgaFrame = 0;
    g_game.bgaLoop = false;
    staffAccumulator = 0.0f;
    staffFrame = 0;
}

void Staff_Update(float dt) {
    if (staffFrame < staffMaxFrame) {
        staffAccumulator += dt;
        while (staffAccumulator >= (1.0f / 60.0f)) {
            staffAccumulator -= (1.0f / 60.0f);
            staffFrame++;
            g_game.bgaFrame = staffFrame;
        }
    }
    if (staffFrame >= staffMaxFrame ||
        Input_IsKeyHit(VK_ESCAPE) ||
        Input_IsPadHit(0, PAD_C) || Input_IsPadHit(1, PAD_C)) {
        BGM_Stop();
        Menu_ResetState();
        Game_ChangeState(STATE_MENU_ENTER);
    }
}
