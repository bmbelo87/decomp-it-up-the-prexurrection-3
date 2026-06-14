#include "pumpy.h"
#include "bga.h"

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
    case STATE_RESET_FLOW:
        if (g_game.stateFrame > 30) {
            g_menuOption = 0;
            g_menuSelection = 0;
            g_game.confirmActive = false;
            g_game.confirmTimer = 0;
            subEnter(STATE_MENU_IDLE);
        }
        break;
    case STATE_MENU_IDLE:
        if (g_game.stateFrame > 60) {
            g_game.input.padPrevState[0] = 0;
            g_game.input.padPrevState[1] = 0;
            g_game.input.padState[0] = 0;
            g_game.input.padState[1] = 0;
            subEnter(STATE_MENU_INPUT_WAIT);
        }
        break;
    case STATE_MENU_INPUT_WAIT:
        if (g_game.stateFrame > 30)
            subEnter(STATE_MENU_ENTER);
        break;
    case STATE_MENU_ENTER:
        if (g_game.stateFrame > 15)
            subEnter(STATE_MENU_INPUT);
        break;
    case STATE_MENU_INPUT:
        if (g_menuSelection == 0) {
            if (padHit(0, PAD_UL)) { g_menuSelection = 1; return; }
            if (padHit(0, PAD_UR)) { g_menuSelection = 2; return; }
            if (padHit(0, PAD_DL)) { g_menuSelection = 3; return; }
            if (padHit(0, PAD_DR)) { g_menuSelection = 4; return; }
        } else {
            if (padHit(0, PAD_UL)) {
                if (g_menuSelection == 1) { g_game.fadeTarget = STATE_SONG_SELECT; Render_SetGlobalColor(0,0,0,0); Game_ChangeState(STATE_MENU_FADE_IN); return; }
                g_menuSelection = 1; return;
            }
            if (padHit(0, PAD_UR)) {
                if (g_menuSelection == 2) { g_game.fadeTarget = STATE_GAMEOPTION_ENTER; Render_SetGlobalColor(0,0,0,0); Game_ChangeState(STATE_MENU_FADE_IN); return; }
                g_menuSelection = 2; return;
            }
            if (padHit(0, PAD_DL)) {
                if (g_menuSelection == 3) { return; }
                g_menuSelection = 3; return;
            }
            if (padHit(0, PAD_DR)) {
                if (g_menuSelection == 4) { Render_SetGlobalColor(0,0,0,0.01f); Game_ChangeState(STATE_EXIT); return; }
                g_menuSelection = 4; return;
            }
        }
        break;
    case STATE_MENU_FADE_IN: {
        float a = g_game.globalColorA + dt * 1.0f;
        if (a >= 1.0f) {
            GameState target = g_game.fadeTarget;
            Render_SetGlobalColor(0, 0, 0, 0);
            g_game.fadeTarget = 0;
            Game_ChangeState(target);
        } else {
            Render_SetGlobalColor(0, 0, 0, a);
        }
        break;
    }
    default:
        break;
    }
}

// Mode-specific frame base offsets (82W.DAT keyframe ranges):
//   UL=420 (06.spr layer 10, 12.spr layer 16), UR=660 (07.spr, 11.spr),
//   DL=540 (07.spr layer 11), DR=780 (06.spr layer 12, 13.spr)
//   Background=1020 (04_1-04_4, 10-13.spr white), Selection=900 (16.spr)
//   Intro arrows=300 (05.spr), 540 (07.spr), 420 (06.spr), 660 (06.spr)
static const int ARROW_FRAME_OFFSET[] = {
    0,    // unused
    300,  // sel=1 UL: 05.spr layer 9
    540,  // sel=2 UR: 07.spr layer 11
    420,  // sel=3 DL: 06.spr layer 10
    660   // sel=4 DR: 06.spr layer 12
};

// Map our menu selection (0-based) to the original's g_dwMenuMode values:
//   1=UL, 3=UR, 7=Center, 9=DL, default=DR
// Returns frame offset + animate flag
// Frame for each panel animation:
//   300 → UL (05.spr layer 9)
//   420 → DL (06.spr layer 10)
//   540 → UR (07.spr layer 11)
//   660 → DR (06.spr layer 12)
static const int MENU_MODE_FRAME[] = {
    0,    // 0 = none
    300,  // 1 = UL
    540,  // 2 = UR
    420,  // 3 = DL
    660,  // 4 = DR
    780   // 5 = unused
};

void Gamestate_RenderMenu(int bgaIndex, int frame) {
    if (bgaIndex < 0 || bgaIndex >= g_game.bgaPicCount) return;
    if (g_game.state != STATE_RESET_FLOW && g_game.state != STATE_MENU_FADE_IN &&
        g_game.state != STATE_MENU_IDLE && g_game.state != STATE_MENU_INPUT_WAIT &&
        g_game.state != STATE_MENU_ENTER && g_game.state != STATE_MENU_INPUT &&
        g_game.state != STATE_EXIT) return;

    BGAPicture* pic = &g_game.bgaPics[bgaIndex];
    int sel = g_menuSelection;
    int aniFrame = (int)(g_game.frameCounter % 120);
    int modeFrame = (sel > 0) ? MENU_MODE_FRAME[sel] : 0;

    static bool dumped = false;
    if (!dumped) {
        dumped = true;
        for (int di = 0; di < pic->layerCount; di++) {
            Log_Print("MENU LAYER[%d] = %s\n", di, pic->layers[di].filename);
        }
    }

    for (int i = 0; i < pic->layerCount; i++) {
        BGALayer* layer = &pic->layers[i];
        int renderFrame = -1;

        if (isMenuOverlayLayer(layer)) {
            // Overlay layers (arrows, text, center)
            if (isMenuTextLayer(layer)) {
                if (sel == 0) {
                    renderFrame = 1020 + aniFrame;
                } else {
                    // Render twice: white base (1020) + black overlay (fixed at modeFrame)
                    BGA_SetEventLayer(bgaIndex, 1020 + aniFrame, i);
                    renderFrame = modeFrame; // fixed frame = always black (no pulse)
                }
            } else if (isMenuCenterLayer(layer)) {
                // Center pulse based on selection state
                if (sel == 0) {
                    // Idle: show 15.spr at 780+, hide 16.spr
                    if (strstr(layer->filename, "15."))
                        renderFrame = 780 + aniFrame;
                    else
                        renderFrame = -1; // don't render 16.spr when idle
                } else {
                    // Selected: show 16.spr at 900+, hide 15.spr
                    if (strstr(layer->filename, "16."))
                        renderFrame = 900 + aniFrame;
                    else
                        renderFrame = -1;
                }
            } else if (isMenuArrowLayer(layer)) {
                // Arrows: render at mode-specific frame
                if (sel == 0) {
                    renderFrame = -1; // don't show arrows when no selection
                } else {
                    renderFrame = modeFrame + aniFrame;
                }
            }
        } else {
            // Non-overlay: logo, intro, copyright, background
            // Background (04_1-04_4): at 1020+ always
            // Intro (int_c, int_d, 02, logo, copyr, 01): at their intro range
            if (strstr(layer->filename, "04_"))
                renderFrame = 1020 + aniFrame;
            else if (strstr(layer->filename, "int_c") || strstr(layer->filename, "int_d") ||
                     strstr(layer->filename, "logo") || strstr(layer->filename, "copyr") ||
                     strstr(layer->filename, "02.") || strstr(layer->filename, "01."))
                renderFrame = (int)(g_game.frameCounter % 415);
            else if (strstr(layer->filename, "forpc"))
                renderFrame = (int)(g_game.frameCounter % 415);
        }

        if (renderFrame >= 0)
            BGA_SetEventLayer(bgaIndex, renderFrame, i);
    }

}
