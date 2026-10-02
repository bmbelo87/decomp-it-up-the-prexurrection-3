#include "pumpy.h"
#include "bga.h"

static int g_menuOption = 0;
int g_menuSelection = 0; // 0=none, 1=UL(Start), 2=UR(Options), 3=DL(Credits), 4=DR(Exit)
static int g_p2MenuSel = 0; // seleção de P2: 0=nenhuma, 1=UL(Start)

void Menu_ResetState(void) {
    g_menuOption = 0;
    g_menuSelection = 0;
    g_p2MenuSel = 0;
    g_game.activePlayerMask = 0x1; /* reset: P1 ativo por padrão */
    g_game.stageCount = 3;
    g_game.bonusStage = true;
    g_game.isBonusSong = false;
    NameEntry_ResetTotals();   /* novo crédito: zera o score acumulado */
}

static bool padHit(int player, PadButton btn) {
    return Input_IsPadHit(player, btn);
}

/* ─────────────────────────── Tela de título ARCADE (extra do port) ──────────
 * Layout e lógica do PREX3-MK5 (PIU32.EXE, estado 0xb = 0x408f40), com os
 * sprites e texturas do 82W.DAT (o 82.DAT extraído só tem PNGs vazios):
 *   - fundo: todos os layers no frame f % 415 (0x409021), sem as camadas do menu
 *   - por jogador ainda fora: sem crédito -> "insert coin" piscando;
 *     com crédito -> "press center step" + painel animado (MAH01)
 *   - centro com crédito entra (0x409292) e consome o crédito (0x4054d0);
 *     60 frames depois (0xed5088 > 0x3c) ou com os dois dentro -> seleção
 *   - contador de créditos do MK5 (0x40507c), com os glifos do texts.png
 * Posições do P1: as naturais dos .spr; P2: espelho +320 (insert2* já vêm
 * na posição do P2). */
static int  s_arcTile[16];      /* índices em g_game.sprTiles, -1 = ausente */
static int  s_arcMahCount;
static int  s_arcTexts = -1;    /* texts.tga */
static int  s_arcJoin;          /* 0x43ff38 */
static int  s_arcJoinTimer;     /* 0xed5088 */
static int  s_arcFade;          /* 0xed5084 */
enum { ARC_INS1A, ARC_INS1B, ARC_INS1C, ARC_INS2A, ARC_INS2B, ARC_INS2C,
       ARC_PRESS, ARC_PRESSB, ARC_MAH, ARC_COUNT };

bool Menu_ArcadeHasJoin(void) { return g_arcadeStyle && s_arcJoin != 0; }

static int arcLoad(const char* path, const char* spr, int* count)
{
    int before = g_game.sprTileCount;
    int n = Resource_LoadSPR(path, spr);
    if (count) *count = n;
    return n > 0 ? before : -1;
}

void Menu_ArcadeLoad(void)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/BGA/82W.DAT", g_game.currentDirectory);
    s_arcTile[ARC_INS1A]  = arcLoad(path, "insert1a.spr", NULL);
    s_arcTile[ARC_INS1B]  = arcLoad(path, "insert1b.spr", NULL);
    s_arcTile[ARC_INS1C]  = arcLoad(path, "insert1c.spr", NULL);
    s_arcTile[ARC_INS2A]  = arcLoad(path, "insert2a.spr", NULL);
    s_arcTile[ARC_INS2B]  = arcLoad(path, "insert2b.spr", NULL);
    s_arcTile[ARC_INS2C]  = arcLoad(path, "insert2c.spr", NULL);
    s_arcTile[ARC_PRESS]  = arcLoad(path, "press.spr", NULL);
    s_arcTile[ARC_PRESSB] = arcLoad(path, "press-b.spr", NULL);
    s_arcTile[ARC_MAH]    = arcLoad(path, "MAH01.spr", &s_arcMahCount);
    s_arcTexts = Resource_LoadTextureFromDAT(path, "texts.tga");

    /* Layout do 82.BGA (arcade) sobre as camadas do 82W.BGA: os dois são iguais
     * exceto por três camadas (comparação keyframe a keyframe, 29/09/2026):
     *   logo.spr  y = +3 em todos os keyframes (82W: -33)
     *   01.spr    último keyframe no frame 300, type 0 (82W: 415)
     *   02.spr    último keyframe no frame 300, type 1 (82W: 415, type 0)
     *   FORPC.SPR não existe no 82.BGA
     * As texturas continuam as do 82W.DAT (os PNGs do 82.DAT extraído estão vazios). */
    if (g_game.bgaPicCount > 0) {
        BGAPicture* pic = &g_game.bgaPics[0];
        for (int i = 0; i < pic->layerCount; i++) {
            BGALayer* L = &pic->layers[i];
            if (_stricmp(L->filename, "logo.spr") == 0) {
                for (int k = 0; k < L->kfCount; k++) L->keyframes[k].y = 3.0f;
            } else if (_stricmp(L->filename, "01.spr") == 0 && L->kfCount == 2) {
                L->keyframes[1].frame = 300; L->keyframes[1].type = 0;
            } else if (_stricmp(L->filename, "02.spr") == 0 && L->kfCount == 2) {
                L->keyframes[1].frame = 300; L->keyframes[1].type = 1;
            } else if (_stricmp(L->filename, "FORPC.SPR") == 0) {
                L->kfCount = 0;
            }
        }
    }
    s_arcJoin = 0; s_arcJoinTimer = 0; s_arcFade = 0;
    Log_Print("MENU ARCADE: ins1a=%d press=%d mah=%d(%d) texts=%d\n",
              s_arcTile[ARC_INS1A], s_arcTile[ARC_PRESS], s_arcTile[ARC_MAH],
              s_arcMahCount, s_arcTexts);
}

/* Tile na posição natural do .spr (+dx para o P2). Centro em Y-down. */
static void arcDrawTile(int tile, float dx, float dy, float alpha)
{
    if (tile < 0 || tile >= g_game.sprTileCount) return;
    float sx = (float)g_game.sprTiles[tile].srcX;
    float sy = (float)g_game.sprTiles[tile].srcY;
    float sw = (float)g_game.sprTiles[tile].srcW;
    float sh = (float)g_game.sprTiles[tile].srcH;
    Sprite_DrawTile(tile, sx + sw / 2.0f + dx, sy + sh / 2.0f + dy, 1.0f, 1.0f, alpha);
}

/* Região do texts.png com o canto inferior esquerdo em (x, y) Y-up */
static void arcDrawTexts(float x, float y, int u0, int v0, int u1, int v1)
{
    if (s_arcTexts < 0) return;
    float w = (float)(u1 - u0), h = (float)(v1 - v0);
    Texture_DrawUV(s_arcTexts, x, 480.0f - y - h, w, h, (float)u0, (float)v0,
                   (float)u1, (float)v1, 1.0f, 1.0f, 1.0f, 1.0f);
}

/* Contador de créditos: fonte do 00.DAT (FONT.PNG, 4ª e 5ª linha).
 * Linha 4 (y 48..59): "CREDIT(S)" e os dígitos 0..9; linha 5 (y 61..72): [ / ]. */
static const int kFontDigitX[10][2] = {
    { 81, 90 }, { 95, 101 }, { 106, 115 }, { 121, 128 }, { 134, 144 },
    { 149, 158 }, { 164, 172 }, { 178, 186 }, { 192, 201 }, { 207, 216 }
};

static void arcDrawFont(float x, float y, int u0, int v0, int u1, int v1)
{
    if (g_fontTexId < 0) return;
    float w = (float)(u1 - u0), h = (float)(v1 - v0);
    Texture_DrawUV(g_fontTexId, x, 480.0f - y - h, w, h, (float)u0, (float)v0,
                   (float)u1, (float)v1, 1.0f, 1.0f, 1.0f, 1.0f);
}

static void arcDrawNumber(float x, float y, int n)   /* 0x405260 */
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", n < 0 ? 0 : n);
    for (const char* c = buf; *c; c++) {
        const int* dx = kFontDigitX[*c - '0'];
        arcDrawFont(x, y, dx[0], 48, dx[1], 59);
        x += 11.0f;
    }
}

/* 0x404f20/0x40507c: CREDIT(S) créditos [ resto / moedas ] — colado na base
 * da tela; x do MK5 */
static void arcDrawCredits(void)
{
    if (Coin_IsFreePlay()) return;          /* o MK5 desenha um sprite FREE PLAY que o PREX3 não tem */
    int coin1 = g_game.svcCoin1 > 0 ? g_game.svcCoin1 : 1;
    const float y = 2.0f;
    arcDrawFont(235.0f, y, 3, 48, 72, 59);                       /* CREDIT(S) */
    arcDrawNumber(326.0f, y, g_game.svcCoinTotal / coin1);
    arcDrawFont(342.0f, y, 3, 61, 9, 72);                        /* [ */
    arcDrawNumber(352.0f, y, g_game.svcCoinTotal % coin1);
    arcDrawFont(364.0f, y, 19, 61, 30, 72);                      /* / */
    if (coin1 >= 10) {
        arcDrawNumber(391.0f, y, coin1);
        arcDrawFont(413.0f, y, 35, 61, 42, 72);                  /* ] */
    } else {
        arcDrawNumber(380.0f, y, coin1);
        arcDrawFont(392.0f, y, 35, 61, 42, 72);
    }
}

static void Menu_RenderArcade(int bgaIndex)
{
    int f = (int)g_game.stateFrame;
    /* 0x409021: layers no frame f % 415 — exceto as camadas do menu do PC
     * (04_x, 05..07, 10..13, 15, 16, todas BA01/BA02). 01.spr e 02.spr (BA02)
     * aparecem: no 82.BGA elas ficam visíveis nos frames 0..300. */
    {
        BGAPicture* pic = &g_game.bgaPics[bgaIndex];
        for (int i = 0; i < pic->layerCount; i++) {
            const char* fn = pic->layers[i].filename;
            if (fn[0] >= '0' && fn[0] <= '9' &&
                _stricmp(fn, "01.spr") != 0 && _stricmp(fn, "02.spr") != 0) continue;
            BGA_SetEventLayer(bgaIndex, f % 0x19f, i);
        }
    }

    bool credit = Coin_GetCredits() > 0;                         /* 0x405480 */
    for (int p = 0; p < 2; p++) {
        if (s_arcJoin & (1 << p)) continue;
        float dx = p ? 320.0f : 0.0f;
        if (credit) {
            /* press center step: balão + texto + painel pisando (6 quadros) */
            int ph = f % 26;
            if (s_arcTile[ARC_MAH] >= 0 && s_arcMahCount > 0)
                arcDrawTile(s_arcTile[ARC_MAH] + ph * s_arcMahCount / 26, dx, 0.0f, 1.0f);
            /* Balão do MAH01.PNG sobre o sprite amarelo (centro x~130, topo y~394):
             * cinza (91,138)-(237,184) 146x46 e contorno branco (8,196)-(168,254)
             * 160x58, alternando como as pílulas do insert coin; texto do press.spr
             * dentro. (O press-b do texts.png não é usado.) */
            if (s_arcTile[ARC_MAH] >= 0) {
                int mt = g_game.sprTiles[s_arcTile[ARC_MAH]].texId;
                if ((f % 26) < 13)
                    Texture_DrawUV(mt, dx + 50.0f, 342.0f, 160.0f, 58.0f,
                                   8.0f, 196.0f, 168.0f, 254.0f, 1, 1, 1, 1.0f);
                else
                    Texture_DrawUV(mt, dx + 57.0f, 348.0f, 146.0f, 46.0f,
                                   91.0f, 138.0f, 237.0f, 184.0f, 1, 1, 1, 1.0f);
            }
            /* press.spr: posição natural (0,5) -> texto no corpo do balão */
            arcDrawTile(s_arcTile[ARC_PRESS], dx + 74.0f, 349.0f, 1.0f);
        } else {
            /* insert coin: pílula branca/cinza alternando a cada 13 frames + texto */
            bool white = (f % 26) < 13;
            int a = p ? ARC_INS2A : ARC_INS1A;
            arcDrawTile(s_arcTile[white ? a + 2 : a + 1], 0.0f, 0.0f, 1.0f);
            arcDrawTile(s_arcTile[a], 0.0f, 0.0f, 1.0f);
        }
    }
    arcDrawCredits();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* Atualização da tela arcade. Devolve true se tratou o frame. */
static bool Menu_UpdateArcade(void)
{
    if (s_arcJoin) { s_arcFade++; s_arcJoinTimer++; }
    else if (g_game.stateFrame >= 0x780 && Coin_GetMaxCredits() == 0)
        s_arcFade++;                                              /* 0x408faf: fade do attract */

    /* 0x4090d6: nos últimos 30 dos 60 frames a tela escurece */
    if (s_arcFade > 30) {
        float a = 1.0f - (60 - s_arcFade) / 30.0f;
        Render_SetGlobalColor(0, 0, 0, a > 1.0f ? 1.0f : a);
    }

    if (s_arcJoin && (s_arcJoinTimer > 0x3c || s_arcJoin == 3)) {  /* 0x408f84 */
        g_game.activePlayerMask = s_arcJoin;
        GameState target = g_game.optionToggle2 ? STATE_HOWTOPLAY : STATE_SONG_SELECT;
        Log_Print("MENU ARCADE: start mask=%d -> %s\n", s_arcJoin, State_ToString(target));
        Game_ChangeState(target);
        return true;
    }
    for (int p = 0; p < 2; p++) {                                 /* 0x409283 */
        if ((s_arcJoin & (1 << p)) || !padHit(p, PAD_C)) continue;
        if (!Coin_HasCredit()) continue;
        Coin_ConsumeCredit();                                     /* 0x4054d0 */
        s_arcJoin |= 1 << p;
        Audio_Play(g_waveSoundIds[SND_2_1], false);
        Log_Print("MENU ARCADE: P%d entrou\n", p + 1);
    }
    return true;
}

static void subEnter(GameState state) {
    g_game.stateFrame = 0;
    g_game.state = state;
}

void Gamestate_UpdateMenu(float dt) {
    (void)dt;
    switch (g_game.state) {
    case STATE_MENU_ENTER:
        Menu_ResetState();
        Font_LoadFontOnly();
        if (g_arcadeStyle) Menu_ArcadeLoad();
        g_game.state = STATE_MENU_INPUT;
        break;
    case STATE_MENU_INPUT:
        if (g_game.confirmActive) {
            g_game.confirmTimer++;
            float a = g_game.confirmTimer * (1.0f / 60.0f);
            if (a > 1.0f) a = 1.0f;
            Render_SetGlobalColor(0, 0, 0, a);
            if (a >= 1.0f) {
                g_game.confirmActive = false;
                g_game.confirmTimer = 0;
                GameState target = g_game.fadeTarget;
                g_game.fadeTarget = 0;
                if (target == STATE_EXIT) {
                    g_game.state = STATE_EXIT;
                    g_game.stateFrame = 0;
                } else {
                    /* Ponto único de cobrança: qualquer caminho que leve à
                     * SongSelect consome um crédito. Em FREE PLAY / EVENT a
                     * função não faz nada. */
                    if (target == STATE_SONG_SELECT)
                        Coin_ConsumeCredit();
                    /* Show Help: se toggle2=ON e indo para SongSelect, passa pelo How To Play */
                    if (target == STATE_SONG_SELECT && g_game.optionToggle2)
                        target = STATE_HOWTOPLAY;
                    Game_ChangeState(target);
                }
            }
            return;
        }
        /* Attract: 1980 frames sem crédito e sem player -> Demo Play (0x404687) */
        if (Attract_MenuTimedOut()) {
            Attract_StartDemo();
            return;
        }
        if (g_arcadeStyle) {
            Menu_UpdateArcade();
            return;
        }
        /* ── P2 input: navega igual P1; UL×2 confirma Start como P2-only ──── */
        {
            int p2btn = -1; /* -1=nenhum, 0=UL, 1=UR, 2=DL, 3=DR */
            int p2sel = 0;
            if (padHit(1, PAD_UL)) { p2btn = PAD_UL; p2sel = 1; }
            else if (padHit(1, PAD_UR)) { p2btn = PAD_UR; p2sel = 2; }
            else if (padHit(1, PAD_DL)) { p2btn = PAD_DL; p2sel = 3; }
            else if (padHit(1, PAD_DR)) { p2btn = PAD_DR; p2sel = 4; }

            if (p2btn >= 0) {
                /* P2 UL×2 (quando já está em UL): confirma Start como P2-only */
                if (p2btn == PAD_UL && g_p2MenuSel == 1) {
                    if (!Coin_HasCredit()) { Log_Print("MENU: sem credito, start P2 bloqueado\n"); return; }
                    Audio_Play(g_waveSoundIds[SND_2_1], false);
                    Log_Print("MENU: P2 confirm UL -> P2-only\n");
                    g_game.activePlayerMask = 0x2;
                    g_game.confirmActive = true; g_game.confirmTimer = 0;
                    g_game.fadeTarget = STATE_SONG_SELECT;
                    return;
                }
                /* Navegação: atualiza seleção visual (mesma animação que P1) */
                Audio_Play(g_waveSoundIds[SND_3_2], false);
                g_p2MenuSel   = p2sel;
                g_menuSelection = p2sel;
                Log_Print("MENU: P2 sel %d\n", p2sel);
                return;
            }
        }

        if (g_menuSelection == 0) {
            if (padHit(0, PAD_UL)) { Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: sel UL->1\n"); g_menuSelection = 1; return; }
            if (padHit(0, PAD_UR)) { Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: sel UR->2\n"); g_menuSelection = 2; return; }
            if (padHit(0, PAD_DL)) { Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: sel DL->3\n"); g_menuSelection = 3; return; }
            if (padHit(0, PAD_DR)) { Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: sel DR->4\n"); g_menuSelection = 4; return; }
        } else {
            if (padHit(0, PAD_UL)) {
                if (g_menuSelection == 1) {
                    /* Sem crédito não inicia. Em FREE PLAY (svcCoin1=0, o
                     * default) ou modo EVENT, Coin_HasCredit() é sempre true. */
                    if (!Coin_HasCredit()) { Log_Print("MENU: sem credito, start bloqueado\n"); return; }
                    g_game.activePlayerMask = 0x1; Audio_Play(g_waveSoundIds[SND_2_1], false); Log_Print("MENU: confirm UL\n"); g_game.confirmActive = true; g_game.confirmTimer = 0; g_game.fadeTarget = STATE_SONG_SELECT; return;
                }
                Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: change UL->1\n"); g_menuSelection = 1; return;
            }
            if (padHit(0, PAD_UR)) {
                if (g_menuSelection == 2) { Audio_Play(g_waveSoundIds[SND_2_1], false); Log_Print("MENU: confirm UR\n"); g_game.confirmActive = true; g_game.confirmTimer = 0; g_game.fadeTarget = STATE_GAMEOPTION_ENTER; return; }
                Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: change UR->2\n"); g_menuSelection = 2; return;
            }
            if (padHit(0, PAD_DL)) {
                if (g_menuSelection == 3) { Audio_Play(g_waveSoundIds[SND_2_1], false); Log_Print("MENU: confirm DL -> STAFF\n"); g_game.confirmActive = true; g_game.confirmTimer = 0; g_game.fadeTarget = STATE_STAFF_ENTER; return; }
                Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: change DL->3\n"); g_menuSelection = 3; return;
            }
            if (padHit(0, PAD_DR)) {
                if (g_menuSelection == 4) { Audio_Play(g_waveSoundIds[SND_2_1], false); g_game.confirmActive = true; g_game.confirmTimer = 0; g_game.fadeTarget = STATE_EXIT; Log_Print("MENU: confirm DR -> EXIT\n"); return; }
                Audio_Play(g_waveSoundIds[SND_3_2], false); Log_Print("MENU: change DR->4\n"); g_menuSelection = 4; return;
            }
        }
        break;
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
    if (g_game.state != STATE_MENU_ENTER && g_game.state != STATE_MENU_INPUT &&
        g_game.state != STATE_EXIT) return;

    if (g_arcadeStyle) {
        Menu_RenderArcade(bgaIndex);
        return;
    }

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

    // Desenha a string de versão no canto inferior direito (original: Menu_UpdateInput)
    // Sombra preta em (560,458)
    glColor4f(0, 0, 0, 1);
    Font_DrawText(560.0f, 458.0f, GetVersionString());
    // Texto branco em (560,459) — 1px acima pra efeito de shadow
    glColor4f(1, 1, 1, 1);
    Font_DrawText(560.0f, 459.0f, GetVersionString());
}
