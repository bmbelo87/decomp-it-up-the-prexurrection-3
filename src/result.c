#include "pumpy.h"

static int g_resultFrame;
static int g_fontTexId = -1;
static int g_gradeP1 = 5; // 0=S..5=F
static int g_gradeP2 = 5;

// Y original Ghidra -> Y-DOWN (topo do digito)
// 480 - yUp - 39 = valor
static const int g_statY[7] = {
    131, 177, 223, 269, 315, 361, 407
};
static const int g_statDelay[7] = { 60, 70, 80, 90, 100, 110, 120 };
static const int g_statDigits[7] = { 3, 3, 3, 3, 3, 3, 7 };
#define SPIN 10  // frames por digito girando (0->1->...->9->final)

static int getDig(int v, int rp) {
    int p = 1;
    for (int j = 0; j < rp; j++) p *= 10;
    return (v / p) % 10;
}

static void drawDig(int x, int y, int d) {
    if (g_fontTexId < 0 || d < 0 || d > 9) return;
    int col = d % 8, row = d / 8;
    Texture_Bind(g_fontTexId);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1,1,1,1);
    float yUp = 480.0f - (float)y - 39.0f;
    float u0 = (float)col * 0.125f;
    float u1 = u0 + 0.125f;
    float vb = (float)row * 0.12109375f + 0.28515625f; // base
    float vt = vb + 0.12109375f;                        // topo
    glBegin(GL_QUADS);
    glTexCoord2f(u0, 1-vt); glVertex2f((float)x, yUp);
    glTexCoord2f(u1, 1-vt); glVertex2f((float)x+36, yUp);
    glTexCoord2f(u1, 1-vb); glVertex2f((float)x+36, yUp+39);
    glTexCoord2f(u0, 1-vb); glVertex2f((float)x, yUp+39);
    glEnd();
}

// P1: left-aligned, MSB primeiro
static void drawNumP1(int x, int y, int v, int nd, int elap, int offX) {
    x += offX;
    for (int i = 0; i < nd; i++) {
        int le = elap - i * SPIN;
        if (le < 0) break;
        int d = (le < SPIN) ? (le % 10) : getDig(v, nd-1-i);
        drawDig(x + i*22, y, d);
    }
}

// P2: right-aligned, LSB primeiro
static void drawNumP2(int rx, int y, int v, int nd, int elap, int offX) {
    rx += offX;
    for (int i = 0; i < nd; i++) {
        int le = elap - i * SPIN;
        if (le < 0) break;
        int d = (le < SPIN) ? (le % 10) : getDig(v, i);
        drawDig(rx - i*22, y, d);
    }
}

static int calcGrade(int perfect, int great, int good, int bad, int miss) {
    int total = perfect + great + good + bad + miss;
    if (total == 0) return 5;
    float w = (float)(perfect*10 + great*7 + good*5 + bad*2) / (float)(total*10);
    if (w >= 0.95f) return 0;
    if (w >= 0.85f) return 1;
    if (w >= 0.75f) return 2;
    if (w >= 0.60f) return 3;
    if (w >= 0.40f) return 4;
    return 5;
}

void Result_Enter(void) {
    g_resultFrame = 0;
    g_game.bgaLoop = false;
    g_game.bgaFrame = 0;

    if (g_fontTexId < 0) g_fontTexId = Font_LoadTexture();

    BGM_Stop();
    char ap[MAX_PATH];
    snprintf(ap, sizeof(ap), "%s\\AUDIO\\83.AUD", g_game.currentDirectory);
    if (BGM_LoadAUDDirect(ap)) BGM_Play(true);

    g_gradeP1 = calcGrade(g_game.stats.perfectCount[0], g_game.stats.greatCount[0],
                          g_game.stats.goodCount[0], g_game.stats.badCount[0],
                          g_game.stats.missCount[0]);
    g_gradeP2 = calcGrade(g_game.stats.perfectCount[1], g_game.stats.greatCount[1],
                          g_game.stats.goodCount[1], g_game.stats.badCount[1],
                          g_game.stats.missCount[1]);
    Log_Print("Result: grades P1=%d P2=%d\n", g_gradeP1, g_gradeP2);
}

void Result_Update(float dt) {
    (void)dt;
    if (g_game.state == STATE_DANCE_GRADE_ENTER) {
        g_resultFrame++;
        if (g_resultFrame >= 15) {
            g_resultFrame = 0;
            Game_ChangeState(STATE_DANCE_GRADE_DISPLAY);
        }
        return;
    }
    if (g_game.state != STATE_DANCE_GRADE_DISPLAY) return;

    g_resultFrame++;
    if (g_resultFrame >= 0x24E) {
        Log_Print("RESULT: auto-transition at f=%d bgaCount=%d\n", g_resultFrame, g_game.bgaPicCount);
        BGM_Stop();
        Resource_ClearBGA();
        Log_Print("RESULT: after ClearBGA bgaCount=%d\n", g_game.bgaPicCount);
        Game_ChangeState(STATE_MENU_ENTER);
        return;
    }
    if (Input_IsKeyHit(VK_ESCAPE) || Input_IsKeyHit(VK_RETURN) ||
        Input_IsKeyHit(VK_SPACE) || Input_IsKeyHit(VK_F1)) {
        Log_Print("RESULT: manual exit at f=%d bgaCount=%d\n", g_resultFrame, g_game.bgaPicCount);
        BGM_Stop();
        Resource_ClearBGA();
        Log_Print("RESULT: after ClearBGA bgaCount=%d\n", g_game.bgaPicCount);
        Game_ChangeState(STATE_MENU_ENTER);
        return;
    }
    if (Input_IsKeyHit(VK_ESCAPE) || Input_IsKeyHit(VK_RETURN) ||
        Input_IsKeyHit(VK_SPACE) || Input_IsKeyHit(VK_F1)) {
        Log_Print("RESULT: manual exit at f=%d bgaCount=%d\n", g_resultFrame, g_game.bgaPicCount);
        BGM_Stop();
        glClear(GL_COLOR_BUFFER_BIT);
        Texture_Shutdown();
        Resource_ClearBGA();
        Log_Print("RESULT: after ClearBGA bgaCount=%d\n", g_game.bgaPicCount);
        Render_SetGlobalColor(0, 0, 0, 1.0f);
        Game_ChangeState(STATE_MENU_ENTER);
        return;
    }
    // Keep BGA playing/looping so background tiles cycle
    g_game.bgaFrame++;
    if (g_game.bgaFrame >= g_game.bgaMaxFrame)
        g_game.bgaFrame = 0;
}

void Result_Render(void) {
    if (g_game.state == STATE_DANCE_GRADE_ENTER) {
        if (g_game.bgaPicCount > 0) BGA_Render(0, g_game.bgaFrame);
        return;
    }
    if (g_game.state != STATE_DANCE_GRADE_DISPLAY) return;

    int f = g_resultFrame;

    // Last frames: only show "Press ENTER" text, no BGA/CLEAR/FAIL
    if (f >= 0x24E) {
        Font_DrawStringCentered(g_game.screenWidth/2, 16,
            "Press ENTER or ESC to continue", 0.7f, 0.7f, 0.7f, 1.0f);
        return;
    }

    if (g_game.bgaPicCount > 0) BGA_Render(0, g_game.bgaFrame);

    int offP1 = 0, offP2 = 0;
    bool drawNums = true;
    if (f > 0x194 && f < 0x1a0) {
        int d = f - 0x195;
        offP1 = (int)((float)d * -22.72f);
        offP2 = (int)((float)d * 23.11f);
    } else if (f >= 0x1a0) {
        drawNums = false;
    }

    if (drawNums) {
        for (int p = 0; p < 2; p++) {
            int stats[7] = {
                g_game.stats.perfectCount[p],
                g_game.stats.greatCount[p],
                g_game.stats.goodCount[p],
                g_game.stats.badCount[p],
                g_game.stats.missCount[p],
                (int)g_game.stats.maxCombo[p],
                (int)g_game.stats.score[p]
            };
            for (int i = 0; i < 7; i++) {
                if (f < g_statDelay[i]) continue;
                int elap = f - g_statDelay[i];
                if (p == 0)
                    drawNumP1(8, g_statY[i], stats[i], g_statDigits[i], elap, offP1);
                else
                    drawNumP2(603, g_statY[i], stats[i], g_statDigits[i], elap, offP2);
            }
        }
    }

    // Grade letter via BGA event layer: frames 211-416
    if (f > 0xd2 && f < 0x1a0) {
        BGA_SetEventLayer(0, f + 0x348, 44 + g_gradeP1);
        BGA_SetEventLayer(0, f + 0x438, 44 + g_gradeP2);
    }

    // CLEAR/FAIL via BGA event layer: after frame 419
    if (f > 0x1a3) {
        int clearFail = (g_gradeP1 < 5 || g_gradeP2 < 5) ? 0x1d : 0x1b;
        int cfOff = clearFail == 0x1d ? 0xc1 : 0x175;
        BGA_SetEventLayer(0, f + cfOff, clearFail);
    }

    Font_DrawStringCentered(g_game.screenWidth/2, 16,
        "Press ENTER or ESC to continue", 0.7f, 0.7f, 0.7f, 1.0f);
}
