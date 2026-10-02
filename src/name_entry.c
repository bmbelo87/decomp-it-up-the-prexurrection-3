/* name_entry.c — Enter Your Name (085.DAT)
 *
 * O PREX3 (PumpyOriginal.exe) não tem esta tela: o fim de crédito vai direto ao
 * Game Over. A referência é o PREX3-MK5 (PIU32.EXE); endereços abaixo são dele.
 * Detalhes em docs/NAME_ENTRY.md.
 *
 *   0x1b  0x406ce0  Enter: 085.DAT, FONT1~4, AL_FRAME.SPR, AUDIO\085.AUD
 *   0x1c  0x406f90  Intro (60 frames); sem jogador pendente -> 0x1e
 *   0x1d  0x407d90  Digitação
 *   0x1e  0x408550  Para o som -> Game Over (0x17)
 *
 * O score que entra no ranking é o total do crédito: o MK5 preserva 0xed50a4 /
 * 0xed513c ao carregar a música a partir do 2º stage (0x410960) e soma cada stage
 * em 0x4125b0.
 */

#include "pumpy.h"
#include <math.h>

#define NE_CHARS        58
#define NE_IDX_BACK     56      /* 0x01 */
#define NE_IDX_END      57      /* 0x00 */
#define NE_INTRO_FRAMES 60      /* 0x3c */
#define NE_TIMER_START  40

/* 0x43fd74 */
static const char kNeChars[NE_CHARS] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T',
    'U','V','W','X','Y','Z','0','1','2','3','4','5','6','7','8','9','~','!','@','#',
    '$','%','^','&','*','(',')','_','-','+','=','\\',':',';','/','?', 0x01, 0x00
};

/* 0x43fdb4.. (X), 0x43fde4.. (escala) — slot 0 é o centro; 5 é o de entrada */
static const float kSlotX[6]     = { 0.0f, 115.0f, 210.0f, 260.0f, 300.0f, 330.0f };
static const float kSlotScale[6] = { 1.0f, 0.85f, 0.75f, 0.65f, 0.55f, 0.55f };

static uint32_t g_neTotal[2];       /* score acumulado no crédito (0xed50a4 / 0xed513c) */
static bool     g_nePending[2];     /* 0xed30d4 (P1) / 0xed30d0 (P2) */
static char     g_neName[2][4];     /* 0xed30f0 (P1) / 0xed30ec (P2) */
static int      g_neCursor;         /* 0xed4024 */
static int      g_nePos;            /* 0xed30e4 */
static int      g_neDir;            /* 0xedabc4: 1 = direita, 2 = esquerda */
static int      g_neStep;           /* 0xed30e8: frames desde o último passo */
static bool     g_neRepeat;         /* 0xed30d8 */
static int      g_neCounter;        /* 0xed30dc */
static int      g_neFont[4] = { -1, -1, -1, -1 };
static int      g_neFrameTile = -1; /* AL_FRAME.SPR tile 0 (0xed30f8) */
static bool     g_neDone;           /* já passou pela tela neste Game Over */

/* ------------------------------------------------------------ Ranking ---- */

/* 0x406cc0: primeira posição com score maior (unsigned); -1 se não entra */
static int neRankFind(uint32_t score)
{
    for (int i = 0; i < Ranking_GetCount(); i++)
        if ((uint32_t)Ranking_GetScore(i) < score) return i;
    return -1;
}

void NameEntry_ResetTotals(void)
{
    g_neTotal[0] = g_neTotal[1] = 0;
    g_neDone = false;
}

void NameEntry_AddStageScore(void)
{
    for (int p = 0; p < 2; p++)
        if (g_game.activePlayerMask & (1 << p))
            g_neTotal[p] += g_game.stats.score[p];
    Log_Print("NAME: total P1=%u P2=%u\n", g_neTotal[0], g_neTotal[1]);
}

/* 0x413267: algum jogador ativo entra no Top 20? */
bool NameEntry_ShouldEnter(void)
{
    if (g_neDone || Attract_IsDemo()) return false;
    for (int p = 0; p < 2; p++)
        if ((g_game.activePlayerMask & (1 << p)) && neRankFind(g_neTotal[p]) >= 0)
            return true;
    return false;
}

/* ------------------------------------------------------------ Desenho ---- */

static void neBindFontFor(int i)
{
    int tex = g_neFont[(i / 16) & 3];
    if (tex >= 0) Texture_Bind(tex);
    glEnable(GL_TEXTURE_2D);
}

/* Glifo 64x64 da célula i: coluna i%4 (0x43fd38), linha (i/4)%4 (0x43fcfc) */
static void neGlyphUV(int i, float* u0, float* v0, float* u1, float* v1)
{
    *u0 = (float)((i % 4) * 64) / 256.0f;
    *v0 = (float)(((i / 4) % 4) * 64) / 256.0f;
    *u1 = *u0 + 0.25f;
    *v1 = *v0 + 0.25f;
}

static int neCharIndex(char c)      /* 0x406910 */
{
    for (int i = 0; i < NE_CHARS; i++)
        if (kNeChars[i] == c) return i;
    return NE_CHARS;
}

/* 0x406950: glifo 34x34 com canto inferior esquerdo em (x,y) — cor definida antes */
static void neDrawGlyph(float x, float y, char c)
{
    int i = neCharIndex(c);
    if (i >= NE_CHARS || c == 0 || c == 1) return;
    float u0, v0, u1, v1;
    neGlyphUV(i, &u0, &v0, &u1, &v1);
    neBindFontFor(i);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x,         y + 34.0f);
    glTexCoord2f(u0, v1); glVertex2f(x,         y);
    glTexCoord2f(u1, v1); glVertex2f(x + 34.0f, y);
    glTexCoord2f(u1, v0); glVertex2f(x + 34.0f, y + 34.0f);
    glEnd();
}

/* 0x406c00 (letras, avanço 24) / 0x406c60 (números, avanço 20) */
static void neDrawText(float x, float y, const char* s, float adv)
{
    for (; *s; s++, x += adv)
        neDrawGlyph(x, y, *s);
}

/* 0x406aa0: glifo centrado na origem (±17), usado pela roda */
static void neDrawGlyphCentered(int i)
{
    if (i < 0 || i >= NE_IDX_BACK) {
        /* BACK/END: a fonte tem os ícones nas células 56/57 (FONT4) */
        if (i != NE_IDX_BACK && i != NE_IDX_END) return;
    }
    float u0, v0, u1, v1;
    neGlyphUV(i, &u0, &v0, &u1, &v1);
    neBindFontFor(i);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(-17.0f,  17.0f);
    glTexCoord2f(u0, v1); glVertex2f(-17.0f, -17.0f);
    glTexCoord2f(u1, v1); glVertex2f( 17.0f, -17.0f);
    glTexCoord2f(u1, v0); glVertex2f( 17.0f,  17.0f);
    glEnd();
}

/* AL_FRAME tile 0: posição natural (0,0,152,160) em Y-down = centro (76,400) em Y-up.
 * Desenhado centrado na origem. */
static void neDrawFrameTile(void)
{
    if (g_neFrameTile < 0 || g_neFrameTile >= g_game.sprTileCount) return;
    SPRTileDef* t = &g_game.sprTiles[g_neFrameTile];
    if (t->texId < 0) return;
    float hw = t->srcW * 0.5f, hh = t->srcH * 0.5f;
    Texture_Bind(t->texId);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(t->u1, t->v1); glVertex2f(-hw,  hh);
    glTexCoord2f(t->u1, t->v2); glVertex2f(-hw, -hh);
    glTexCoord2f(t->u2, t->v2); glVertex2f( hw, -hh);
    glTexCoord2f(t->u2, t->v1); glVertex2f( hw,  hh);
    glEnd();
}

/* 0x406e60(x, y, ch, rot=0, s, a): moldura + glifo da roda */
static void neDrawWheelItem(float x, float y, int idx, float s, float a)
{
    glColor4f(1.0f, 1.0f, 1.0f, a);
    glPushMatrix();
    glTranslatef(x + 76.0f, y + 400.0f, 0.0f);
    glScalef(s, s, 1.0f);
    neDrawFrameTile();
    glPopMatrix();

    glColor4f(1.0f, 1.0f, 1.0f, a * 0.5f);
    glPushMatrix();
    glTranslatef(x + 74.0f, y + 400.0f, 0.0f);
    glScalef(3.5f * s, 3.5f * s, 1.0f);
    neDrawGlyphCentered(idx);
    glPopMatrix();
}

static float neLerp(float a, float b, float t) { return a + (b - a) * t; }

/* 0x407720: 9 letras em volta do cursor, X = 245 ± slot, Y = -104.
 * Na animação cada letra vem do slot vizinho em 20 frames (t*0.05). */
static void neDrawWheel(int dir, int t)
{
    float shift = 0.0f;
    if (dir != 0 && t <= 20) {
        float p = 1.0f - t * 0.05f;
        shift = (dir == 1) ? p : -p;
    }
    for (int k = -4; k <= 4; k++) {
        float r = k + shift;
        float ar = fabsf(r);
        if (ar > 5.0f) continue;
        int s0 = (int)ar;
        float fr = ar - (float)s0;
        int s1 = s0 < 5 ? s0 + 1 : 5;
        float dx = neLerp(kSlotX[s0], kSlotX[s1], fr);
        float sc = neLerp(kSlotScale[s0], kSlotScale[s1], fr);
        float a  = (ar <= 3.0f) ? 1.0f : (ar >= 4.0f ? 0.0f : 4.0f - ar);
        if (a <= 0.0f) continue;
        int idx = ((g_neCursor + k) % NE_CHARS + NE_CHARS) % NE_CHARS;
        neDrawWheelItem(r < 0.0f ? 245.0f - dx : 245.0f + dx, -104.0f, idx, sc, a);
    }
}

/* 0x407610: timer de 2 dígitos em FONT4, blend aditivo */
static void neDrawTimer(int value, int y)
{
    if (value < 0) value = 0;
    if (g_neFont[3] < 0) return;
    Texture_Bind(g_neFont[3]);
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    int x = 600;
    for (int n = 0; n < 2; n++, x -= 34) {
        int d = value % 10;
        value /= 10;
        float u0 = d * 0.0976562f, u1 = u0 + 0.0976562f;
        glBegin(GL_QUADS);
        glTexCoord2f(u0, 0.75f);    glVertex2i(x, y + 34);
        glTexCoord2f(u0, 0.84766f); glVertex2i(x, y);
        glTexCoord2f(u1, 0.84766f); glVertex2i(x + 34, y);
        glTexCoord2f(u1, 0.75f);    glVertex2i(x + 34, y + 34);
        glEnd();
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static int neCurrentPlayer(void) { return g_nePending[0] ? 0 : (g_nePending[1] ? 1 : -1); }

/* Linha do jogador (glScalef 1.5): rank, nome, score */
static void neDrawPlayerRow(int p, bool cursor)
{
    char buf[16];
    glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
    glPushMatrix();
    glScalef(1.5f, 1.5f, 1.0f);
    snprintf(buf, sizeof(buf), "%02d", neRankFind(g_neTotal[p]) + 1);
    neDrawText(44.0f, 44.0f, buf, 20.0f);
    char nm[5];
    for (int c = 0; c < 4; c++) {
        char ch = g_neName[p][c];
        nm[c] = (ch == 0 || ch == 1) ? ' ' : ch;
    }
    nm[4] = '\0';
    neDrawText(103.0f, 44.0f, nm, 24.0f);
    if (cursor && (g_neCounter % 60) > 30) {        /* '_' piscando na posição */
        char cur[5] = "    ";
        if (g_nePos < 4) cur[g_nePos] = '_';
        neDrawText(103.0f, 42.0f, cur, 24.0f);
    }
    snprintf(buf, sizeof(buf), "%07u", g_neTotal[p]);
    neDrawText(218.0f, 44.0f, buf, 20.0f);
    glPopMatrix();
}

/* ------------------------------------------------------------ Estados ---- */

static void NameEntry_Enter(void)
{
    char path[MAX_PATH];
    static const char* kFonts[4] = { "FONT1.TGA", "FONT2.TGA", "FONT3.TGA", "FONT4.TGA" };
    snprintf(path, sizeof(path), "%s/BGA/085.DAT", g_game.currentDirectory);
    for (int f = 0; f < 4; f++)
        g_neFont[f] = Resource_LoadTextureFromDAT(path, kFonts[f]);
    int before = g_game.sprTileCount;
    g_neFrameTile = (Resource_LoadSPR(path, "AL_FRAME.SPR") > 0) ? before : -1;

    snprintf(path, sizeof(path), "%s/AUDIO/085.AUD", g_game.currentDirectory);
    BGM_Stop();
    if (BGM_LoadAUDDirect(path)) BGM_Play(true);

    memset(g_neName, ' ', sizeof(g_neName));
    for (int p = 0; p < 2; p++)
        g_nePending[p] = (g_game.activePlayerMask & (1 << p)) && neRankFind(g_neTotal[p]) >= 0;
    g_neCursor = 0; g_nePos = 0; g_neDir = 0; g_neStep = 0; g_neRepeat = false;
    g_neCounter = 0;
    g_game.bgaLoop = false;
    Log_Print("NAME: enter P1=%d(%u) P2=%d(%u) fontes %d %d %d %d frame=%d\n",
              g_nePending[0], g_neTotal[0], g_nePending[1], g_neTotal[1],
              g_neFont[0], g_neFont[1], g_neFont[2], g_neFont[3], g_neFrameTile);
}

static void neFinish(void)
{
    g_neDone = true;
    BGM_Stop();                           /* 0x408550 */
    Game_ChangeState(STATE_GAMEOVER_ENTER);
}

static void neMove(int dir)
{
    g_neDir = dir;
    g_neStep = 0;
    g_neCursor += (dir == 1) ? 1 : -1;
    if (g_neCursor < 0) g_neCursor += NE_CHARS;
    if (g_neCursor >= NE_CHARS) g_neCursor -= NE_CHARS;
    Audio_Play(g_waveSoundIds[SND_3_2], false);
}

void NameEntry_Update(void)
{
    switch (g_game.state) {
    case STATE_NAME_ENTER:
        NameEntry_Enter();
        g_game.state = STATE_NAME_INTRO;
        g_game.stateFrame = 0;
        break;

    case STATE_NAME_INTRO:                            /* 0x406f90 */
        if (neCurrentPlayer() < 0) { neFinish(); return; }
        if (++g_neCounter >= NE_INTRO_FRAMES) {
            g_neCounter = 0;
            g_game.state = STATE_NAME_INPUT;
            g_game.stateFrame = 0;
        }
        break;

    case STATE_NAME_INPUT: {                          /* 0x407d90 */
        int p = neCurrentPlayer();
        if (p < 0) { neFinish(); return; }
        int timer = NE_TIMER_START - (int)g_game.stateFrame / 60;
        char* name = g_neName[p];

        if (g_neDir != 0 && g_neStep >= 20) g_neDir = 0;

        /* esquerda/direita: UL/UR (bits 0x1/0x2 do jogador) */
        if (Input_IsPadHit(p, PAD_UL))      { g_neRepeat = false; neMove(2); }
        else if (Input_IsPadHit(p, PAD_UR)) { g_neRepeat = false; neMove(1); }
        else if (Input_IsPadDown(p, PAD_UL) && (g_neStep > 30 || (g_neRepeat && g_neStep > 20)))
            { g_neRepeat = true; neMove(2); }
        else if (Input_IsPadDown(p, PAD_UR) && (g_neStep > 30 || (g_neRepeat && g_neStep > 20)))
            { g_neRepeat = true; neMove(1); }

        /* pré-visualização da letra sob o cursor */
        if (g_nePos < 4) name[g_nePos] = kNeChars[g_neCursor];

        bool finish = timer <= 0;
        if (Input_IsPadHit(p, PAD_C)) {               /* centro (bit 0x4) */
            char c = kNeChars[g_neCursor];
            if (c == 0x01) {
                if (g_nePos > 0) { name[g_nePos < 4 ? g_nePos : 3] = ' '; g_nePos--; }
            } else if (c == 0x00) {
                finish = true;
            } else {
                g_nePos++;
            }
            Audio_Play(g_waveSoundIds[SND_2_1], false);
        }

        if (finish) {
            if (g_nePos < 4) name[g_nePos] = ' ';
            if (name[0] == ' ' && name[1] == ' ' && name[2] == ' ' && name[3] == ' ')
                memcpy(name, "PUMP", 4);
            char nm[5];
            memcpy(nm, name, 4); nm[4] = '\0';
            Var_SetSystemVariable((int)g_neTotal[p], nm);      /* 0x405650 */
            Log_Print("NAME: P%d '%s' %u\n", p + 1, nm, g_neTotal[p]);
            g_nePending[p] = false;
            g_neCursor = 0; g_nePos = 0; g_neDir = 0; g_neStep = 0; g_neRepeat = false;
            g_neCounter = 0;
            g_game.state = STATE_NAME_INTRO;              /* 0x408487: volta à intro */
            g_game.stateFrame = 0;
            return;
        }

        if (g_nePos >= 4) g_neCursor = NE_IDX_END;       /* 0x4084dd */
        g_neCounter++;
        g_neStep += g_neRepeat ? 2 : 1;
        break;
    }
    default:
        break;
    }
}

/* Camadas do 085.BGA (0x403d00) */
static void neDrawLayers(int p, int frame)
{
    static const int kLoop[] = { 2, 7, 10, 0x22, 0x23, 0x25, 0x26, 0x29, 0x2a };
    BGA_SetEventLayer(0, p == 0 ? 0x168 : 0x20e, p == 0 ? 0x25 : 0x26);
    for (size_t i = 0; i < sizeof(kLoop) / sizeof(kLoop[0]); i++)
        BGA_SetEventLayer(0, frame, kLoop[i]);
    BGA_SetEventLayer(0, 0x3c, 0x2b);
    BGA_SetEventLayer(0, 0x3c, 0x2d);
    BGA_SetEventLayer(0, 0, p == 0 ? 0xc : 0xd);
}

void NameEntry_Render(void)
{
    int p = neCurrentPlayer();
    if (p < 0) return;

    if (g_game.state == STATE_NAME_INTRO) {
        neDrawLayers(p, g_neCounter);
        neDrawWheel(0, 0);
        neDrawPlayerRow(p, false);
        int y = 424;
        if (g_neCounter <= 10) y = (int)(424.0f + 250.0f - 25.0f * g_neCounter);
        neDrawTimer(NE_TIMER_START, y);
    } else if (g_game.state == STATE_NAME_INPUT) {
        neDrawLayers(p, g_neCounter % 0xe3);
        neDrawWheel(g_nePos < 4 ? g_neDir : 0, g_neStep);
        /* setas 0x2f (->) / 0x30 (<-): frame 11 + 1.5*t enquanto anima */
        int fa = (g_neDir == 1 && g_neStep <= 20) ? (int)(g_neStep * 1.5 + 11.0) : 11;
        int fb = (g_neDir == 2 && g_neStep <= 20) ? (int)(g_neStep * 1.5 + 11.0) : 11;
        BGA_SetEventLayer(0, fa, 0x2f);
        BGA_SetEventLayer(0, fb, 0x30);
        neDrawPlayerRow(p, true);
        neDrawTimer(NE_TIMER_START - (int)g_game.stateFrame / 60, 424);
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
