#include "pumpy.h"
#include <string.h>

/* ── Command detection ──────────────────────────────────────────────────────
 * Buffer5 (5-botões): velocidade e Vanish/NonStep
 *   Speed:   UL UR UL UR CN → x1→x2→x3→x4→RV→x1
 *   Vanish:  UL UR DL DR CN → Vanish→NonStep→OFF
 *
 * Buffer9 (9-botões): cheats longos
 *   Random Velocity: UL UR UL UR UL UR UL UR CN
 *   Mirror:          DR DL UR UL DR DL UR UL CN
 *   Random Step:     UL UR UL UR DL DR DL DR CN
 *   Freedom:         UL DL UR DR DR UL UR DL CN  (só som, sem ícone)
 *   Earthworm:       DR DL UR UL DR UR DL UL CN  (só som, sem ícone)
 *
 * Buffer6 (simultâneo): DL+DR × 3 = Reset todos os cheats do player
 */
#define CMD_BUF_LEN  5
#define CMD_BUF9_LEN 9

static PadButton g_cmdBuf[2][CMD_BUF_LEN];
static int       g_cmdBufCount[2]  = {0, 0};
static int       g_cmdSpeedIdx[2]  = {0, 0};

static PadButton g_cmdBuf9[2][CMD_BUF9_LEN];
static int       g_cmdBuf9Count[2] = {0, 0};

static int       g_cmdBuf6Count[2] = {0, 0}; /* pares DL+DR detectados por player */
static int       g_cmdBuf6State[2] = {0, 0}; /* 0=idle, 1=DL recebido aguardando DR */

/* Buffer5 sequências */
static const PadButton k_speedSeq[CMD_BUF_LEN] = {
    PAD_UL, PAD_UR, PAD_UL, PAD_UR, PAD_C
};
static const PadButton k_vanishSeq[CMD_BUF_LEN] = {
    PAD_UL, PAD_UR, PAD_DL, PAD_DR, PAD_C
};

/* Buffer9 sequências [0=RV, 1=Mirror, 2=RandomStep, 3=Freedom, 4=Earthworm] */
static const PadButton k_seq9[5][CMD_BUF9_LEN] = {
    { PAD_UL, PAD_UR, PAD_UL, PAD_UR, PAD_UL, PAD_UR, PAD_UL, PAD_UR, PAD_C }, /* Random Velocity */
    { PAD_DR, PAD_DL, PAD_UR, PAD_UL, PAD_DR, PAD_DL, PAD_UR, PAD_UL, PAD_C }, /* Mirror          */
    { PAD_UL, PAD_UR, PAD_UL, PAD_UR, PAD_DL, PAD_DR, PAD_DL, PAD_DR, PAD_C }, /* Random Step     */
    { PAD_UL, PAD_DL, PAD_UR, PAD_DR, PAD_DR, PAD_UL, PAD_UR, PAD_DL, PAD_C }, /* Freedom         */
    { PAD_DR, PAD_DL, PAD_UR, PAD_UL, PAD_DR, PAD_UR, PAD_DL, PAD_UL, PAD_C }, /* Earthworm       */
};

/* Ciclo de velocidade: x1→x2→x3→x4→RV→x1 */
#define CMD_SPEED_COUNT 5
static const int  k_speedMult[] = {1, 2, 3, 4, 1};
static const bool k_speedRV[]   = {false, false, false, false, true};

/* Reseta todos os cheats de um player (Buffer6 trigger) */
static void Cmd_ResetAllCheats(int player) {
    g_game.cmdSpeedMult[player]      = 1;
    g_game.cmdSpeedRV[player]        = false;
    g_game.cmdMirror[player]         = false;
    g_game.cmdRandomStep[player]     = false;
    g_game.cmdRandomVelocity[player] = false;
    g_game.cmdVanish[player]         = false;
    g_game.cmdNonStep[player]        = false;
    g_cmdSpeedIdx[player]            = 0;
    g_cmdBufCount[player]            = 0;
    g_cmdBuf9Count[player]           = 0;
    g_cmdBuf6Count[player]           = 0;
    g_cmdBuf6State[player]           = 0;
    Log_Print("CMD P%d: RESET todos os cheats\n", player + 1);
    Audio_Play(g_waveSoundIds[SND_2_1], false);
}

/* Empurra um botão nos buffers do player e verifica todas as sequências.
 * Retorna true se algum cheat foi detectado (para tocar o som de confirmação). */
static bool Cmd_Push(int player, PadButton btn) {
    bool cheatFired = false;

    /* ── Buffer5: velocidade + Vanish/NonStep ─────────────────────────── */
    if (g_cmdBufCount[player] < CMD_BUF_LEN) {
        g_cmdBuf[player][g_cmdBufCount[player]++] = btn;
    } else {
        memmove(g_cmdBuf[player], g_cmdBuf[player] + 1, (CMD_BUF_LEN - 1) * sizeof(PadButton));
        g_cmdBuf[player][CMD_BUF_LEN - 1] = btn;
    }
    if (g_cmdBufCount[player] >= CMD_BUF_LEN) {
        if (memcmp(g_cmdBuf[player], k_speedSeq, CMD_BUF_LEN * sizeof(PadButton)) == 0) {
            g_cmdBufCount[player]  = 0;
            g_cmdBuf9Count[player] = 0;
            g_cmdSpeedIdx[player]  = (g_cmdSpeedIdx[player] + 1) % CMD_SPEED_COUNT;
            g_game.cmdSpeedMult[player] = k_speedMult[g_cmdSpeedIdx[player]];
            g_game.cmdSpeedRV[player]   = k_speedRV[g_cmdSpeedIdx[player]];
            Log_Print("CMD P%d: Speed -> x%d%s (idx=%d)\n",
                      player + 1, g_game.cmdSpeedMult[player],
                      g_game.cmdSpeedRV[player] ? " RV" : "",
                      g_cmdSpeedIdx[player]);
            cheatFired = true;
        } else if (memcmp(g_cmdBuf[player], k_vanishSeq, CMD_BUF_LEN * sizeof(PadButton)) == 0) {
            g_cmdBufCount[player]  = 0;
            g_cmdBuf9Count[player] = 0;
            if (!g_game.cmdVanish[player]) {
                g_game.cmdVanish[player]   = true;
                g_game.cmdNonStep[player]  = false;
                Log_Print("CMD P%d: Vanish ON\n", player + 1);
            } else if (!g_game.cmdNonStep[player]) {
                g_game.cmdNonStep[player]  = true;
                Log_Print("CMD P%d: NonStep ON (Vanish+NonStep)\n", player + 1);
            } else {
                g_game.cmdVanish[player]   = false;
                g_game.cmdNonStep[player]  = false;
                Log_Print("CMD P%d: Vanish/NonStep OFF\n", player + 1);
            }
            cheatFired = true;
        }
    }

    /* ── Buffer9: cheats de 9 botões ─────────────────────────────────── */
    if (g_cmdBuf9Count[player] < CMD_BUF9_LEN) {
        g_cmdBuf9[player][g_cmdBuf9Count[player]++] = btn;
    } else {
        memmove(g_cmdBuf9[player], g_cmdBuf9[player] + 1, (CMD_BUF9_LEN - 1) * sizeof(PadButton));
        g_cmdBuf9[player][CMD_BUF9_LEN - 1] = btn;
    }
    if (g_cmdBuf9Count[player] >= CMD_BUF9_LEN) {
        for (int seq = 0; seq < 5; seq++) {
            if (memcmp(g_cmdBuf9[player], k_seq9[seq], CMD_BUF9_LEN * sizeof(PadButton)) == 0) {
                g_cmdBuf9Count[player] = 0;
                g_cmdBufCount[player]  = 0;
                switch (seq) {
                case 0: /* Random Velocity */
                    g_game.cmdRandomVelocity[player] = !g_game.cmdRandomVelocity[player];
                    Log_Print("CMD P%d: RandomVelocity %s\n", player+1,
                              g_game.cmdRandomVelocity[player] ? "ON" : "OFF");
                    break;
                case 1: /* Mirror */
                    g_game.cmdMirror[player] = !g_game.cmdMirror[player];
                    Log_Print("CMD P%d: Mirror %s\n", player+1,
                              g_game.cmdMirror[player] ? "ON" : "OFF");
                    break;
                case 2: /* Random Step */
                    g_game.cmdRandomStep[player] = !g_game.cmdRandomStep[player];
                    Log_Print("CMD P%d: RandomStep %s\n", player+1,
                              g_game.cmdRandomStep[player] ? "ON" : "OFF");
                    break;
                case 3: /* Freedom — só som, sem ícone */
                    Log_Print("CMD P%d: Freedom ativado\n", player+1);
                    break;
                case 4: /* Earthworm — só som, sem ícone */
                    Log_Print("CMD P%d: Earthworm ativado\n", player+1);
                    break;
                }
                cheatFired = true;
                break;
            }
        }
    }

    return cheatFired;
}

static int prevSongId = -1;
static int previewState = 0;
static int selectedState = 0;
static int g_cdTexIds[45];
bool g_cdLoaded = false;
static float g_songAnimPos = 0.0f;
static int g_songAnimCounter = 0;

static int g_carrosselFrame = 588;
static int g_carrosselDir = 0;
static int g_carrosselTarget = 588;
static bool g_carrosselIntro = true;
static int g_introFrame = 0;
static int g_pendingMove = 0;
static float g_previewDelay = 0.0f;

static const int g_slotFrameOffset[7] = { -48, -32, -16, 0, +16, +32, +48 };

// Modos 1 jogador: NORMAL(0), HARD(1), CRAZY(2), HALFDOUBLE(3), DOUBLE(4), NIGHTMARE(5)
// Modos 2 jogadores (P1+P2 juntos): NORMAL(0), HARD(1), CRAZY(2), BATTLE(3)
// BATTLE usa steps HARD e só aparece quando activePlayerMask == 0x3 (ambos ativos)
static const char* g_modeNames1P[6] = {"NORMAL","HARD","CRAZY","HALFDOUBLE","DOUBLE","NIGHTMARE"};
static const int   g_modeLayers1P[6] = {31, 32, 30, 27, 28, 8};
static const char* g_modeNames2P[4] = {"NORMAL","HARD","CRAZY","BATTLE"};
static const int   g_modeLayers2P[4] = {31, 32, 30, -1}; /* -1=BATTLE: layer buscado por nome */

/* Array dinâmico de layer indices — preenchido por rebuildModeList().
 * Para BATTLE, o layer do BATTLE.SPR é descoberto pelo nome no BGAPicture. */
static int g_modeLayersDyn[6];

static int g_modeCount = 6;    // número de modos ativos (6 em 1P/P2-solo, 4 em P1+P2)
static int g_modeDBIdx[6];     // indices correspondentes no SongDB
static int g_modeTileIdx[6];   // indices dos primeiros tiles SPR de cada modo
static int g_modeSongIndex[6]; // última posição de música lembrada por modo
static bool g_isBattleMode = false; // true quando slot selecionado é BATTLE (P1+P2)

static int g_selDispIdx = 0;          // indice de exibicao atual (0-5)
static bool g_modeAnimActive = false;
static int g_modeAnimFrame = 0;
static int g_modeAnimDir = 0;         // +1=UR, -1=UL
/* Modo e índice de música a usar na RENDERIZAÇÃO dos CDs.
 * Atualizado imediatamente ao pressionar UR/UL, para os CDs
 * já mostrarem o novo modo desde o início da animação dos boxes. */
static int g_displayModeDBIdx = 0;
static int g_displaySongIndex = 0;
#define MODE_ANIM_DURATION 15
#define MODE_LEFT_X   107.0f
#define MODE_CENTER_X 315.0f
#define MODE_RIGHT_X  537.0f
#define MODE_Y         90.0f

typedef struct {
    int frame;
    float x, sx, alpha;
} BoxKeyframe;

static const BoxKeyframe g_boxKF[7] = {
    {540, -235.0f, 0.50f, 0.0f},
    {556, -175.0f, 0.65f, 0.6f},
    {572, -110.0f, 0.80f, 1.0f},
    {588,    0.0f, 1.00f, 1.0f},
    {604,  110.0f, 0.80f, 1.0f},
    {620,  175.0f, 0.65f, 0.6f},
    {636,  235.0f, 0.50f, 0.0f},
};

static void EvalBoxFrame(float frame, float* outX, float* outSc, float* outAlpha) {
    if (frame <= 540.0f) { *outX = -235.0f; *outSc = 0.50f; *outAlpha = 0.0f; return; }
    if (frame >= 636.0f) { *outX =  235.0f; *outSc = 0.50f; *outAlpha = 0.0f; return; }
    for (int i = 0; i < 6; i++) {
        if (frame >= g_boxKF[i].frame && frame <= g_boxKF[i+1].frame) {
            float t = (frame - g_boxKF[i].frame) / (float)(g_boxKF[i+1].frame - g_boxKF[i].frame);
            *outX     = g_boxKF[i].x     + t * (g_boxKF[i+1].x     - g_boxKF[i].x);
            *outSc    = g_boxKF[i].sx    + t * (g_boxKF[i+1].sx    - g_boxKF[i].sx);
            *outAlpha = g_boxKF[i].alpha + t * (g_boxKF[i+1].alpha  - g_boxKF[i].alpha);
            return;
        }
    }
}

static int g_cdSongs[45][2] = {
    {108,109},{112,205},{212,301},{312,318},{320,321},
    {401,402},{403,404},{405,413},{503,505},{508,516},
    {701,703},{704,705},{707,711},{712,714},{719,720},
    {721,722},{730,733},{734,735},{736,801},{802,803},
    {804,805},{806,807},{808,809},{810,811},{812,813},
    {814,815},{816,817},{818,819},{820,911},{916,921},
    {101,104},{202,203},{204,212},{302,303},{305,306},
    {310,311},{312,414},{501,504},{507,517},{902,906},
    {913,915},{922,102},{821,822},{823,824},{825,826}
};

static int findCdForSong(int songId, int* outHalf) {
    for (int i = 0; i < 45; i++) {
        if (g_cdSongs[i][0] == songId) { *outHalf = 0; return i; }
        if (g_cdSongs[i][1] == songId) { *outHalf = 1; return i; }
    }
    return -1;
}

static void cacheModeTileIndices(void) {
    if (g_game.bgaPicCount <= 0) return;
    BGAPicture* pic = &g_game.bgaPics[0];
    for (int m = 0; m < g_modeCount; m++) {
        int layer = g_modeLayersDyn[m];
        if (layer >= 0 && layer < pic->layerCount) {
            g_modeTileIdx[m] = pic->layers[layer].sprTileStart;
        } else {
            g_modeTileIdx[m] = -1;
        }
    }
}

/* Encontra o índice de layer de um SPR pelo nome (busca substring, case-insensitive não
 * disponível em C puro — usa strstr; nomes no BGA são uppercase). Retorna -1 se não achar. */
static int findLayerByName(const char* keyword) {
    if (g_game.bgaPicCount <= 0) return -1;
    BGAPicture* pic = &g_game.bgaPics[0];
    for (int li = 0; li < pic->layerCount; li++) {
        if (strstr(pic->layers[li].filename, keyword))
            return li;
    }
    return -1;
}

/* Reconstrói a lista de modos disponíveis de acordo com activePlayerMask.
 * BATTLE aparece apenas quando P1+P2 estão ambos ativos (mask == 0x3).
 * P2 sozinho (0x2) ou P1 sozinho (0x1) = 6 modos normais. */
static void rebuildModeList(void) {
    SongDB* db = &g_game.songDB;
    if (g_game.activePlayerMask == 0x3) {
        /* P1 + P2 juntos: 4 modos. BATTLE usa steps HARD */
        g_modeCount = 4;
        for (int m = 0; m < 4; m++) {
            const char* dbName = (m == 3) ? "HARD" : g_modeNames2P[m];
            g_modeDBIdx[m] = Song_FindMode(db, dbName);
            if (g_modeDBIdx[m] < 0) g_modeDBIdx[m] = 0;
            /* Layer dinâmico: BATTLE busca BATTLE.SPR pelo nome */
            if (m == 3) {
                int bl = findLayerByName("battle"); /* bga.c converte filenames para minúsculo */
                g_modeLayersDyn[m] = (bl >= 0) ? bl : g_modeLayers2P[1]; /* fallback=HARD */
            } else {
                g_modeLayersDyn[m] = g_modeLayers2P[m];
            }
        }
        g_isBattleMode = false; /* atualizado quando slot 3 é selecionado */
    } else {
        /* 1P solo (P1 ou P2 separado): 6 modos com HD/Double/Nightmare */
        g_modeCount = 6;
        for (int m = 0; m < 6; m++) {
            g_modeDBIdx[m] = Song_FindMode(db, g_modeNames1P[m]);
            if (g_modeDBIdx[m] < 0) g_modeDBIdx[m] = 0;
            g_modeLayersDyn[m] = g_modeLayers1P[m];
        }
        g_isBattleMode = false;
    }
    /* Se o selDispIdx atual está fora do novo g_modeCount, volta para 0 */
    if (g_selDispIdx >= g_modeCount) {
        g_selDispIdx = 0;
        g_game.selectedModeIndex = g_modeDBIdx[0];
        g_displayModeDBIdx = g_modeDBIdx[0];
    } else {
        g_game.selectedModeIndex = g_modeDBIdx[g_selDispIdx];
        g_displayModeDBIdx = g_modeDBIdx[g_selDispIdx];
    }
    cacheModeTileIndices();
}

static void loadCdTextures(void) {
    if (g_cdLoaded) return;
    char datPath[MAX_PATH];
    snprintf(datPath, sizeof(datPath), "%s\\BGA\\90.DAT", g_game.currentDirectory);
    if (!RES_Open(datPath)) { g_cdLoaded = true; return; }
    for (int i = 0; i < 45; i++) {
        char name[32];
        snprintf(name, sizeof(name), "CD%02d.png", i + 1);
        g_cdTexIds[i] = loadTextureFromRES(name);
    }
    RES_Close();
    g_cdLoaded = true;
}

void SongSelect_ResetCreditIndices(void) {
    memset(g_modeSongIndex, 0, sizeof(g_modeSongIndex));
}

void SongSelect_Reset(void) {
    prevSongId = -1;
    previewState = 0;
    selectedState = 0;
    g_isBattleMode = false;
    g_game.isBattleMode = false;
    g_game.selectedSongIndex = 0;
    g_game.songSelectHighlighted = 0;
    g_game.previewSongId = -1;
    memset(g_modeSongIndex, 0, sizeof(g_modeSongIndex));

    g_cmdBufCount[0]  = 0; g_cmdBufCount[1]  = 0;
    g_cmdBuf9Count[0] = 0; g_cmdBuf9Count[1] = 0;
    g_cmdBuf6Count[0] = 0; g_cmdBuf6Count[1] = 0;
    g_cmdBuf6State[0] = 0; g_cmdBuf6State[1] = 0;
    g_cmdSpeedIdx[0]  = 0; g_cmdSpeedIdx[1]  = 0;

    /* Inicializa lista de modos de acordo com activePlayerMask */
    g_modeCount = 6;
    g_selDispIdx = 0;
    g_displaySongIndex = 0;
    g_modeAnimActive = false;
    g_modeAnimFrame = 0;
    g_modeAnimDir = 0;
    rebuildModeList();

    g_songAnimCounter = 0;
    g_songAnimPos = 0.0f;
    g_carrosselFrame = 588;
    g_carrosselDir = 0;
    g_carrosselTarget = 588;
    g_carrosselIntro = true;
    g_introFrame = 0;
    g_previewDelay = 0.0f;
    loadCdTextures();
    cacheModeTileIndices();

    /* Carrega font/arrows (00.DAT) se ainda não estiverem carregados.
     * Necessário para os ícones de Command (X, R, M, V, NS) na HUD do SongSelect. */
    if (g_fontArrow541 < 0) {
        char datPath[MAX_PATH];
        snprintf(datPath, sizeof(datPath), "%s\\BGA\\00.DAT", g_game.currentDirectory);
        Resource_LoadFontAndArrows(datPath);
    }
}

void SongSelect_ResetIntro(void) {
    prevSongId = -1;
    previewState = 0;
    selectedState = 0;
    g_songAnimCounter = 0;
    g_songAnimPos = 0.0f;
    g_carrosselFrame = 588;
    g_carrosselDir = 0;
    g_carrosselTarget = 588;
    g_carrosselIntro = true;
    g_introFrame = 0;
    g_previewDelay = 0.0f;
    loadCdTextures();
    cacheModeTileIndices();
    /* Resource_ClearBGA (chamado antes pelo Game_ChangeState) reseta g_fontArrow541.
     * Recarregar 00.DAT para que os ícones de Command continuem aparecendo. */
    if (g_fontArrow541 < 0) {
        char datPath[MAX_PATH];
        snprintf(datPath, sizeof(datPath), "%s\\BGA\\00.DAT", g_game.currentDirectory);
        Resource_LoadFontAndArrows(datPath);
    }
}

static void stopPreview(void) {
    if (previewState) {
        BGM_Stop();
        previewState = 0;
    }
}

static void playPreview(int songId) {
    stopPreview();
    if (songId < 0) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\AUDIO\\D%d.AUD", g_game.currentDirectory, songId);
    if (BGM_LoadAUDDirect(path)) {
        BGM_Play(false);
        previewState = 1;
    }
    prevSongId = songId;
}

void Gamestate_UpdateSongSelect(float dt) {
    if (g_game.state == STATE_SONG_SELECT_B) {
        float a = g_game.globalColorA - dt;
        if (a <= 0.0f) {
            Render_SetGlobalColor(0, 0, 0, 0);
            if (g_game.stateFrame > 60) {
                g_game.state = STATE_SONG_SELECT;
                g_game.stateFrame = 0;
                previewState = 0;
                prevSongId = -1;
                selectedState = 0;
            }
        } else {
            Render_SetGlobalColor(0, 0, 0, a);
        }
        return;
    }

    SongDB* db = &g_game.songDB;

    /* ── Jogadores entrando (CN de quem não está ativo ainda) ──────────── */
    if ((g_game.activePlayerMask & 0x2) == 0 && Input_IsPadHit(1, PAD_C)) {
        g_game.activePlayerMask |= 0x2;
        Log_Print("SONGSEL: P2 entrou (mask=0x%x)\n", g_game.activePlayerMask);
        Audio_Play(g_waveSoundIds[SND_3_2], false);
        rebuildModeList();
        selectedState = 0;
        stopPreview();
    }
    if ((g_game.activePlayerMask & 0x1) == 0 && Input_IsPadHit(0, PAD_C)) {
        g_game.activePlayerMask |= 0x1;
        Log_Print("SONGSEL: P1 entrou (mask=0x%x)\n", g_game.activePlayerMask);
        Audio_Play(g_waveSoundIds[SND_3_2], false);
        rebuildModeList();
        selectedState = 0;
        stopPreview();
    }

    /* ── Captura todos os botões para detecção de Commands (por jogador) ── */
    {
        static const PadButton all5[] = {PAD_UL, PAD_UR, PAD_C, PAD_DL, PAD_DR};
        for (int _p = 0; _p < 2; _p++) {
            if (!(g_game.activePlayerMask & (1 << _p))) continue;

            bool dlHit = Input_IsPadHit(_p, PAD_DL);
            bool drHit = Input_IsPadHit(_p, PAD_DR);

            /* Buffer6: pares DL+DR (simultâneo ou DL→DR sequencial) × 3 = Reset
             * Máquina de estados por player:
             *   state 0 (idle) + DL pressed → state 1
             *   state 1 + DR pressed → par detectado, count++, state 0
             *   DL+DR no mesmo frame   → par detectado imediatamente, state 0 */
            if (dlHit && drHit) {
                /* simultâneo: par imediato */
                g_cmdBuf6State[_p] = 0;
                g_cmdBuf6Count[_p]++;
                Log_Print("CMD P%d: DL+DR par (simult.) #%d\n", _p+1, g_cmdBuf6Count[_p]);
                if (g_cmdBuf6Count[_p] >= 3) Cmd_ResetAllCheats(_p);
            } else {
                if (g_cmdBuf6State[_p] == 0 && dlHit) {
                    g_cmdBuf6State[_p] = 1; /* DL recebido, aguarda DR */
                } else if (g_cmdBuf6State[_p] == 1 && drHit) {
                    g_cmdBuf6State[_p] = 0;
                    g_cmdBuf6Count[_p]++;
                    Log_Print("CMD P%d: DL→DR par (seq.) #%d\n", _p+1, g_cmdBuf6Count[_p]);
                    if (g_cmdBuf6Count[_p] >= 3) Cmd_ResetAllCheats(_p);
                }
            }

            /* Buffer5 + Buffer9: todos os 5 botões individualmente */
            for (int _ci = 0; _ci < 5; _ci++) {
                if (Input_IsPadHit(_p, all5[_ci])) {
                    if (Cmd_Push(_p, all5[_ci]))
                        Audio_Play(g_waveSoundIds[SND_2_1], false);
                }
            }
        }
    }

    /* Qualquer player ativo pode trocar de modo (UR=próximo, UL=anterior) */
    {
        bool urHit = ((g_game.activePlayerMask & 0x1) && Input_IsPadHit(0, PAD_UR))
                  || ((g_game.activePlayerMask & 0x2) && Input_IsPadHit(1, PAD_UR));
        bool ulHit = ((g_game.activePlayerMask & 0x1) && Input_IsPadHit(0, PAD_UL))
                  || ((g_game.activePlayerMask & 0x2) && Input_IsPadHit(1, PAD_UL));

        if (urHit) {
            if (g_modeAnimActive) {
                g_modeSongIndex[g_selDispIdx] = g_game.selectedSongIndex;
                g_selDispIdx = (g_selDispIdx + g_modeAnimDir + g_modeCount) % g_modeCount;
                g_game.selectedModeIndex = g_modeDBIdx[g_selDispIdx];
                g_game.selectedSongIndex = g_modeSongIndex[g_selDispIdx];
                g_modeAnimActive = false;
            }
            g_modeSongIndex[g_selDispIdx] = g_game.selectedSongIndex;
            {
                int nextDispIdx = (g_selDispIdx + 1) % g_modeCount;
                g_displayModeDBIdx = g_modeDBIdx[nextDispIdx];
                g_displaySongIndex = g_modeSongIndex[nextDispIdx];
            }
            loadCdTextures(); cacheModeTileIndices();
            Audio_Play(g_waveSoundIds[SND_3_2], false);
            selectedState = 0; stopPreview();
            g_modeAnimActive = true; g_modeAnimFrame = 0; g_modeAnimDir = 1;
            prevSongId = -1; g_songAnimCounter = 0;
            g_carrosselIntro = true; g_introFrame = 0;
            g_carrosselFrame = 588; g_carrosselDir = 0; g_carrosselTarget = 588;
        }

        if (ulHit) {
            if (g_modeAnimActive) {
                g_modeSongIndex[g_selDispIdx] = g_game.selectedSongIndex;
                g_selDispIdx = (g_selDispIdx + g_modeAnimDir + g_modeCount) % g_modeCount;
                g_game.selectedModeIndex = g_modeDBIdx[g_selDispIdx];
                g_game.selectedSongIndex = g_modeSongIndex[g_selDispIdx];
                g_modeAnimActive = false;
            }
            g_modeSongIndex[g_selDispIdx] = g_game.selectedSongIndex;
            {
                int nextDispIdx = (g_selDispIdx - 1 + g_modeCount) % g_modeCount;
                g_displayModeDBIdx = g_modeDBIdx[nextDispIdx];
                g_displaySongIndex = g_modeSongIndex[nextDispIdx];
            }
            loadCdTextures(); cacheModeTileIndices();
            Audio_Play(g_waveSoundIds[SND_3_2], false);
            selectedState = 0; stopPreview();
            g_modeAnimActive = true; g_modeAnimFrame = 0; g_modeAnimDir = -1;
            prevSongId = -1; g_songAnimCounter = 0;
            g_carrosselIntro = true; g_introFrame = 0;
            g_carrosselFrame = 588; g_carrosselDir = 0; g_carrosselTarget = 588;
        }
    }

    // Atualiza animacao dos modos
    if (g_modeAnimActive) {
        g_modeAnimFrame++;
        if (g_modeAnimFrame >= MODE_ANIM_DURATION) {
            g_modeAnimActive = false;
            g_selDispIdx = (g_selDispIdx + g_modeAnimDir + g_modeCount) % g_modeCount;
            g_game.selectedModeIndex = g_modeDBIdx[g_selDispIdx];
            /* Restaura última posição de música do modo destino */
            g_game.selectedSongIndex = g_modeSongIndex[g_selDispIdx];
            /* Sincroniza display vars (devem já estar iguais, mas por segurança) */
            g_displayModeDBIdx = g_game.selectedModeIndex;
            g_displaySongIndex = g_game.selectedSongIndex;
            g_pendingMove = 0;
            g_carrosselDir = 0;
            g_carrosselFrame = 588;
            g_carrosselTarget = 588;
            /* Atualiza flag de BATTLE */
            g_isBattleMode = (g_game.activePlayerMask == 0x3) && (g_selDispIdx == 3);
        }
    }

    SongMode* mode = &db->modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;
    if (songCount == 0) return;

    if (g_carrosselIntro) {
        g_introFrame++;
        if (g_introFrame >= 50) {
            g_carrosselIntro = false;
        }
        /* Não retorna: DL/DR abaixo cancela a intro e move o carrossel normalmente */
    }

    /* Qualquer player ativo navega músicas (DR=próxima, DL=anterior) */
    {
        bool drHit = ((g_game.activePlayerMask & 0x1) && Input_IsPadHit(0, PAD_DR))
                  || ((g_game.activePlayerMask & 0x2) && Input_IsPadHit(1, PAD_DR));
        bool dlHit = ((g_game.activePlayerMask & 0x1) && Input_IsPadHit(0, PAD_DL))
                  || ((g_game.activePlayerMask & 0x2) && Input_IsPadHit(1, PAD_DL));

        if (drHit) {
            Audio_Play(g_waveSoundIds[SND_3_2], false);
            selectedState = 0; stopPreview(); prevSongId = -1;
            g_carrosselIntro = false;
            if (g_carrosselDir != 0) {
                g_carrosselFrame = g_carrosselTarget;
                if (g_pendingMove != 0) {
                    g_game.selectedSongIndex += g_pendingMove;
                    if (g_game.selectedSongIndex >= songCount) g_game.selectedSongIndex = 0;
                    if (g_game.selectedSongIndex < 0) g_game.selectedSongIndex = songCount - 1;
                    g_pendingMove = 0;
                }
                g_carrosselFrame = 588; g_carrosselDir = 0;
            }
            g_pendingMove = +1;
            g_carrosselTarget = g_carrosselFrame - 16;
            g_carrosselDir = -1;
            g_previewDelay = 1.0f;
        }

        if (dlHit) {
            Audio_Play(g_waveSoundIds[SND_3_2], false);
            selectedState = 0; stopPreview(); prevSongId = -1;
            g_carrosselIntro = false;
            if (g_carrosselDir != 0) {
                g_carrosselFrame = g_carrosselTarget;
                if (g_pendingMove != 0) {
                    g_game.selectedSongIndex += g_pendingMove;
                    if (g_game.selectedSongIndex >= songCount) g_game.selectedSongIndex = 0;
                    if (g_game.selectedSongIndex < 0) g_game.selectedSongIndex = songCount - 1;
                    g_pendingMove = 0;
                }
                g_carrosselFrame = 588; g_carrosselDir = 0;
            }
            g_pendingMove = -1;
            g_carrosselTarget = g_carrosselFrame + 16;
            g_carrosselDir = +1;
            g_previewDelay = 1.0f;
        }
    }

    // Avanca o frame do carrossel — velocidade 1 = mais lento
    if (g_carrosselDir != 0) {
        g_carrosselFrame += g_carrosselDir * 1;

        if ((g_carrosselDir == +1 && g_carrosselFrame >= g_carrosselTarget) ||
            (g_carrosselDir == -1 && g_carrosselFrame <= g_carrosselTarget)) {
            // Aplicar movimento pendente ao índice só quando a animação terminar
            if (g_pendingMove != 0) {
                g_game.selectedSongIndex += g_pendingMove;
                if (g_game.selectedSongIndex >= songCount) g_game.selectedSongIndex = 0;
                if (g_game.selectedSongIndex < 0) g_game.selectedSongIndex = songCount - 1;
                g_pendingMove = 0;
            }
            // Reset base frame para centro
            g_carrosselFrame = 588;
            g_carrosselTarget = 588;
            g_carrosselDir = 0;
        }
    }

    int songId = mode->songIds[g_game.selectedSongIndex];
    /* Preview audio: não inicia enquanto animação de modo ou intro do carrossel estiver ativa.
     * Ghidra (SongSelect_RenderCarouselMove @ 0x4086c0): no original, Box2 (layer 0x18) só
     * aparece quando local_30 == 20.0 (carrossel de DL/DR no pico). Ao pressionar UL/UR,
     * g_nCarouselSpeed=0 → local_30=0.0 → Box2 nunca aparece e audio para.
     * Aqui replicamos: bloqueamos o preview durante troca de modo e intro do carrossel. */
    if (!g_modeAnimActive && !g_carrosselIntro) {
        if (g_previewDelay > 0.0f) {
            g_previewDelay -= dt;
            if (g_previewDelay <= 0.0f) {
                if (songId != prevSongId) {
                    playPreview(songId);
                }
            }
        } else {
            if (songId != prevSongId)
                playPreview(songId);
        }
    }

    /* Confirmação de música: CN de qualquer jogador ativo */
    {
        bool cnHit = (Input_IsPadHit(0, PAD_C) && (g_game.activePlayerMask & 0x1)) ||
                     (Input_IsPadHit(1, PAD_C) && (g_game.activePlayerMask & 0x2));
        if (cnHit) {
            if (!selectedState)
                Audio_Play(g_waveSoundIds[SND_3_2], false);
            if (selectedState) {
                Audio_Play(g_waveSoundIds[SND_4_2], false);
                stopPreview();
                g_game.selectedDifficulty = mode->difficulties[g_game.selectedSongIndex];
                selectedState = 0;
                g_game.isBattleMode = g_isBattleMode; /* propaga flag BATTLE para gameplay */
                Loading_Enter(songId);
            } else {
                selectedState = 1;
            }
        }
    }
}

// Frames dos keyframes do BGA para cada posicao (extraidos do 099.DAT)
#define FRAME_LEFT    14   // Event 1: x=0,   y=0,   hx=0,   sx=1.0
#define FRAME_CENTER  74   // Event 3: x=210, y=35,  hx=110, sx=1.3
#define FRAME_RIGHT   134  // Event 5: x=430, y=0,   hx=110, sx=1.0
#define FRAME_OFF     194  // Event 7: x=210, y=-130, hx=110, sx=1.0

void Gamestate_RenderSongSelect(void) {
    if (g_game.state != STATE_SONG_SELECT && g_game.state != STATE_SONG_SELECT_B)
        return;
    if (g_game.state != STATE_SONG_SELECT) return;

    if (g_game.bgaPicCount > 0) {
        int f = g_game.bgaFrame;
        BGA_SetEventLayer(0, f % 60, 0);

        BGA_SetEventLayer(0, 0, 0x1a);
        BGA_SetEventLayer(0, 0, 0x24);

        // Mantém camadas de fundo/overlay; os boxes serão desenhados
        // intercalados com os CDs abaixo para garantir a ordem desejada.
    }

    SongDB* db = &g_game.songDB;
    /* Durante animação de modo, usa vars de display (atualizadas imediatamente
     * no press de UR/UL) para que os CDs já mostrem o modo destino. */
    int renderModeIdx = g_modeAnimActive ? g_displayModeDBIdx : g_game.selectedModeIndex;
    int renderSongIdx = g_modeAnimActive ? g_displaySongIndex : g_game.selectedSongIndex;
    SongMode* mode = &db->modes[renderModeIdx];
    int songCount = mode->songCount;
    if (songCount == 0) return;

    // Renderiza os 7 slots do carrossel em ordem de distância do centro,
    // garantindo que os discos mais distantes fiquem atrás e os mais próximos fiquem por cima.
    typedef struct {
        int slotIndex;
        float screenX;
        float bsc;
        float balpha;
        int texId;
        int cdHalf;
        float tw;
        float th;
        int bgaSlotFrame;
        float introXOff;
        float cdYOff;   /* Y offset por posicao: far=10, mid=5, center=0 */
    } SlotRender;
    /* Y offset proporcional ao bx do slot: max 4px nas posições left-2/right-2 (bx=±175),
     * interpola continuamente durante animação — sem jump ao final do carrossel. */
    static const float kCdYOffScale = 4.0f / 175.0f;

    SlotRender slots[7];
    int slotCount = 0;

    for (int si = 0; si < 7; si++) {
        float slotFrame;
        float introXOff = 0.0f;

        if (g_carrosselIntro) {
            /* Cascata de entrada — segue SongSelect_RenderBoxesStatic (00408290):
             * todos os BCDs entram simultaneamente da direita a 106px/frame,
             * mas cada posição começa a uma distância diferente (incremento de 636):
             *   far-left(556): 640px → chega ao fim em ~6 frames
             *   left(572):    1276px → ~12 frames
             *   center(588):  1912px → ~18 frames
             *   right(604):   2548px → ~24 frames
             *   far-right(620): 3184px → ~30 frames
             * Valor 0x6a=106 = velocidade original exata. */
            int rawFrame = 588 + g_slotFrameOffset[si];
            float cascadeStart;
            if      (rawFrame <= 556) cascadeStart = 640.0f;
            else if (rawFrame <= 572) cascadeStart = 1276.0f;
            else if (rawFrame <= 588) cascadeStart = 1912.0f;
            else if (rawFrame <= 604) cascadeStart = 2548.0f;
            else                      cascadeStart = 3184.0f;
            float remaining = cascadeStart - 106.0f * (float)g_introFrame;
            introXOff = (remaining > 0.0f) ? remaining : 0.0f;
            slotFrame = (float)rawFrame;
        } else {
            slotFrame = (float)(g_carrosselFrame + g_slotFrameOffset[si]);
        }
        float bx, bsc, balpha;
        EvalBoxFrame(slotFrame, &bx, &bsc, &balpha);
        if (balpha < 0.01f) continue;

        float screenX = 320.0f + bx;
        int slotOffset = si - 3;
        int idx = (renderSongIdx + slotOffset + songCount) % songCount;
        int sid = mode->songIds[idx];
        int si2 = Song_FindByID(db, sid);

        int cdHalf = 0;
        int cdIdx = (si2 >= 0) ? findCdForSong(sid, &cdHalf) : -1;
        int texId = (cdIdx >= 0) ? g_cdTexIds[cdIdx] : -1;

        if (slotCount < 7) {
            int bgaSlotFrame = (int)slotFrame;
            if (bgaSlotFrame < 540) bgaSlotFrame = 540;
            if (bgaSlotFrame > 636) bgaSlotFrame = 636;
            slots[slotCount].slotIndex = si;
            slots[slotCount].screenX = screenX + introXOff;
            slots[slotCount].bsc = bsc;
            slots[slotCount].balpha = balpha;
            slots[slotCount].texId = texId;
            slots[slotCount].cdHalf = cdHalf;
            slots[slotCount].tw = 256.0f * bsc;
            slots[slotCount].th = 128.0f * bsc;
            slots[slotCount].bgaSlotFrame = bgaSlotFrame;
            slots[slotCount].introXOff = introXOff;
            slots[slotCount].cdYOff = fabsf(bx) * kCdYOffScale;
            slotCount++;
        }
    }

    /* Ordena pela posição de REPOUSO (sem introXOff), maior distância primeiro.
     * Para mesma distância, esquerda antes de direita — segue a ordem do Ghidra:
     * far-left, far-right, left, right, center.
     * Importante: durante a cascata screenX inclui introXOff (posição atual),
     * mas a profundidade deve ser determinada pela posição final de cada slot. */
    for (int i = 0; i < slotCount - 1; i++) {
        for (int j = i + 1; j < slotCount; j++) {
            float restI = slots[i].screenX - slots[i].introXOff;
            float restJ = slots[j].screenX - slots[j].introXOff;
            float distI = fabsf(restI - 320.0f);
            float distJ = fabsf(restJ - 320.0f);
            bool swap = false;
            if (distI < distJ) {
                swap = true;
            } else if (distI == distJ) {
                /* Mesma distância: esquerda (< 320) antes de direita */
                if (restI > 320.0f && restJ < 320.0f)
                    swap = true;
            }
            if (swap) {
                SlotRender tmp = slots[i];
                slots[i] = slots[j];
                slots[j] = tmp;
            }
        }
    }

    /* Loop único de renderização — segue SongSelect_RenderBoxesStatic (00408290):
     * para cada slot, CD primeiro (atrás), depois Box (na frente).
     * Ordem dos slots: far-left, far-right, left, right, center.
     *
     * Estado estático (g_carrosselDir==0, !g_carrosselIntro):
     *   Layers dedicadas por posição (não-compactadas), frame=0:
     *     556 → 0x0b(11) far-left, 572 → 0x11(17) left,
     *     588 → 0x17(23) center,   604 → 0x14(20) right,
     *     620 → 0x0e(14) far-right
     * Estado animando: todos usam layer 0x0b com frame 540-636.
     */
    bool bgaIsAnimating = (g_carrosselDir != 0) || g_carrosselIntro;

    for (int i = 0; i < slotCount; i++) {
        SlotRender* slot = &slots[i];

        /* --- CD art (atrás, igual ao glPushMatrix inner do Ghidra) --- */
        if (slot->texId >= 0) {
            float cdScaleX, cdScaleY, cdOffX, cdOffY;
            if (selectedState && slot->slotIndex == 3 && !bgaIsAnimating) {
                cdScaleX = 1.1f; cdScaleY = 1.1f; cdOffX = 0.0f; cdOffY = -6.0f;
            } else {
                cdScaleX = 1.00f; cdScaleY = 0.95f; cdOffX = 0.0f; cdOffY = -1.0f;
            }
            float sw = slot->tw * cdScaleX;
            float sh = slot->th * cdScaleY;
            float u1 = 0.0f;
            float v1 = (1.0f - (float)slot->cdHalf) * 128.0f;
            float u2 = 256.0f;
            float v2 = v1 + 128.0f;
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            Texture_DrawUV(slot->texId,
                slot->screenX - sw * 0.5f + cdOffX,
                240.0f - sh * 0.5f - 10.0f + cdOffY + slot->cdYOff,
                sw, sh * 1.575f,
                u1, v1, u2, v2,
                1.0f, 1.0f, 1.0f, slot->balpha);
        }

        /* --- Box frame (na frente, BGA_SetEventLayer) --- */
        if (bgaIsAnimating) {
            /* Carousel em movimento: layer 0x0b, frame interpolado */
            if (g_carrosselIntro && slot->introXOff != 0.0f) {
                glPushMatrix();
                glTranslatef(slot->introXOff, 0.0f, 0.0f);
                BGA_SetEventLayer(0, slot->bgaSlotFrame, 0x0b);
                glPopMatrix();
            } else {
                BGA_SetEventLayer(0, slot->bgaSlotFrame, 0x0b);
            }
        } else {
            /* Estático: layer dedicada, frame=0 */
            int staticLayer = -1;
            switch (slot->bgaSlotFrame) {
                case 556: staticLayer = 0x0b; break; /* far-left  */
                case 572: staticLayer = 0x11; break; /* left      */
                case 588: staticLayer = 0x17; break; /* center    */
                case 604: staticLayer = 0x14; break; /* right     */
                case 620: staticLayer = 0x0e; break; /* far-right */
            }
            if (staticLayer >= 0) {
                if (selectedState && slot->slotIndex == 3) {
                    glPushMatrix();
                    glTranslatef(320.0f, 240.0f, 0.0f);
                    glScalef(1.1f, 1.1f, 1.0f);
                    glTranslatef(-320.0f, -240.0f, 0.0f);
                    BGA_SetEventLayer(0, 0, staticLayer);
                    glPopMatrix();
                } else {
                    BGA_SetEventLayer(0, 0, staticLayer);
                }
            }
        }
    }

    // Desenha os sprites dos modos (esquerda, centro, direita)
    // g_modeLayersDyn já contém os layer indices corretos (incl. BATTLE.SPR para slot 3)
    if (g_game.bgaPicCount > 0) {
        int leftIdx  = (g_selDispIdx - 1 + g_modeCount) % g_modeCount;
        int centIdx  = g_selDispIdx;
        int rightIdx = (g_selDispIdx + 1) % g_modeCount;

        if (g_modeAnimActive) {
            float t = g_modeAnimFrame / (float)MODE_ANIM_DURATION;
            int oldLeft   = (g_selDispIdx - 1 + g_modeCount) % g_modeCount;
            int oldCenter = g_selDispIdx;
            int oldRight  = (g_selDispIdx + 1) % g_modeCount;
            if (g_modeAnimDir == 1) {
                int newRight = (g_selDispIdx + 2) % g_modeCount;
                BGA_SetEventLayer(0, (int)Math_Lerp(420.0f, 434.0f, t), g_modeLayersDyn[oldLeft]);
                BGA_SetEventLayer(0, (int)Math_Lerp(74.0f, 60.0f, t),   g_modeLayersDyn[oldCenter]);
                BGA_SetEventLayer(0, (int)Math_Lerp(134.0f, 120.0f, t), g_modeLayersDyn[oldRight]);
                BGA_SetEventLayer(0, (int)Math_Lerp(194.0f, 180.0f, t), g_modeLayersDyn[newRight]);
            } else {
                int newLeft = (g_selDispIdx - 2 + g_modeCount) % g_modeCount;
                BGA_SetEventLayer(0, (int)Math_Lerp(180.0f, 194.0f, t), g_modeLayersDyn[oldRight]);
                BGA_SetEventLayer(0, (int)Math_Lerp(120.0f, 134.0f, t), g_modeLayersDyn[oldCenter]);
                BGA_SetEventLayer(0, (int)Math_Lerp(60.0f, 74.0f, t),   g_modeLayersDyn[oldLeft]);
                BGA_SetEventLayer(0, (int)Math_Lerp(434.0f, 420.0f, t), g_modeLayersDyn[newLeft]);
            }
        } else {
            BGA_SetEventLayer(0, FRAME_LEFT,   g_modeLayersDyn[leftIdx]);
            BGA_SetEventLayer(0, FRAME_CENTER, g_modeLayersDyn[centIdx]);
            BGA_SetEventLayer(0, FRAME_RIGHT,  g_modeLayersDyn[rightIdx]);
        }
    }

    // box2.spr glow pulsante — só mostra quando preview está ativo E fora de animação de modo/intro.
    // Ghidra: Box2 (0x18) aparece apenas quando carrossel DL/DR chegou ao pico (local_30==20.0).
    // Durante UL/UR (troca de modo), local_30=0.0 → Box2 nunca renderizado no original.
    if (g_game.bgaPicCount > 0 && previewState && !g_modeAnimActive && !g_carrosselIntro) {
        if (selectedState) {
            glPushMatrix();
            glTranslatef(320.0f, 240.0f, 0.0f);
            glScalef(1.1f, 1.1f, 1.0f);
            glTranslatef(-320.0f, -240.0f, 0.0f);
            BGA_SetEventLayer(0, g_game.bgaFrame % 55, 0x18);
            glPopMatrix();
        } else {
            BGA_SetEventLayer(0, g_game.bgaFrame % 55, 0x18);
        }
    }

    if (previewState) {
        char buf[64];
        snprintf(buf, sizeof(buf), "D%d.AUD", prevSongId);
        Font_DrawStringCentered(320, 410, buf, 0.5f, 1.0f, 0.5f, 1.0f);
    }

    if (selectedState) {
        Font_DrawStringCentered(320, 380, "SELECTED", 0.0f, 1.0f, 0.0f, 1.0f);
    }

    /* ── Ícones de Command na HUD do SongSelect ────────────────────────────
     * Velocidade (Y=184):
     *   accel1=36, accel2=7, accel3=8, accel4=9, raccel(RV)=12  (ARROW541.SP2)
     *   Quando Random Velocity ativo: exibe raccel(12) independente do speedMult.
     * Modificadores (Y=216,248,280,312): RandomStep, Mirror, Vanish, NonStep
     *   Inativo: _randm=34, _mirrr=35, _vanis=37, _nnstp=38
     *   Ativo:    random=10,  mirror=11,  vanish=13,  nonstp=14
     * P1 lado esquerdo (hx = 18 + sw/2), P2 lado direito (hx = 640-18-sw/2).
     */
    if (g_fontArrow541 >= 0) {
        /* Tabela de offsets: OFF e ON para [RandomStep, Mirror, Vanish, NonStep] */
        static const int   kModOff_OFF[4] = { 34, 35, 37, 38 }; /* _randm,_mirrr,_vanis,_nnstp */
        static const int   kModOff_ON[4]  = { 10, 11, 13, 14 }; /* random,mirror,vanish,nonstp  */
        static const float kModY[4]       = { 216.0f, 248.0f, 280.0f, 312.0f };

        for (int _p = 0; _p < 2; _p++) {
            if (!(g_game.activePlayerMask & (1 << _p))) continue;

            /* ── Velocidade ──────────────────────────────────────────────── */
            int speedOff;
            if (g_game.cmdRandomVelocity[_p]) {
                speedOff = 12; /* raccel: Random Velocity ativo */
            } else {
                speedOff = 36; /* accel1 (x1 ou RV) */
                if (!g_game.cmdSpeedRV[_p]) {
                    if      (g_game.cmdSpeedMult[_p] >= 4) speedOff = 9;
                    else if (g_game.cmdSpeedMult[_p] >= 3) speedOff = 8;
                    else if (g_game.cmdSpeedMult[_p] >= 2) speedOff = 7;
                }
            }
            int speedIdx = g_fontArrow541 + speedOff;
            if (speedIdx < g_game.sprTileCount) {
                float sw = (float)g_game.sprTiles[speedIdx].srcW;
                float sh = (float)g_game.sprTiles[speedIdx].srcH;
                float hx = (_p == 0) ? (18.0f + sw/2.0f) : (640.0f - 18.0f - sw/2.0f);
                Sprite_DrawTileUV(speedIdx, hx, 184.0f + sh/2.0f, sw, sh, 1.0f);
            }

            /* ── Modificadores: R, M, V, NS ──────────────────────────────── */
            bool modActive[4] = {
                g_game.cmdRandomStep[_p],
                g_game.cmdMirror[_p],
                g_game.cmdVanish[_p],
                g_game.cmdNonStep[_p]
            };
            for (int di = 0; di < 4; di++) {
                int tileOff = modActive[di] ? kModOff_ON[di] : kModOff_OFF[di];
                int didx = g_fontArrow541 + tileOff;
                if (didx < g_game.sprTileCount) {
                    float sw = (float)g_game.sprTiles[didx].srcW;
                    float sh = (float)g_game.sprTiles[didx].srcH;
                    float hx = (_p == 0) ? (18.0f + sw/2.0f) : (640.0f - 18.0f - sw/2.0f);
                    Sprite_DrawTileUV(didx, hx, kModY[di] + sh/2.0f, sw, sh, 1.0f);
                }
            }
        }
    }
}