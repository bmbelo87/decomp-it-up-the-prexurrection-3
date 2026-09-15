#include "pumpy.h"

static int g_resultFrame;
/* Este static escondia o g_fontTexId global (font.c:103, declarado extern em
 * pumpy.h:376). Funcionava só por efeito colateral: Font_LoadTexture() além de
 * devolver o id também grava no global, então as duas cópias acabavam iguais.
 * Bastava alguém zerar o global — como Font_Shutdown() faz em todo clear de
 * BGA — para as duas divergirem e este arquivo passar a usar uma textura já
 * destruída. Removido; agora usa o global diretamente.
 * static int g_fontTexId = -1; */
static int g_gradeP1 = 5; // 0=S..5=F
static int g_gradeP2 = 5;
static int g_lastDigitSoundCount = 0;
static int g_lastSoundFrame = 0;
static bool g_gradeSoundPlayed = false;

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

/* Fórmula e escada extraídas do PUMPY.EXE.
 *
 * A razão é calculada no fim de Gameplay_ProcessJudgment (0x0041042c) e
 * gravada como float em [0x00da22d0] — a decompilação do Ghidra mostra um
 * cast para (int) por erro de tipagem, mas DanceGradeDisplay lê o endereço
 * com "FLD float ptr".
 *
 *   razao = (perfect + great*0.9 + good*0.6 - bad*0.5 - miss + maxCombo*0.03)
 *           / total
 *
 * O termo maxCombo*0.03 só entra fora do modo EVENT (g_nGameMode != 1).
 *
 * A escada de notas vem de DanceGradeDisplay (0x00415330..0x004153ac), onde
 * cada FCOMP compara a razão com 1.0, 0.9, 0.8, 0.7 e 0.6. A nota máxima
 * exige adicionalmente missCount == 0 (teste de [0x00da2314] em 0x00415343).
 */
static int calcGrade(int perfect, int great, int good, int bad, int miss, int maxCombo) {
    int total = perfect + great + good + bad + miss;
    float ratio;
    if (total == 0) return 5;

    ratio = (float)perfect
          + (float)great * 0.9f
          + (float)good  * 0.6f
          - (float)bad   * 0.5f
          - (float)miss;
    if (g_game.svcGameMode != 1)          /* fora do modo EVENT */
        ratio += (float)maxCombo * 0.03f;
    ratio /= (float)total;

    if (ratio >= 1.0f && miss == 0) return 0;  /* S */
    if (ratio >= 0.9f)              return 1;  /* A */
    if (ratio >= 0.8f)              return 2;  /* B */
    if (ratio >= 0.7f)              return 3;  /* C */
    if (ratio >= 0.6f)              return 4;  /* D */
    return 5;                                  /* F */
}

// Decide proximo estado baseado nas grades e contagem de stages
GameState Result_GetNextState(void) {
    int g1 = calcGrade(g_game.stats.perfectCount[0], g_game.stats.greatCount[0],
                       g_game.stats.goodCount[0], g_game.stats.badCount[0],
                       g_game.stats.missCount[0], (int)g_game.stats.maxCombo[0]);
    int g2 = calcGrade(g_game.stats.perfectCount[1], g_game.stats.greatCount[1],
                       g_game.stats.goodCount[1], g_game.stats.badCount[1],
                       g_game.stats.missCount[1], (int)g_game.stats.maxCombo[1]);

    /* Grade efetivo: P2 sozinho usa g2; caso contrário P1 decide progressão */
    int grade = (g_game.activePlayerMask == 0x2) ? g2 : g1;

    Log_Print("RESULT NEXT: grade=%d g1=%d g2=%d stageCount=%d bonusStage=%d isBonus=%d\n", grade, g1, g2, g_game.stageCount, g_game.bonusStage, g_game.isBonusSong);

    // F = game over
    if (grade == 5) {
        Log_Print("RESULT DECISION: grade=F -> GAMEOVER\n");
        return STATE_GAMEOVER_ENTER;
    }

    // Stage bonus: se S ou A mantem, caso contrario perde o bonus
    if (grade >= 2) // B, C, D
        g_game.bonusStage = false;

    // Bonus stage ja foi: game over direto
    if (g_game.isBonusSong)
        return STATE_GAMEOVER_ENTER;

    if (g_game.stageCount > 0)
        return STATE_STAGE_TRANSITION;

    if (g_game.bonusStage)
        return STATE_STAGE_TRANSITION;  // vai pro bonus

    return STATE_GAMEOVER_ENTER;
}

void Result_Enter(void) {
    g_resultFrame = 0;
    g_lastDigitSoundCount = 0;
    g_lastSoundFrame = -100;
    g_gradeSoundPlayed = false;
    g_game.bgaLoop = false;
    g_game.bgaFrame = 0;

    Font_LoadTexture();   /* já grava no g_fontTexId global */

    BGM_Stop();
    char ap[MAX_PATH];
    snprintf(ap, sizeof(ap), "%s\\AUDIO\\83.AUD", g_game.currentDirectory);
    if (BGM_LoadAUDDirect(ap)) BGM_Play(true);

    g_gradeP1 = calcGrade(g_game.stats.perfectCount[0], g_game.stats.greatCount[0],
                          g_game.stats.goodCount[0], g_game.stats.badCount[0],
                          g_game.stats.missCount[0], (int)g_game.stats.maxCombo[0]);
    g_gradeP2 = calcGrade(g_game.stats.perfectCount[1], g_game.stats.greatCount[1],
                          g_game.stats.goodCount[1], g_game.stats.badCount[1],
                          g_game.stats.missCount[1], (int)g_game.stats.maxCombo[1]);
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
        GameState ns = Result_GetNextState();
        Log_Print("RESULT: auto-transition at f=%d, next=%d=%s\n", g_resultFrame, ns, ns==STATE_GAMEOVER_ENTER?"GAMEOVER":ns==STATE_STAGE_TRANSITION?"STAGE_TRANS":"SONG_SEL");
        BGM_Stop();
        Resource_ClearBGA();
        Game_ChangeState(ns);
        return;
    }
    if (Input_IsKeyHit(VK_ESCAPE) || Input_IsKeyHit(VK_RETURN) ||
        Input_IsKeyHit(VK_SPACE) || Input_IsKeyHit(VK_F1)) {
        Log_Print("RESULT: manual exit at f=%d bgaCount=%d\n", g_resultFrame, g_game.bgaPicCount);
        BGM_Stop();
        Resource_ClearBGA();
        Log_Print("RESULT: manual exit, next=%d\n", Result_GetNextState());
        Game_ChangeState(Result_GetNextState());
        return;
    }
    if (Input_IsKeyHit(VK_ESCAPE) || Input_IsKeyHit(VK_RETURN) ||
        Input_IsKeyHit(VK_SPACE) || Input_IsKeyHit(VK_F1)) {
        Log_Print("RESULT: manual exit (2nd), next=%d\n", Result_GetNextState());
        BGM_Stop();
        glClear(GL_COLOR_BUFFER_BIT);
        Texture_Shutdown();
        Resource_ClearBGA();
        Render_SetGlobalColor(0, 0, 0, 1.0f);
        Game_ChangeState(Result_GetNextState());
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

    // Toca 8-1.wav a cada 5 frames (independente do digito, total 25)
    if (f >= 60 && (f - g_lastSoundFrame) >= 5 && g_lastDigitSoundCount < 25) {
        Audio_Play(g_waveSoundIds[SND_8_1], false);
        g_lastDigitSoundCount++;
        g_lastSoundFrame = f;
    }

    /* Grade sound (5-1 + rank) quando a nota aparece no frame 0xd2.
     * Para P2 solo usa g_gradeP2; para 2P toca apenas o som do player efetivo
     * (P1 decide, pois ambas as grades já estão visíveis na tela). */
    if (f >= 0xd2 && !g_gradeSoundPlayed) {
        g_gradeSoundPlayed = true;
        int effectiveGrade = (g_game.activePlayerMask == 0x2) ? g_gradeP2 : g_gradeP1;
        Audio_Play(g_waveSoundIds[SND_5_1], false);
        int rankSnd;
        if (effectiveGrade <= 1) rankSnd = SND_RANK_A;        // S ou A
        else if (effectiveGrade == 2) rankSnd = SND_RANK_B;
        else if (effectiveGrade == 3) rankSnd = SND_RANK_C;
        else if (effectiveGrade == 4) rankSnd = SND_RANK_D;
        else rankSnd = SND_RANK_F;
        Audio_Play(g_waveSoundIds[rankSnd], false);
    }

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
        /* Itera apenas os players ativos.
         * P1 (p=0): drawNumP1 no lado esquerdo (x=8).
         * P2 (p=1): drawNumP2 no lado direito (rx=603). */
        int pStart = (g_game.activePlayerMask == 0x2) ? 1 : 0;
        int pEnd   = (g_game.activePlayerMask & 0x2)  ? 2 : 1;
        for (int p = pStart; p < pEnd; p++) {
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
                    drawNumP1(8,   g_statY[i], stats[i], g_statDigits[i], elap, offP1);
                else
                    drawNumP2(603, g_statY[i], stats[i], g_statDigits[i], elap, offP2);
            }
        }
    }

    // Grade letter via BGA event layer: frames 211-416
    if (f > 0xd2 && f < 0x1a0) {
        if (g_game.activePlayerMask & 0x1)
            BGA_SetEventLayer(0, f + 0x348, 44 + g_gradeP1);
        if (g_game.activePlayerMask & 0x2)
            BGA_SetEventLayer(0, f + 0x438, 44 + g_gradeP2);
    }

    /* CLEAR/FAIL — P2 solo usa g_gradeP2; em 2P mostra resultado de cada player
     * no mesmo layer (0x1d=CLEAR, 0x1b=FAIL). Para 2P, o CLEAR/FAIL visual
     * do binário original usava apenas a tela de P1; mantemos a mesma layer. */
    if (f > 0x1a3) {
        int activeGrade;
        if (g_game.activePlayerMask == 0x2)
            activeGrade = g_gradeP2;          // P2 solo
        else if (g_game.activePlayerMask == 0x3)
            activeGrade = (g_gradeP1 < g_gradeP2) ? g_gradeP1 : g_gradeP2; // 2P: melhor nota decide
        else
            activeGrade = g_gradeP1;          // P1 solo (padrão)

        int clearFail = (activeGrade < 5) ? 0x1d : 0x1b;
        int cfOff     = (clearFail == 0x1d) ? 0xc1 : 0x175;
        BGA_SetEventLayer(0, f + cfOff, clearFail);
    }

    Font_DrawStringCentered(g_game.screenWidth/2, 16,
        "Press ENTER or ESC to continue", 0.7f, 0.7f, 0.7f, 1.0f);
}
