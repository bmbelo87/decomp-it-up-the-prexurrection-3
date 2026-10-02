/* attract.c — Ciclo de demonstração (attract) e tela de High Scores (084.DAT)
 *
 * O PREX3 (PumpyOriginal.exe, estado 0 = 0x4020a0, contador 0x8bbf40 % 3) cicla
 * Logo -> Menu -> Demo Play. A base usada aqui é o PREX3-MK5 (PIU32.EXE, estado 0 =
 * 0x4044b0, contador 0x5e05e8 % 4), que acrescenta a tela de High Scores:
 *
 *   Logo 81.DAT -> Menu 82W.DAT -> Demo Play -> High Scores 084.DAT -> Logo ...
 *
 * O port não tem o "estado 0" despachante: cada tela chama a próxima ao terminar,
 * na mesma ordem. Detalhes e endereços em docs/NAME_ENTRY.md.
 *
 * Tempos (update fixo a 60 Hz):
 *   Menu         1980 frames (0x7bc; PREX3 0x404687, MK5 0x40923e), só sem crédito
 *   Demo         8400 ticks de 240 Hz = 35 s = 2100 frames (PREX3 0x4149d5, MK5 0x411497)
 *   High Scores  133 frames (0x408660) + 900 frames de lista (0x4087e0) + fade de 30
 *
 * Crédito: o original testa 0x402460 (Coin_GetMaxCredits), que devolve 99 em FREE
 * PLAY/EVENT. Por isso nesses modos o menu nunca expira e o demo/high scores saem
 * na hora — comportamento do original, mantido.
 */

#include "pumpy.h"
#include "bga.h"

#define MENU_ATTRACT_FRAMES   1980   /* 0x7bc */
#define DEMO_FRAMES           2100   /* 0x20d0 ticks / 240 Hz * 60 */
#define HS_PAGE_FRAMES        133    /* 0x85 */
#define HS_LIST_FRAMES        900    /* 0x384 */
#define HS_FADE_FRAMES        30     /* 0x1e */

/* ------------------------------------------------------------------ Demo --- */

static bool g_demoActive;
static int  g_demoCount;      /* 0xd35edc (PREX3) — escolhe a música */
static int  g_demoHard;       /* 0xd5fde0 — alterna -n / -h */
static int  g_demoFrames;

bool Attract_IsDemo(void) { return g_demoActive; }

/* 0x407cb0: índice = demoCount % total do modo; a lista lida (0xd5f622) é a mesma
 * para -n e -h, só o total muda (0xd5fd3a / 0xd5fd3e). Aqui a lista é a do EASY
 * (hipótese: 0xd5f622 é a primeira lista do Stage.cfg). */
void Attract_StartDemo(void)
{
    int easy = Song_FindMode(&g_game.songDB, "EASY");
    int hard = Song_FindMode(&g_game.songDB, "HARD");
    int mode = g_demoHard ? hard : easy;
    if (easy < 0 || mode < 0 || g_game.songDB.modes[easy].songCount <= 0 ||
        g_game.songDB.modes[mode].songCount <= 0) {
        Log_Print("ATTRACT: demo sem lista de musicas, pulando\n");
        Attract_Next(STATE_HIGHSCORE_ENTER);
        return;
    }
    SongMode* list = &g_game.songDB.modes[easy];
    int idx = g_demoCount % g_game.songDB.modes[mode].songCount;
    if (idx >= list->songCount) idx %= list->songCount;
    int songId = list->songIds[idx];
    Log_Print("ATTRACT: demo %d %s (#%d)\n", songId, g_demoHard ? "-h" : "-n", g_demoCount);

    g_demoCount++;
    g_demoHard = !g_demoHard;

    /* igual ao "run" do console: 0x410cf0 */
    BGM_Stop();
    Menu_ResetState();
    g_game.activePlayerMask = 0x3;       /* -demo: máscara 0x100533, os dois players */
    g_game.selectedModeIndex = mode;
    g_demoActive = true;
    g_demoFrames = 0;
    Loading_Enter(songId);
}

static void Attract_EndDemo(void)
{
    Log_Print("ATTRACT: fim do demo (%d frames)\n", g_demoFrames);
    g_demoActive = false;
    BGM_Stop();
    Resource_ClearBGA();
    Menu_ResetState();
    Attract_Next(STATE_HIGHSCORE_ENTER);
}

/* Chamada todo frame pelo Game_Update, antes do switch de estados. */
void Attract_UpdateDemo(void)
{
    if (!g_demoActive) return;

    /* com o bit 0x100000 o jogo ignora o pad (0x402987: 0xc1a494 &= 0xffffe0e0) */
    memset(g_game.input.padState, 0, sizeof(g_game.input.padState));

    /* A música acabou antes dos 35 s: o port iria para o resultado */
    if (g_game.state == STATE_DANCE_GRADE_ENTER || g_game.state == STATE_DANCE_GRADE_DISPLAY ||
        g_game.state == STATE_STAGE_BREAK) {
        Attract_EndDemo();
        return;
    }
    if (g_game.state != STATE_SONG_TITLE && g_game.state != STATE_SONG_TITLE_OUT &&
        g_game.state != STATE_GAMEPLAY) {
        g_demoActive = false;            /* saiu por outro caminho (ESC, service) */
        return;
    }
    g_demoFrames++;
    /* 0x4149c7: crédito ou relógio >= 0x20d0 -> 0x411890 (volta ao estado 0) */
    if (Coin_GetMaxCredits() != 0 || g_demoFrames >= DEMO_FRAMES)
        Attract_EndDemo();
}

/* --------------------------------------------------------- Sequência ------ */

/* Próxima tela do ciclo. 'next' é a tela que vem depois da atual. */
void Attract_Next(GameState next)
{
    switch (next) {
    case STATE_LOGO_ENTER:
    case STATE_MENU_ENTER:
    case STATE_HIGHSCORE_ENTER:
        Game_ChangeState(next);
        break;
    default:          /* demo */
        Attract_StartDemo();
        break;
    }
}

/* Menu 82W: 0x404687 — sai com contador >= 0x7bc, sem crédito e sem player. */
bool Attract_MenuTimedOut(void)
{
    return g_game.stateFrame >= MENU_ATTRACT_FRAMES && Coin_GetMaxCredits() == 0 &&
           !g_game.confirmActive && !Menu_ArcadeHasJoin();
}

/* ------------------------------------------------------ High Scores ------ */

static int g_hsFont[4] = { -1, -1, -1, -1 };  /* 0xed20bc..0xed20c8: FONT1~4 */
static int g_hsFade;                           /* 0xed30e8 */

/* 0x43fd74: tabela de 58 caracteres (as fontes do 084/085 seguem essa ordem) */
static const char kHsChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789~!@#$%^&*()_-+=\\:;/?";

static int hsCharIndex(char c)   /* 0x406910 — 58 = fora da tabela */
{
    for (int i = 0; i < 56; i++)
        if (kHsChars[i] == c) return i;
    return 58;
}

/* 0x406950: glifo 34x34 com canto inferior esquerdo em (x,y).
 * Textura FONT(1 + i/16); célula de 64 px: coluna 0x43fd38[i] = i%4,
 * linha 0x43fcfc[i] = (i/4)%4. */
static void hsDrawGlyph(float x, float y, char c, float alpha)
{
    int i = hsCharIndex(c);
    if (i >= 58) return;
    int tex = g_hsFont[(i / 16) & 3];
    if (tex < 0) return;
    float u0 = (float)((i % 4) * 64) / 256.0f;
    float v0 = (float)(((i / 4) % 4) * 64) / 256.0f;
    float u1 = u0 + 64.0f / 256.0f;
    float v1 = v0 + 64.0f / 256.0f;
    Texture_Bind(tex);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x,         y + 34.0f);
    glTexCoord2f(u1, v0); glVertex2f(x + 34.0f, y + 34.0f);
    glTexCoord2f(u1, v1); glVertex2f(x + 34.0f, y);
    glTexCoord2f(u0, v1); glVertex2f(x,         y);
    glEnd();
}

/* 0x406c00: texto com avanço de 24 px por caractere */
static void hsDrawText(float x, float y, const char* s, float alpha)
{
    for (; *s; s++, x += 24.0f)
        hsDrawGlyph(x, y, *s, alpha);
}

/* Uma linha: rank " %2d" em x=25, nome em x=135, score "%08d" em x=385 */
static void hsDrawRow(int i, float y, float alpha)
{
    char buf[16];
    snprintf(buf, sizeof(buf), " %2d", i + 1);
    hsDrawText(25.0f, y, buf, alpha);
    hsDrawText(135.0f, y, Ranking_GetName(i), alpha);
    snprintf(buf, sizeof(buf), "%08d", Ranking_GetScore(i));
    hsDrawText(385.0f, y, buf, alpha);
}

/* 0x408580: carrega 084.DAT + FONT1~4.TGA */
static void HighScore_Enter(void)
{
    char path[MAX_PATH];
    static const char* kFonts[4] = { "FONT1.TGA", "FONT2.TGA", "FONT3.TGA", "FONT4.TGA" };
    snprintf(path, sizeof(path), "%s/BGA/084.DAT", g_game.currentDirectory);
    for (int f = 0; f < 4; f++)
        g_hsFont[f] = Resource_LoadTextureFromDAT(path, kFonts[f]);
    g_hsFade = 0;
    g_game.bgaFrame = 0;
    g_game.bgaLoop = false;
    Log_Print("ATTRACT: high scores (fontes %d %d %d %d)\n",
              g_hsFont[0], g_hsFont[1], g_hsFont[2], g_hsFont[3]);
}

void HighScore_Update(void)
{
    /* 0x408663 / 0x4087e4: com crédito vai para o menu */
    if (g_game.state != STATE_HIGHSCORE_ENTER && Coin_GetMaxCredits() != 0) {
        Render_SetGlobalColor(0, 0, 0, 0);
        Game_ChangeState(STATE_MENU_ENTER);
        return;
    }
    switch (g_game.state) {
    case STATE_HIGHSCORE_ENTER:
        HighScore_Enter();
        if (Coin_GetMaxCredits() != 0) {       /* 0x408596 */
            Game_ChangeState(STATE_MENU_ENTER);
            return;
        }
        g_game.state = STATE_HIGHSCORE_PAGE;
        g_game.stateFrame = 0;
        break;
    case STATE_HIGHSCORE_PAGE:                  /* 0x408660 */
        if (g_game.stateFrame >= HS_PAGE_FRAMES) {
            g_game.state = STATE_HIGHSCORE_LIST;
            g_game.stateFrame = 0;
            g_hsFade = 0;
        }
        break;
    case STATE_HIGHSCORE_LIST:                  /* 0x4087e0 */
        if ((int)g_game.stateFrame > HS_LIST_FRAMES) {
            g_hsFade++;
            Render_SetGlobalColor(0, 0, 0, g_hsFade / (float)HS_FADE_FRAMES);
            if (g_hsFade > HS_FADE_FRAMES) {    /* 0x408b90: para o som -> estado 0 */
                Render_SetGlobalColor(0, 0, 0, 1);
                Attract_Next(STATE_LOGO_ENTER);
            }
        }
        break;
    default:
        break;
    }
}

void HighScore_Render(void)
{
    int f = (int)g_game.stateFrame;

    if (g_game.state == STATE_HIGHSCORE_PAGE) {
        /* 0x403cc0(bga, f): todos os layers no frame f */
        BGA_SetEventFrame(0, f);
        /* 8 linhas com fade: alpha = (f - 78 - 5i) / 15, y = 480 - 132 - 48i */
        for (int i = 0; i < 8; i++) {
            int t = f - 0x4e - i * 5;
            if (t <= 0) continue;
            hsDrawRow(i, 480.0f - 132.0f - 48.0f * i, t * 0.06667f);
        }
        return;
    }
    if (g_game.state != STATE_HIGHSCORE_LIST) return;

    /* fundo: layer 2 em 133 + f%59, layers 3..7 em 133 + f%120 */
    BGA_SetEventLayer(0, 0x85 + f % 0x3b, 2);
    for (int l = 3; l <= 7; l++)
        BGA_SetEventLayer(0, 0x85 + f % 0x78, l);

    /* primeira linha visível: (f-120)/48, no máximo 12; 10 linhas desenhadas */
    int first = 0;
    if (f > 0x78) {
        first = (f - 0x78) / 0x30;
        if (first > 12) first = 12;
    }
    for (int k = 0; k < 10; k++) {
        int i = first + k;
        if (i >= 20) break;
        float ty;
        if (f < 0x78 || first == 12)
            ty = -48.0f * k;
        else
            ty = (float)((f - 0x78) % 0x30) - 48.0f * k;

        /* a linha que sobe além da posição de cima some (alpha (48-ty)/48) */
        float a = 1.0f;
        if (f < HS_LIST_FRAMES && ty > 0.0f && ty < 48.0f)
            a = (48.0f - ty) / 48.0f;

        glPushMatrix();
        glTranslatef(0.0f, ty, 0.0f);
        BGA_SetEventLayer(0, 0x85, 0x12);        /* barra da linha */
        hsDrawRow(i, 348.0f, a);
        glPopMatrix();
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
