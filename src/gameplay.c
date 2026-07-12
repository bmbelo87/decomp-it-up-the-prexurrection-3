#include "pumpy.h"
#include "vsl.h"

#define MAX_PANELS 10
#define PANEL_SIZE 30
#define P1_CENTER_X 160
#define P2_CENTER_X 480

#define MISS_WINDOW 0.25f

#define JUDGE_PERFECT  0.055f
#define JUDGE_GREAT    0.110f
#define JUDGE_GOOD     0.165f
#define JUDGE_BAD      0.220f

/* ── Lifebar — valores exatos do GameInit (Ghidra) ───────────────────────
 * DAT_00da2324 = 500    (vida inicial P1)
 * _DAT_00da2328 = 500   (speed inicial, modo normal)
 * DAT_00da2260 = 200    (speed mínimo)
 * DAT_00da22c0 = 1000   (speed máximo)
 * DAT_00da225c = -700   (penalidade de speed em MISS; /2 em BAD)
 * Danger threshold: vida < 180 → barra vermelha
 * Stage break: missCombo > 50 → game over imediato */
#define LIFE_INITIAL        500
#define LIFE_DANGER         90
#define LIFE_SPEED_INIT     500
#define LIFE_SPEED_MIN      200
#define LIFE_SPEED_MAX      1000
#define LIFE_SPEED_PENALTY  (-700)
#define STAGE_BREAK_MISSES  50
/* Escala visual: barra cheia = 252 pixels (original), mapeada como 252.0f */
#define LIFE_FULL_DISPLAY   252.0f

/* BASE_ROW_SPACING: no original, o espaçamento por row = 60.0 / beatSplit.
 * beatSplit=2 → 30 px/row; beatSplit=4 → 15 px/row.
 * Confirmado via Ghidra: fórmula original y = 376 - scrollSpeed*beatPos*(1/1000),
 * onde beatPos += 60.0/beatSplit por row, e scrollSpeed=1000 no x1.
 * Calculado dinamicamente como g_baseRowSpacing = 60.0f / g_baseBeatSplit. */
/* #define BASE_ROW_SPACING 18.0f */
#define MAX_VISIBLE_ROWS 512

typedef enum {
    JT_NONE = 0,
    JT_PERFECT,
    JT_GREAT,
    JT_GOOD,
    JT_BAD,
    JT_MISS
} JudgeType;

static const char* g_judgeNames[] = { "", "Perfect!", "Great", "Good", "Bad", "Miss" };
static const int g_judgeSpriteIndices[] = { -1, 7, 8, 9, 10, 11 }; // n1, n2, n3, n4, perfec, great_
static const float g_judgeColors[6][3] = {
    {0,0,0}, {0.6f,1.0f,1.0f}, {0.6f,1.0f,0.6f}, {1.0f,1.0f,0.2f}, {1.0f,0.4f,0.4f}, {1.0f,0.5f,1.0f}
};

typedef struct {
    int rowIndex;
    bool judged;
    JudgeType judgment;
    double hitTime;
} NoteHit;

static StepSong g_playSong;
static int g_chartIdx;
StepChart* g_chart;
static bool g_songLoaded;

static double g_songTime;
static double g_secondsPerRow;
static double g_totalSongSeconds;
static double g_chartDelay;
static int g_baseBeatSplit;
static double g_baseBpm;
static double* g_visualRow = NULL;
static int g_visualRowCount = 0;
static double g_maxSongTime;
static int g_stagnantFrames;
static uint32_t g_lastPosMs;
static int g_lastNoteRow;
static bool g_hasAudio;
static bool g_autoplay;
static bool g_autoPanel[10]; // per-panel autoplay: 0-4 P1, 5-9 P2
static float g_scrollSpeedX; // current (interpolated) speed
static float g_scrollSpeedTarget; // target speed from keypress

/* Aplica variação de vida para o julgamento dado (fórmulas exatas do Ghidra). */
static void applyLife(int player, JudgeType jt)
{
    int* life  = &g_game.stats.life[player];
    int* speed = &g_game.stats.lifeSpeed[player];
    switch (jt) {
        case JT_PERFECT:
            *life  += (*speed * 12) / 1000;
            *speed += 20;
            if (*speed > LIFE_SPEED_MAX) *speed = LIFE_SPEED_MAX;
            break;
        case JT_GREAT:
            *life  += (*speed * 10) / 1000;
            *speed += 16;
            if (*speed > LIFE_SPEED_MAX) *speed = LIFE_SPEED_MAX;
            break;
        case JT_GOOD:
            /* Sem mudança na vida — apenas quebra o missCombo */
            break;
        case JT_BAD:
            *life  -= 50;
            if (*life < 0) *life = 0;
            *speed += LIFE_SPEED_PENALTY / 2;   /* -= 350 */
            if (*speed < LIFE_SPEED_MIN) *speed = LIFE_SPEED_MIN;
            break;
        case JT_MISS:
            *life   = (*life * 3) / 4 - 20;
            if (*life < 0) *life = 0;
            *speed += LIFE_SPEED_PENALTY;        /* -= 700 */
            if (*speed < LIFE_SPEED_MIN) *speed = LIFE_SPEED_MIN;
            break;
        default:
            break;
    }
}

static double getSegmentSpr(int seg)
{
    return 60.0 / ((double)g_chart->segments[seg].bpm * (double)g_chart->segments[seg].beatSplit);
}

static double getSegmentDelay(int seg)
{
    return g_chart->segments[seg].delay / 100.0;
}

static double getRowTime(int ri)
{
    if (!g_chart) return ri * g_secondsPerRow + g_chartDelay;
    // Find which segment this row belongs to
    for (int s = g_chart->segmentCount - 1; s >= 0; s--)
    {
        if (ri >= (int)g_chart->segments[s].rowStart)
        {
            double accum = 0;
            for (int ps = 0; ps < s; ps++)
                accum += g_chart->segments[ps].rowCount * getSegmentSpr(ps) + getSegmentDelay(ps);
            accum += getSegmentDelay(s);
            return accum + (ri - g_chart->segments[s].rowStart) * getSegmentSpr(s);
        }
    }
    return ri * g_secondsPerRow + g_chartDelay;
}

// Compute block number and line-within-block for a given row
static int getBlockInfo(int ri, int* outLine) {
    if (!g_chart || ri < 0) { if (outLine) *outLine = 1; return 1; }
    // Walk through rows counting block boundaries
    // A block boundary occurs at: every rowsPerBlock rows, OR at each split (segment start mid-block)
    int block = 1;
    int lastSplitRow = 0; // row where current block started
    int row = 0;
    int segIdx = 0;
    
    // Collect all block boundary rows
    #define MAX_BOUNDARIES 2000
    static int boundaries[MAX_BOUNDARIES];
    int bc = 0;
    
    for (int s = 0; s < g_chart->segmentCount && bc < MAX_BOUNDARIES; s++)
    {
        int rpBlock = g_chart->segments[s].beatPerMeasure * g_chart->segments[s].beatSplit;
        int segStart = g_chart->segments[s].rowStart;
        int segEnd = segStart + g_chart->segments[s].rowCount;
        
        // If this segment starts mid-block (after a split), that's a block boundary
        if (s > 0 && bc < MAX_BOUNDARIES)
            boundaries[bc++] = segStart; // split creates new block
        
        // Normal block boundaries within this segment
        int firstBlockRow = segStart;
        if (s > 0) {
            int prevRPB = g_chart->segments[s-1].beatPerMeasure * g_chart->segments[s-1].beatSplit;
            firstBlockRow = ((segStart / prevRPB) + 1) * prevRPB;
            if (firstBlockRow < segStart) firstBlockRow = segStart;
        }
        for (int br = firstBlockRow + rpBlock; br < segEnd && bc < MAX_BOUNDARIES; br += rpBlock)
            boundaries[bc++] = br;
    }
    
    // Find which boundary segment our row is in
    int blockStart = 0;
    for (int b = 0; b < bc; b++) {
        if (ri < boundaries[b]) break;
        blockStart = boundaries[b];
        block++;
    }
    if (outLine) *outLine = ri - blockStart + 1;
    return block;
}

static int getRowAtTime(double t)
{
    if (!g_chart) return 0;
    double accum = 0;
    for (int s = 0; s < g_chart->segmentCount; s++)
    {
        double segSpr = getSegmentSpr(s);
        double segDelay = getSegmentDelay(s);
        double segDur = g_chart->segments[s].rowCount * segSpr + segDelay;
        if (t < accum + segDur || s == g_chart->segmentCount - 1)
        {
            double tInSeg = t - accum - segDelay;
            if (tInSeg < 0) tInSeg = 0;
            int ri = g_chart->segments[s].rowStart + (int)(tInSeg / segSpr);
            if (ri < 0) ri = 0;
            if (ri >= (int)g_chart->rowCount) ri = g_chart->rowCount - 1;
            return ri;
        }
        accum += segDur;
    }
    return (int)g_chart->rowCount - 1;
}

static double getRowAtTimeFloat(double t)
{
    if (!g_chart) return t / g_secondsPerRow;
    double accum = 0;
    for (int s = 0; s < g_chart->segmentCount; s++)
    {
        double segSpr = getSegmentSpr(s);
        double segDelay = getSegmentDelay(s);
        double segDur = g_chart->segments[s].rowCount * segSpr + segDelay;
        if (t < accum + segDur || s == g_chart->segmentCount - 1)
        {
            double tInSeg = t - accum - segDelay;
            if (tInSeg < 0) tInSeg = 0;
            double ri = (double)g_chart->segments[s].rowStart + tInSeg / segSpr;
            if (ri < 0) ri = 0;
            if (ri >= (double)g_chart->rowCount) ri = (double)g_chart->rowCount - 1;
            return ri;
        }
        accum += segDur;
    }
    return (double)g_chart->rowCount - 1;
}

static NoteHit g_noteHits[2][MAX_PANELS][2048];
static int g_noteHitCount[2][MAX_PANELS];
static int g_nextNoteRow[2][MAX_PANELS];

// Hold tracking: which rows have active hold heads per player/panel
static int g_holdRows[2][MAX_PANELS]; // row index of active hold (-1 = none)

static float g_judgeDisplayTimer[2];
static JudgeType g_judgeDisplayType[2];
static int g_judgeDisplayCombo[2];
static int g_judgeFrame[2]; // frame counter 25->0 for judge animation
static int g_hitTimer[2][MAX_PANELS]; // hit flash animation timer (p1)
static int g_glowTimer[2][MAX_PANELS];    // glow aditivo: apenas PERFECT/GREAT
static int g_p1FlashTimer[2][MAX_PANELS]; // tile p1: zoom+fade ao pressionar

// Maquina de estados da nota
static int g_noteState[2][MAX_PANELS]; // 0=normal, 1=exploding, 2=dead
static int g_noteExplodeRow[2][MAX_PANELS]; // row index for clearing when dead
static int g_noteExplodeFrame[2][MAX_PANELS]; // explosion frame counter 0..15
static int g_blindTimer[2];
static int g_prevBlindRow;
static int g_lastPerfectRow[2][MAX_PANELS];

// Pop-up de score (catch effect)
#define MAX_POPUPS 32
static struct {
    int score;       // valor do score (+1000, +500)
    int combo;       // combo atual
    float y;         // posicao Y atual (sobe)
    float alpha;     // fade out
    bool active;
    int player;
} g_popups[MAX_POPUPS];

// Linhas com multiplas setas aguardando julgamento (ate BAD window expirar)
#define MAX_PENDING 32
static struct {
    int row;
    double deadline;
    int totalMask;    // bits 0-4: setas que EXISTEM na linha
    int hitMask;      // bits 0-4: setas que ja foram pressionadas
    float worstDiff;
    bool active;
} g_pending[MAX_PENDING];
static int g_pendingCount;

// Conta tiles consecutivos de um SPR pelo nome (ex: 01.spr_0, 01.spr_1 = 2)
static int sprTileCount(int startIdx) {
    if (startIdx < 0 || startIdx >= g_game.sprTileCount) return 0;
    const char* name = g_game.sprTiles[startIdx].name;
    const char* us = strrchr(name, '_');
    if (!us) return 1;
    char prefix[64];
    int plen = (int)(us - name);
    if (plen > 63) plen = 63;
    memcpy(prefix, name, plen);
    prefix[plen] = '\0';
    int c = 0;
    while (startIdx + c < g_game.sprTileCount) {
        char exp[64];
        snprintf(exp, sizeof(exp), "%s_%d", prefix, c);
        if (stricmp(g_game.sprTiles[startIdx + c].name, exp) != 0) break;
        c++;
    }
    return c;
}

static bool loadChartForSong(int songId, int diffTier, const char* modeName)
{
    g_songLoaded = false;
    g_chart = NULL;

    char stxPath[MAX_PATH];
    snprintf(stxPath, sizeof(stxPath), "%s\\STEP\\%d.STX",
             g_game.currentDirectory, songId);

    Log_Print("GP: loading '%s' (song %d, mode=%s)\n", stxPath, songId, modeName ? modeName : "?");

    if (!Step_LoadSong(stxPath, &g_playSong))
    {
        Log_Print("GP: FAILED to load STX\n");
        return false;
    }

    g_chartIdx = Step_SelectChart(modeName, 1);
    if (g_chartIdx < 0 || g_chartIdx >= g_playSong.chartCount)
    {
        Log_Print("GP: chart index %d out of range, using 0\n", g_chartIdx);
        g_chartIdx = 0;
    }

    g_chart = &g_playSong.charts[g_chartIdx];
    g_songTime = 0.0;
    g_maxSongTime = 0.0;
    g_stagnantFrames = 0;
    g_lastPosMs = 0;
    // Encontrar último row com nota
    g_lastNoteRow = -1;
    if (g_chart) {
        for (int ri = (int)g_chart->rowCount - 1; ri >= 0; ri--) {
            StepRow* r = &g_chart->rows[ri];
            if (r->half1.dl || r->half1.ul || r->half1.cn || r->half1.ur || r->half1.dr ||
                r->half2.dl || r->half2.ul || r->half2.cn || r->half2.ur || r->half2.dr) {
                g_lastNoteRow = ri;
                break;
            }
        }
    }
    Log_Print("GP: last note row = %d / %u\n", g_lastNoteRow, g_chart ? g_chart->rowCount : 0);
    g_chartDelay = g_chart->delay / 100.0;
    float bpm = g_chart->bpm;
    if (bpm <= 0) bpm = 120.0f;
    uint32_t subdiv = g_chart->beatSplit;
    if (subdiv == 0) subdiv = 4;

    g_secondsPerRow = 60.0 / ((double)bpm * (double)subdiv);
    // Total time using segments
    {
        double total = 0;
        for (int s = 0; s < g_chart->segmentCount; s++)
        {
            double segSpr = 60.0 / ((double)g_chart->segments[s].bpm * (double)g_chart->segments[s].beatSplit);
            total += g_chart->segments[s].rowCount * segSpr + (g_chart->segments[s].delay / 100.0);
        }
        g_totalSongSeconds = total;
    }
    g_autoplay = g_game.input.autoplay;

    /* Aplica multiplicador de velocidade do Command.
     * P2-only: usa cmdSpeedMult[1]; caso contrário usa cmdSpeedMult[0]. */
    int _speedPlayer = (g_game.activePlayerMask == 0x2) ? 1 : 0;
    float initSpeed = (g_game.cmdSpeedMult[_speedPlayer] >= 1) ? (float)g_game.cmdSpeedMult[_speedPlayer] : 1.0f;
    g_scrollSpeedX      = initSpeed;
    g_scrollSpeedTarget = initSpeed;

    memset(g_noteHits, 0, sizeof(g_noteHits));
    memset(g_noteHitCount, 0, sizeof(g_noteHitCount));
    memset(g_nextNoteRow, 0, sizeof(g_nextNoteRow));
    for (int p = 0; p < 2; p++)
        for (int pan = 0; pan < MAX_PANELS; pan++)
            g_holdRows[p][pan] = -1;

    g_baseBeatSplit = g_chart->segments[0].beatSplit;
    g_baseBpm = g_chart->segments[0].bpm;

    // Pre-compute normalized visual rows (BPM-based, ignoring beatSplit)
    g_visualRowCount = (int)g_chart->rowCount;
    g_visualRow = (double*)realloc(g_visualRow, g_visualRowCount * sizeof(double));
    if (g_visualRow) {
        double vRow = 0;
        for (int s = 0; s < g_chart->segmentCount; s++) {
            double beatRatio = (double)g_baseBeatSplit / (double)g_chart->segments[s].beatSplit;
            for (uint32_t r = g_chart->segments[s].rowStart; r < g_chart->segments[s].rowStart + g_chart->segments[s].rowCount; r++) {
                if ((int)r < g_visualRowCount) g_visualRow[r] = vRow;
                vRow += beatRatio;
            }
        }
    }

    Log_Print("GP: chart %d: BPM=%.1f subdiv=%d rows=%d panels=%d time=%.1fs spR=%.4f segments=%d baseSpr=%.4f\n",
        g_chartIdx, bpm, subdiv, g_chart->rowCount, g_chart->panelCount, g_totalSongSeconds, g_secondsPerRow, g_chart->segmentCount, 60.0/(g_baseBpm*(double)g_baseBeatSplit));
    g_songLoaded = true;

    g_chart->totalNotes = 0;
    for (uint32_t r = 0; r < g_chart->rowCount; r++)
    {
        StepRow* row = &g_chart->rows[r];
        uint8_t* p1 = (uint8_t*)&row->half1;
        uint8_t* p2 = (uint8_t*)&row->half2;
        for (int i = 0; i < 5; i++)
        {
            if (p1[i] != 0) g_chart->totalNotes++;
            if (p2[i] != 0) g_chart->totalNotes++;
        }
    }
    return true;
}

static void loadChart(void)
{
    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int songId = mode->songIds[g_game.songSelectHighlighted];
    int diffTier = mode->difficulties[g_game.songSelectHighlighted];
    loadChartForSong(songId, diffTier, mode->name);
}

static JudgeType evaluateTiming(double diff)
{
    double ad = diff < 0 ? -diff : diff;
    if (ad <= JUDGE_PERFECT) return JT_PERFECT;
    if (ad <= JUDGE_GREAT)   return JT_GREAT;
    if (ad <= JUDGE_GOOD)    return JT_GOOD;
    if (ad <= JUDGE_BAD)     return JT_BAD;
    return JT_MISS;
}

static void popupCreate(int player, int score, int combo, float y)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_popups[i].active) {
            g_popups[i].player = player;
            g_popups[i].score = score;
            g_popups[i].combo = combo;
            g_popups[i].y = y;
            g_popups[i].alpha = 1.0f;
            g_popups[i].active = true;
            break;
        }
    }
}

static int getPanelForButton(PadButton btn)
{
    switch (btn)
    {
        case PAD_DL: return 0;
        case PAD_UL: return 1;
        case PAD_C:  return 2;
        case PAD_UR: return 3;
        case PAD_DR: return 4;
        default: return -1;
    }
}

static uint8_t getPanelValue(StepRow* row, int panel, int player)
{
    StepHalf* h = (player == 0) ? &row->half1 : &row->half2;
    switch (panel) {
        case 0: return h->dl;
        case 1: return h->ul;
        case 2: return h->cn;
        case 3: return h->ur;
        case 4: return h->dr;
    }
    return 0;
}

// HalfDouble note reading: 6 posicoes mapeadas para colunas 2-7 do STX (10-col)
// pos 0..2 = half1 cols 2..4 (CN, UR, DR)
// pos 3..5 = half2 cols 0..2 (DL, UL, CN)
static uint8_t getNoteHD(StepRow* row, int pos)
{
    StepHalf* h = (pos < 3) ? &row->half1 : &row->half2;
    int pd = (pos < 3) ? (pos + 2) : (pos - 3);
    switch (pd) {
        case 0: return h->dl;
        case 1: return h->ul;
        case 2: return h->cn;
        case 3: return h->ur;
        case 4: return h->dr;
        default: return 0;
    }
}

static void clearHDPanel(StepRow* row, int pan)
{
    switch (pan) { case 0: row->half1.cn = 0; break; case 1: row->half1.ur = 0; break; case 2: row->half1.dr = 0; break; case 3: row->half2.dl = 0; break; case 4: row->half2.ul = 0; break; case 5: row->half2.cn = 0; break; }
}

static void clearPanel(StepRow* row, int pan, int player)
{
    StepHalf* hh = (player == 0) ? &row->half1 : &row->half2;
    switch (pan) { case 0: hh->dl = 0; break; case 1: hh->ul = 0; break; case 2: hh->cn = 0; break; case 3: hh->ur = 0; break; case 4: hh->dr = 0; break; }
}

// HD: panels 0=P1_CN(left/center), 1=P1_UR(up), 2=P1_DR(down)
// panels 3=P2_DL(keypad1), 4=P2_UL(keypad7), 5=P2_CN(keypad5)
static PadButton hdPanelBtn(int pan)
{
    static const PadButton hdBtn[6] = { PAD_C, PAD_UR, PAD_DR, PAD_DL, PAD_UL, PAD_C };
    return (pan >= 0 && pan < 6) ? hdBtn[pan] : PAD_C;
}

static int hdPanelPlayer(int pan)
{
    return (pan < 3) ? 0 : 1;
}

static bool isHDMode(void)
{
    return (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
            strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
}

static bool isDNMode(void)
{
    return (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
           (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
            strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
}

static int dnPanelBtn(int pan)
{
    static const PadButton dnBtn[10] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR, PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
    return (pan >= 0 && pan < 10) ? dnBtn[pan] : PAD_C;
}

static int dnPanelPlayer(int pan)
{
    return (pan < 5) ? 0 : 1;
}

static uint8_t getDNPanelValue(StepRow* row, int pan)
{
    return (pan < 5) ? getPanelValue(row, pan, 0) : getPanelValue(row, pan - 5, 1);
}

static void clearDNPanel(StepRow* row, int pan)
{
    if (pan < 5) clearPanel(row, pan, 0);
    else clearPanel(row, pan - 5, 1);
}

// Processa o julgamento final de uma linha
static void processRowJudgment(int player, int row, JudgeType jt) {
    int receptorY = 38;
    bool hdCheck = isHDMode();
    bool dnJdg = isDNMode();
    int jdPanels = hdCheck ? 6 : (dnJdg ? 10 : 5);
    // So Perfect/Great consomem a nota (ela some). Good/Bad/Miss passam reto.
    if (jt == JT_PERFECT || jt == JT_GREAT) {
        StepRow* r = &g_chart->rows[row];
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(r, pan) : (dnJdg ? getDNPanelValue(r, pan) : getPanelValue(r, pan, player));
            if (pv) {
                g_noteState[player][pan] = 1;
                g_noteExplodeRow[player][pan] = row;
                g_noteExplodeFrame[player][pan] = 0;
                if (hdCheck) clearHDPanel(r, pan);
                else if (dnJdg) clearDNPanel(r, pan);
                else clearPanel(r, pan, player);
            }
            g_lastPerfectRow[player][pan] = row;
        }
        /* clearPanel já zerou os painéis do player atual.
         * memset destruiria half do outro player em modo 2P. */
        /* if (!hdCheck && !dnJdg)
            memset(r, 0, sizeof(StepRow)); */
    }
    g_judgeDisplayType[player] = jt;
    g_judgeDisplayTimer[player] = 0.6f;
    g_judgeFrame[player] = (jt == JT_GREAT || jt == JT_PERFECT) ? 40 : 25;
    int sc = 0, cb = g_game.stats.combo[player];
    switch (jt) {
        case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
        case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
        case JT_GOOD:    sc = 0;    break;
        case JT_BAD:     sc = 0;    cb = 0; break;
        default: break;
    }
if (sc > 0) popupCreate(player, sc, cb, 178.0f); // Y=80 (Ghidra) + 98 offset = Y=178 (game reality)
    switch (jt) {
        case JT_PERFECT:
        case JT_GREAT:
            g_game.stats.combo[player]++;
            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
            g_game.stats.missCombo[player] = 0;
            break;
        case JT_GOOD:
            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
            g_game.stats.missCombo[player] = 0;
            break;
        case JT_BAD:
            g_game.stats.combo[player] = 0;
            g_judgeDisplayCombo[player] = 0;
            g_game.stats.missCombo[player] = 0;
            break;
        default: break;
    }
    switch (jt) {
        case JT_PERFECT:
            g_game.stats.perfectCount[player]++;
            g_game.stats.score[player] += 1000;
            if (g_game.stats.combo[player] > 3)
                g_game.stats.score[player] += 1000;
            break;
        case JT_GREAT:
            g_game.stats.greatCount[player]++;
            g_game.stats.score[player] += 500;
            if (g_game.stats.combo[player] > 3)
                g_game.stats.score[player] += 1000;
            break;
        case JT_GOOD:
            g_game.stats.goodCount[player]++;
            break;
        case JT_BAD:
            g_game.stats.badCount[player]++;
            break;
        default: break;
    }
    applyLife(player, jt);
    if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
        g_game.stats.maxCombo[player] = g_game.stats.combo[player];
}

// Processa linhas multi-seta pendentes
static void processPendingRows(int player) {
    double now = g_songTime;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!g_pending[i].active) continue;
        if (g_pending[i].deadline > now && g_pending[i].hitMask != g_pending[i].totalMask) continue;
        g_pending[i].active = false;
        g_pendingCount--;
        JudgeType jt;
        bool hdCheck = isHDMode();
        bool dnPr = isDNMode();
        int jdPanels = hdCheck ? 6 : (dnPr ? 10 : 5);
        if (g_pending[i].hitMask == g_pending[i].totalMask)
            jt = evaluateTiming(g_pending[i].worstDiff);
        else
            jt = JT_MISS;
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(&g_chart->rows[g_pending[i].row], pan) : (dnPr ? getDNPanelValue(&g_chart->rows[g_pending[i].row], pan) : getPanelValue(&g_chart->rows[g_pending[i].row], pan, player));
            if (!pv) continue;
            int hdPly = hdCheck ? hdPanelPlayer(pan) : (dnPr ? dnPanelPlayer(pan) : player);
            PadButton holdBtn = hdCheck ? hdPanelBtn(pan) : (dnPr ? dnPanelBtn(pan) : 0);
            if ((pv == NT_HOLD_H || pv == NT_HOLD_B || pv == NT_HOLD_T) && g_holdRows[hdPly][pan] < 0) {
                if (hdCheck || dnPr) {
                    if (!Input_IsPadDown(hdPly, holdBtn))
                        jt = JT_MISS;
                    else {
                        g_holdRows[hdPly][pan] = g_pending[i].row;
                        if (hdCheck) clearHDPanel(&g_chart->rows[g_pending[i].row], pan);
                        else clearDNPanel(&g_chart->rows[g_pending[i].row], pan);
                    }
                } else {
                    static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                    if (!Input_IsPadDown(player, btnMap[pan]))
                        jt = JT_MISS;
                    else {
                        g_holdRows[player][pan] = g_pending[i].row;
                        clearPanel(&g_chart->rows[g_pending[i].row], pan, player);
                    }
                }
            }
        }
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(&g_chart->rows[g_pending[i].row], pan) : (dnPr ? getDNPanelValue(&g_chart->rows[g_pending[i].row], pan) : getPanelValue(&g_chart->rows[g_pending[i].row], pan, player));
            if (pv == NT_HOLD_H)
                g_holdRows[player][pan] = g_pending[i].row;
        }
        for (int pan = 0; pan < jdPanels; pan++) {
            uint8_t pv = hdCheck ? getNoteHD(&g_chart->rows[g_pending[i].row], pan) : (dnPr ? getDNPanelValue(&g_chart->rows[g_pending[i].row], pan) : getPanelValue(&g_chart->rows[g_pending[i].row], pan, player));
            if (pv && pv != NT_HOLD_B && pv != NT_HOLD_T)
                g_nextNoteRow[player][pan] = g_pending[i].row + 1;
        }
        processRowJudgment(player, g_pending[i].row, jt);
    }
}

static int rowAnyNote(StepRow* row, int player)
{
    StepHalf* h = (player == 0) ? &row->half1 : &row->half2;
    return h->dl || h->ul || h->cn || h->ur || h->dr;
}

static int rowIsHold(int player, int panel, StepRow* row)
{
    uint8_t v = getPanelValue(row, panel, player);
    return (v == NT_HOLD_H || v == NT_HOLD_B || v == NT_HOLD_T);
}

static int rowAnyNoteHD(StepRow* row)
{
    return row->half1.cn || row->half1.ur || row->half1.dr ||
           row->half2.dl || row->half2.ul || row->half2.cn;
}

static void processInput(int player)
{
    if (!g_songLoaded) return;

    bool isHD = isHDMode();
    bool isDN = isDNMode();
    int panCount = isHD ? 6 : (isDN ? 10 : 5);
    int numBtns = isHD ? 6 : (isDN ? 10 : PAD_BUTTONS_PER_PLAYER);

    for (int b = 0; b < numBtns; b++)
    {
        int panel;
        PadButton btn;
        int usePlayer;
        if (isHD) {
            panel = b;
            btn = hdPanelBtn(panel);
            usePlayer = hdPanelPlayer(panel);
        } else if (isDN) {
            panel = b;
            btn = dnPanelBtn(panel);
            usePlayer = dnPanelPlayer(panel);
        } else {
            panel = getPanelForButton((PadButton)b);
            btn = (PadButton)b;
            usePlayer = player;
        }
        if (panel < 0) continue;
        if (!Input_IsPadHit(usePlayer, btn)) continue;
        g_hitTimer[player][panel] = 17;
        g_p1FlashTimer[player][panel] = 15; // inicia zoom+fade do tile p1

        double bestDiff = 999;
        int bestRow = -1;

        for (int ri = g_nextNoteRow[player][panel]; ri < (int)g_chart->rowCount; ri++)
        {
            uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (isDN ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, player));
            if (!val || val == NT_HOLD_B || val == NT_HOLD_T) continue;

            double rowTime = getRowTime(ri);
            double diff = g_songTime - rowTime;
            if (diff < -JUDGE_BAD) break;
            if (diff > JUDGE_BAD) { g_nextNoteRow[player][panel] = ri + 1; continue; }

            double ad = diff < 0 ? -diff : diff;
            if (ad < bestDiff) { bestDiff = ad; bestRow = ri; }
        }

        if (bestRow < 0) continue;

        Log_Print("TAPHIT: p=%d pan=%d row=%d diff=%.3f\n", player, panel, bestRow, bestDiff);

        int arrowsInRow = 0;
        for (int pan = 0; pan < panCount; pan++)
            if (isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player))) arrowsInRow++;

        if (arrowsInRow > 1) {
            g_nextNoteRow[player][panel] = bestRow + 1;
            int slot = -1;
            for (int i = 0; i < MAX_PENDING; i++)
                if (g_pending[i].active && g_pending[i].row == bestRow) { slot = i; break; }
            if (slot < 0)
                for (int i = 0; i < MAX_PENDING; i++)
                    if (!g_pending[i].active) { slot = i; break; }
            if (slot >= 0) {
                if (!g_pending[slot].active) {
                    g_pending[slot].active = true;
                    g_pending[slot].row = bestRow;
                    g_pending[slot].deadline = g_songTime + JUDGE_BAD;
                    g_pending[slot].totalMask = 0;
                    g_pending[slot].hitMask = 0;
                    for (int pan = 0; pan < panCount; pan++) {
                        uint8_t pv = isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player));
                        if (pv && pv < NT_HOLD_H)
                            g_pending[slot].totalMask |= (1 << pan);
                    }
                    g_pendingCount++;
                }
                g_pending[slot].hitMask |= (1 << panel);
                g_pending[slot].worstDiff = bestDiff;

                if ((g_pending[slot].hitMask & g_pending[slot].totalMask) == g_pending[slot].totalMask) {
                    g_pending[slot].active = false;
                    g_pendingCount--;
                    JudgeType pjt = evaluateTiming(g_pending[slot].worstDiff);
                    for (int pan = 0; pan < panCount; pan++) {
                        uint8_t pv = isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player));
                        if (!pv) continue;
                        if ((pv == NT_HOLD_H || pv == NT_HOLD_B || pv == NT_HOLD_T) && g_holdRows[player][pan] < 0) {
                            if (isHD) {
                                if (!Input_IsPadDown(hdPanelPlayer(pan), hdPanelBtn(pan)))
                                    pjt = JT_MISS;
                                else {
                                    g_holdRows[player][pan] = bestRow;
                                    clearHDPanel(&g_chart->rows[bestRow], pan);
                                }
                            } else if (isDN) {
                                if (!Input_IsPadDown(dnPanelPlayer(pan), dnPanelBtn(pan)))
                                    pjt = JT_MISS;
                                else {
                                    g_holdRows[player][pan] = bestRow;
                                    clearDNPanel(&g_chart->rows[bestRow], pan);
                                }
                            } else {
                                static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                                if (!Input_IsPadDown(player, btnMap[pan]))
                                    pjt = JT_MISS;
                                else {
                                    g_holdRows[player][pan] = bestRow;
                                    clearPanel(&g_chart->rows[bestRow], pan, player);
                                }
                            }
                        }
                    }
                    if (pjt == JT_PERFECT || pjt == JT_GREAT) {
                        for (int pan = 0; pan < panCount; pan++)
                            if (isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player))) {
                                g_noteState[player][pan] = 1;
                                g_noteExplodeRow[player][pan] = bestRow;
                                g_noteExplodeFrame[player][pan] = 0;
                                g_glowTimer[player][pan] = 17; // glow aditivo P/G
                                if (isHD) clearHDPanel(&g_chart->rows[bestRow], pan);
                                else if (isDN) clearDNPanel(&g_chart->rows[bestRow], pan);
                                else clearPanel(&g_chart->rows[bestRow], pan, player);
                            }
                        /* clearPanel já zerou cada painel do player atual (half1 ou half2).
                         * memset destruiria dados do outro player em modo 2P. */
                        /* if (!isHD && !isDN)
                        memset(&g_chart->rows[bestRow], 0, sizeof(StepRow)); */
                    }
                    g_judgeDisplayType[player] = pjt;
                    g_judgeDisplayTimer[player] = 0.6f;
                    g_judgeFrame[player] = (pjt == JT_GREAT || pjt == JT_PERFECT) ? 40 : 25;
                    { int sc = 0, cb = g_game.stats.combo[player];
                      int receptorY = 38;
                      switch (pjt) {
                        case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
                        case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
                        default: break;
} if (sc > 0) popupCreate(player, sc, cb, 178.0f); }
                    switch (pjt) {
                        case JT_PERFECT: case JT_GREAT:
                            g_game.stats.combo[player]++;
                            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                            g_game.stats.missCombo[player] = 0;
                            if (pjt == JT_PERFECT) {
                                g_game.stats.perfectCount[player]++;
                                g_game.stats.score[player] += 1000;
                                if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                            } else {
                                g_game.stats.greatCount[player]++;
                                g_game.stats.score[player] += 500;
                                if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                            }
                            break;
                        case JT_GOOD:
                            g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                            g_game.stats.missCombo[player] = 0;
                            g_game.stats.goodCount[player]++;
                            break;
                        case JT_BAD:
                            g_game.stats.combo[player] = 0;
                            g_judgeDisplayCombo[player] = 0;
                            g_game.stats.missCombo[player] = 0;
                            g_game.stats.badCount[player]++;
                            break;
                        default: break;
                    }
                    applyLife(player, pjt);
                    if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
                        g_game.stats.maxCombo[player] = g_game.stats.combo[player];
                }
            }
            continue;
        }

        g_nextNoteRow[player][panel] = bestRow + 1;
        JudgeType jt = evaluateTiming(bestDiff);
        if (jt == JT_PERFECT || jt == JT_GREAT) {
            for (int pan = 0; pan < panCount; pan++)
                if (isHD ? getNoteHD(&g_chart->rows[bestRow], pan) : (isDN ? getDNPanelValue(&g_chart->rows[bestRow], pan) : getPanelValue(&g_chart->rows[bestRow], pan, player))) {
                    g_noteState[player][pan] = 1;
                    g_noteExplodeRow[player][pan] = bestRow;
                    g_noteExplodeFrame[player][pan] = 0;
                    g_glowTimer[player][pan] = 17; // glow aditivo P/G
                    if (isHD) clearHDPanel(&g_chart->rows[bestRow], pan);
                    else if (isDN) clearDNPanel(&g_chart->rows[bestRow], pan);
                    else clearPanel(&g_chart->rows[bestRow], pan, player);
                }
            /* clearPanel já zerou cada painel do player atual (half1 ou half2).
             * memset destruiria dados do outro player em modo 2P. */
            /* if (!isHD && !isDN)
                memset(&g_chart->rows[bestRow], 0, sizeof(StepRow)); */
        }
        g_judgeDisplayType[player] = jt;
        g_judgeDisplayTimer[player] = 0.6f;
        g_judgeFrame[player] = (jt == JT_GREAT || jt == JT_PERFECT) ? 40 : 25;
        { int sc = 0, cb = g_game.stats.combo[player];
          int receptorY = 38;
          switch (jt) {
            case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
            case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
            default: break;
} if (sc > 0) popupCreate(player, sc, cb, 178.0f); }
        switch (jt) {
            case JT_PERFECT: case JT_GREAT:
                g_game.stats.combo[player]++;
                g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                g_game.stats.missCombo[player] = 0;
                if (jt == JT_PERFECT) {
                    g_game.stats.perfectCount[player]++;
                    g_game.stats.score[player] += 1000;
                    if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                } else {
                    g_game.stats.greatCount[player]++;
                    g_game.stats.score[player] += 500;
                    if (g_game.stats.combo[player] > 3) g_game.stats.score[player] += 1000;
                }
                break;
            case JT_GOOD:
                g_judgeDisplayCombo[player] = g_game.stats.combo[player];
                g_game.stats.missCombo[player] = 0;
                g_game.stats.goodCount[player]++;
                break;
            case JT_BAD:
                g_game.stats.combo[player] = 0;
                g_judgeDisplayCombo[player] = 0;
                g_game.stats.missCombo[player] = 0;
                g_game.stats.badCount[player]++;
                break;
            default: break;
        }
        applyLife(player, jt);
        if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
            g_game.stats.maxCombo[player] = g_game.stats.combo[player];
    }
}

static bool anyAutoPanel(void)
{
    bool isHD = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                 strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
    bool dnAP = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
                 strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
    int pc = dnAP ? 10 : (isHD ? 6 : 5);
    for (int a = 0; a < pc; a++)
        if (g_autoPanel[a]) return true;
    return false;
}

static void processAutoplay(void)
{
    if (!g_songLoaded || !anyAutoPanel()) return;

    bool isHD = isHDMode();
    bool dnAP = isDNMode();
    int panCount = isHD ? 6 : (dnAP ? 10 : 5);

    int _ap0 = (isHD || dnAP) ? 0 : ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int _ap1 = (isHD || dnAP) ? 1 : ((g_game.activePlayerMask == 0x3) ? 2 : _ap0 + 1);
    for (int p = _ap0; p < _ap1; p++)
    {
        int hitRows[10], hitCount = 0;
        for (int panel = 0; panel < panCount; panel++)
        {
            if (!g_autoPanel[panel]) continue;

            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (!val) continue;
                if (val == NT_HOLD_B || val == NT_HOLD_T) continue;

                double rowTime = getRowTime(ri);
                double diff = g_songTime - rowTime;
                if (diff < -JUDGE_PERFECT) break;
                if (diff > JUDGE_PERFECT) { g_nextNoteRow[p][panel] = ri + 1; continue; }

                g_nextNoteRow[p][panel] = ri + 1;
                g_hitTimer[p][panel] = 17;
                hitRows[hitCount++] = ri;
                break;
            }
        }

        int dedupRows[10], dedupCount = 0;
        for (int i = 0; i < hitCount; i++)
        {
            int dup = 0;
            for (int d = 0; d < dedupCount; d++)
                if (dedupRows[d] == hitRows[i]) { dup = 1; break; }
            if (!dup) dedupRows[dedupCount++] = hitRows[i];
        }

        for (int i = 0; i < dedupCount; i++)
        {
            g_judgeDisplayType[p] = JT_PERFECT;
            g_judgeDisplayTimer[p] = 0.6f;
            g_judgeFrame[p] = 40;
            g_judgeDisplayCombo[p] = ++g_game.stats.combo[p];
            g_game.stats.missCombo[p] = 0;
            g_game.stats.score[p] += 1000;
            if (g_game.stats.combo[p] > 3)
                g_game.stats.score[p] += 1000;
            if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
                g_game.stats.maxCombo[p] = g_game.stats.combo[p];

            for (int panel = 0; panel < panCount; panel++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[hitRows[i]], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[hitRows[i]], panel) : getPanelValue(&g_chart->rows[hitRows[i]], panel, p));
                if (val == NT_HOLD_H)
                    g_holdRows[p][panel] = hitRows[i];
                if (val)
                    g_lastPerfectRow[p][panel] = hitRows[i];
            }

            for (int panel = 0; panel < panCount; panel++) {
                if (!g_autoPanel[panel]) continue;
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[hitRows[i]], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[hitRows[i]], panel) : getPanelValue(&g_chart->rows[hitRows[i]], panel, p));
                if (!val) continue;
                if (isHD) clearHDPanel(&g_chart->rows[hitRows[i]], panel);
                else if (dnAP) clearDNPanel(&g_chart->rows[hitRows[i]], panel);
                else clearPanel(&g_chart->rows[hitRows[i]], panel, p);
            }
        }
    }
}
                
static void processHolds(void)
{
    if (!g_songLoaded) return;
    bool isHD = isHDMode();
    bool dnAP = isDNMode();
    int panCount = isHD ? 6 : (dnAP ? 10 : 5);
    /* Itera players ativos: P1 (0) e/ou P2 (1). HD/DN usam sempre p=0. */
    int _hp0 = (isHD || dnAP) ? 0 : ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int _hp1 = (isHD || dnAP) ? 1 : ((g_game.activePlayerMask == 0x3) ? 2 : _hp0 + 1);
    for (int p = _hp0; p < _hp1; p++)
    {
        for (int panel = 0; panel < panCount; panel++)
        {
            int holdPly;
            PadButton holdBtn;
            if (isHD) {
                holdPly = hdPanelPlayer(panel);
                holdBtn = hdPanelBtn(panel);
            } else if (dnAP) {
                holdPly = dnPanelPlayer(panel);
                holdBtn = dnPanelBtn(panel);
            } else {
                static const PadButton panelToBtn[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                holdPly = p;  /* era 0 (hardcoded P1) — corrigido para p (player atual) */
                holdBtn = panelToBtn[panel];
            }
            bool held = g_autoPanel[panel] ? true : Input_IsPadDown(holdPly, holdBtn);

            // Auto-capture: botao segurado e HH ou HB/HT nao capturado (re-press)
            if (g_holdRows[p][panel] < 0 && held)
            {
                for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
                {
                    uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                    if (!val) continue;
                    double rt = getRowTime(ri);
                    if (g_songTime < rt - JUDGE_BAD) break;
                    if (val == NT_HOLD_H || val == NT_HOLD_B || val == NT_HOLD_T) {
                        g_holdRows[p][panel] = ri;
                        g_nextNoteRow[p][panel] = ri + 1;
                        if (isHD) clearHDPanel(&g_chart->rows[ri], panel);
                        else if (dnAP) clearDNPanel(&g_chart->rows[ri], panel);
                        else clearPanel(&g_chart->rows[ri], panel, p);
                        g_noteState[p][panel] = 1;
                        g_noteExplodeRow[p][panel] = ri;
                        g_noteExplodeFrame[p][panel] = 0;
                        int hasTap = 0;
                        for (int pan = 0; pan < panCount; pan++)
                            if (pan != panel && (isHD ? getNoteHD(&g_chart->rows[ri], pan) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], pan) : getPanelValue(&g_chart->rows[ri], pan, p)))) { hasTap = 1; break; }
                        if (!hasTap) {
                            g_game.stats.combo[p]++;
                            g_game.stats.missCombo[p] = 0;
                            g_game.stats.score[p] += 1000;
                            if (g_game.stats.combo[p] > 3) g_game.stats.score[p] += 1000;
                            g_game.stats.perfectCount[p]++;
                            if (g_game.stats.combo[p] > g_game.stats.maxCombo[p]) g_game.stats.maxCombo[p] = g_game.stats.combo[p];
                            g_judgeDisplayType[p] = JT_PERFECT;
                            g_judgeDisplayTimer[p] = 0.6f;
                            g_judgeFrame[p] = 40;
                            g_judgeDisplayCombo[p] = g_game.stats.combo[p];
                        }
                    }
                }
            }

            if (g_holdRows[p][panel] < 0) continue;

            for (int ri = g_holdRows[p][panel] + 1; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (val != 0 && val != NT_HOLD_B && val != NT_HOLD_T) break;
                if (val == 0) continue;

                double rowTime = getRowTime(ri);
                if (g_songTime < rowTime - 0.01) break;

                bool alreadyJudged = false;
                for (int h = 0; h < g_noteHitCount[p][panel]; h++)
                    if (g_noteHits[p][panel][h].rowIndex == ri) { alreadyJudged = true; break; }
                if (alreadyJudged) continue;

                if (!held)
                {
                    g_game.stats.combo[p] = 0;
                    g_game.stats.missCount[p]++;
                    g_game.stats.missCombo[p]++;
                }
                else
                {
                    int hasUnjudgedTap = false;
                    for (int op = 0; op < panCount; op++) {
                        if (op == panel) continue;
                        uint8_t ov = isHD ? getNoteHD(&g_chart->rows[ri], op) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], op) : getPanelValue(&g_chart->rows[ri], op, p));
                        if (ov && ov != NT_HOLD_B && ov != NT_HOLD_T) { hasUnjudgedTap = true; break; }
                    }
                    if (isHD) clearHDPanel(&g_chart->rows[ri], panel);
                    else if (dnAP) clearDNPanel(&g_chart->rows[ri], panel);
                    else clearPanel(&g_chart->rows[ri], panel, p);
                    g_noteState[p][panel] = 1;
                    g_noteExplodeRow[p][panel] = ri;
                    g_noteExplodeFrame[p][panel] = 0;
                    if (!hasUnjudgedTap) {
                        g_game.stats.combo[p]++;
                    g_game.stats.missCombo[p] = 0;
                    g_game.stats.score[p] += 1000;
                    if (g_game.stats.combo[p] > 3)
                        g_game.stats.score[p] += 1000;
                    g_game.stats.perfectCount[p]++;
                    if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
                        g_game.stats.maxCombo[p] = g_game.stats.combo[p];
                    g_judgeDisplayType[p] = JT_PERFECT;
                    g_judgeDisplayTimer[p] = 0.6f;
                    g_judgeFrame[p] = 40;
                    g_judgeDisplayCombo[p] = g_game.stats.combo[p];
                }
                }

                if (val == NT_HOLD_T || !held)
                {
                    g_holdRows[p][panel] = -1;
                    g_nextNoteRow[p][panel] = ri + 1;
                }
            }
        }
    }
}

static void processMisses(void)
{
    if (!g_songLoaded) return;

    bool isHD = isHDMode();
    bool dnAP = isDNMode();
    int panCount = isHD ? 6 : (dnAP ? 10 : 5);

    double missThreshold = g_songTime - JUDGE_BAD;
    /* Itera players ativos. HD/DN usam p=0; single usa p=0, p=1, ou ambos. */
    int _mp0 = (isHD || dnAP) ? 0 : ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int _mp1 = (isHD || dnAP) ? 1 : ((g_game.activePlayerMask == 0x3) ? 2 : _mp0 + 1);
    for (int p = _mp0; p < _mp1; p++)
    {
        int missedRows[256], missCount = 0;
        for (int panel = 0; panel < panCount; panel++)
        {
            int missPly;
            PadButton missBtn;
            if (isHD) {
                missPly = hdPanelPlayer(panel);
                missBtn = hdPanelBtn(panel);
            } else if (dnAP) {
                missPly = dnPanelPlayer(panel);
                missBtn = dnPanelBtn(panel);
            } else {
                static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                missPly = p;  /* era 0 (hardcoded P1) — corrigido para p (player atual) */
                missBtn = btnMap[panel];
            }
            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = isHD ? getNoteHD(&g_chart->rows[ri], panel) : (dnAP ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (!val) continue;
                if (val == NT_HOLD_H || val == NT_HOLD_B || val == NT_HOLD_T)
                {
                    if (g_holdRows[p][panel] >= 0) continue;
                    if (Input_IsPadDown(missPly, missBtn)) continue;
                }

                double rowTime = getRowTime(ri);
                if (missThreshold <= rowTime) break;

                g_nextNoteRow[p][panel] = ri + 1;
                int dup = 0;
                for (int m = 0; m < missCount; m++)
                    if (missedRows[m] == ri) { dup = 1; break; }
                if (!dup) missedRows[missCount++] = ri;
            }
        }

        if (missCount > 0)
        {
            g_game.stats.combo[p] = 0;
            g_game.stats.missCount[p] += missCount;
            g_game.stats.missCombo[p] += missCount;
            g_judgeDisplayType[p] = JT_MISS;
            g_judgeDisplayTimer[p] = 0.6f;
            g_judgeFrame[p] = 25;
            g_judgeDisplayCombo[p] = g_game.stats.missCombo[p];
            for (int m = 0; m < missCount; m++)
                applyLife(p, JT_MISS); /* penalidade por linha perdida */
            for (int m = 0; m < missCount; m++)
            {
                for (int pan = 0; pan < panCount; pan++)
                {
                    uint8_t val = isHD ? getNoteHD(&g_chart->rows[missedRows[m]], pan) : (dnAP ? getDNPanelValue(&g_chart->rows[missedRows[m]], pan) : getPanelValue(&g_chart->rows[missedRows[m]], pan, p));
                    if (val)
                    {
                        NoteHit* nh = &g_noteHits[p][pan][g_noteHitCount[p][pan]++];
                        nh->rowIndex = missedRows[m];
                        nh->judged = true;
                        nh->judgment = JT_MISS;
                        nh->hitTime = g_songTime;
                    }
                }
            }
        }
    }
}

void Gameplay_Start(int songId)
{
    memset(&g_game.stats, 0, sizeof(g_game.stats));
    g_game.stats.life[0]      = 224; /* baseline visual: 11+2/3 de 26 retangulos ao inicio da musica. */
    g_game.stats.life[1]      = 224;
    g_game.stats.lifeSpeed[0] = LIFE_SPEED_INIT; /* 500 — GameInit easy: _DAT_00da2328 = 500 */
    g_game.stats.lifeSpeed[1] = LIFE_SPEED_INIT;
    memset(g_judgeDisplayTimer, 0, sizeof(g_judgeDisplayTimer));
    memset(g_hitTimer, 0, sizeof(g_hitTimer));
    memset(g_glowTimer, 0, sizeof(g_glowTimer));
    memset(g_p1FlashTimer, 0, sizeof(g_p1FlashTimer));
    memset(g_noteState, 0, sizeof(g_noteState));
    memset(g_noteExplodeFrame, 0, sizeof(g_noteExplodeFrame));
    for (int p = 0; p < 2; p++)
        for (int pan = 0; pan < MAX_PANELS; pan++)
            g_holdRows[p][pan] = -1;

    g_game.bgaFrame = 0;
    g_blindTimer[0] = 0;
    g_blindTimer[1] = 0;
    g_prevBlindRow = -1;
    memset(g_lastPerfectRow, -1, sizeof(g_lastPerfectRow));
    g_pendingCount = 0;
    memset(g_pending, 0, sizeof(g_pending));
    // Limpa estados de input (nada de input preso do menu)
    memset(g_game.input.padState, 0, sizeof(g_game.input.padState));
    memset(g_game.input.padPrevState, 0, sizeof(g_game.input.padPrevState));
    Log_Print("GP: initialized\n");

    // Igual Font_LoadFontAndArrows no Ghidra — carrega font.tga, dec00.tga e todos os SPRs da 00.DAT
    {
        char datPath[MAX_PATH];
        snprintf(datPath, sizeof(datPath), "%s\\BGA\\00.DAT", g_game.currentDirectory);
        Resource_LoadFontAndArrows(datPath);
    }

    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int diffTier = g_game.selectedDifficulty;
    loadChartForSong(songId, diffTier, mode->name);

    /* 2P: duplicar half1 → half2 para que P2 veja os mesmos padrões de P1.
     * Aplicado a TODOS os modos quando ambos P1+P2 estão ativos (incluindo BATTLE).
     * P1 lê half1, P2 lê half2 (= cópia de half1). */
    if (g_game.activePlayerMask == 0x3 && g_songLoaded && g_chart) {
        for (int ri = 0; ri < (int)g_chart->rowCount; ri++)
            g_chart->rows[ri].half2 = g_chart->rows[ri].half1;
        Log_Print("GP: 2P mode — duplicated half1 -> half2 (%d rows)\n", g_chart->rowCount);
    }

    Log_Print("Gameplay: started song %d\n", songId);
}

void Gameplay_Exit(void)
{
    BGM_Stop();

    if (g_songLoaded)
    {
        Step_FreeSong(&g_playSong);
        g_songLoaded = false;
    }
    free(g_visualRow);
    g_visualRow = NULL;
    g_visualRowCount = 0;
    Log_Print("Gameplay: exit\n");
}

void Gameplay_Update(float dt)
{
    if (g_game.state != STATE_GAMEPLAY) return;
    if (!g_songLoaded) return;
    if (dt > 0.05f) dt = 0.05f;

    {
        bool hdAP = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                     strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
        bool dnAP = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                    (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
                     strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
        int apKeys[10] = { VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10 };
        int apCount = dnAP ? 10 : (hdAP ? 6 : 5);
        for (int a = 0; a < apCount; a++)
        {
            if (Input_IsKeyHit(apKeys[a]))
            {
                g_autoPanel[a] = !g_autoPanel[a];
                Log_Print("GP: autoplay %d: %s\n", a, g_autoPanel[a] ? "ON" : "OFF");
            }
        }
        g_autoplay = true;
        for (int a = 0; a < apCount; a++)
            if (!g_autoPanel[a]) { g_autoplay = false; break; }
    }

    // Scroll speed adjustment (number keys 1-8, smooth animation)
    for (int k = '1'; k <= '8'; k++)
    {
        if (Input_IsKeyHit(k))
        {
            g_scrollSpeedTarget = (float)(k - '0');
            Log_Print("GP: speed target %.0fX\n", g_scrollSpeedTarget);
        }
    }

    // Smooth interpolation toward target
    float speedDiff = g_scrollSpeedTarget - g_scrollSpeedX;
    if (fabsf(speedDiff) > 0.01f)
        g_scrollSpeedX += speedDiff * dt * 5.0f;
    else
        g_scrollSpeedX = g_scrollSpeedTarget;

    if (BGM_IsDSActive()) {
        uint32_t posMs = BGM_GetPositionMs();
        if (posMs > 100) // ignore first 100ms (startup)
            g_songTime = posMs / 1000.0 - 0.150; // compensate audio buffer
        else
            g_songTime += dt;
    } else {
        g_songTime += dt;
    }

    {
        int maxFrame = g_game.bgaMaxFrame;
        if (g_game.isVSL && g_vsl.active && g_vsl.frameCount > 0)
            maxFrame = g_vsl.frameCount - 1;
        if (maxFrame > 0) {
            float bgaTime = (float)g_songTime;
            if (bgaTime < 0.0f) bgaTime = 0.0f;
            int newFrame = (int)(bgaTime * 60.0f);
            if (newFrame > maxFrame) newFrame = maxFrame;
            g_game.bgaFrame = newFrame;
        }
    }

    processInput(0);
    if (g_game.activePlayerMask & 0x2) processInput(1);
    processPendingRows(0);
    if (g_game.activePlayerMask & 0x2) processPendingRows(1);
    processAutoplay();
    processHolds();
    processMisses();

    /* Stage Break: missCombo consecutivo > 50 → game over imediato (original) */
    if (g_game.stats.missCombo[0] > STAGE_BREAK_MISSES) {
        Log_Print("GP: stage break (missCombo=%d)\n", g_game.stats.missCombo[0]);
        BGM_Stop();
        Game_ChangeState(STATE_GAMEOVER_ENTER);
        return;
    }

    for (int p = 0; p < 2; p++)
    {
        if (g_judgeDisplayTimer[p] > 0)
            g_judgeDisplayTimer[p] -= dt;
        if (g_judgeFrame[p] > 0)
            g_judgeFrame[p]--;
        for (int pan = 0; pan < MAX_PANELS; pan++) {
            if (g_hitTimer[p][pan] > 0)
                g_hitTimer[p][pan]--;
            if (g_glowTimer[p][pan] > 0)
                g_glowTimer[p][pan]--;
            if (g_p1FlashTimer[p][pan] > 0)
                g_p1FlashTimer[p][pan]--;
            if (g_noteState[p][pan] == 1) { // EXPLODING
                g_noteExplodeFrame[p][pan]++;
                if (g_noteExplodeFrame[p][pan] >= 25)
                    g_noteState[p][pan] = 0; // reseta estado
            }
        }
    }
    // Popup update (sobe e fade out)
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_popups[i].active) continue;
        g_popups[i].y += 60.0f * dt;  // sobe
        g_popups[i].alpha -= 1.2f * dt;  // fade
        if (g_popups[i].alpha <= 0) g_popups[i].active = false;
    }

    // Blind por beat (ativa 02.SPR em cada batida)
    if (g_chart && g_chart->beatSplit > 0 && g_songTime > 0) {
        int curBeatRow = (int)(getRowAtTimeFloat(g_songTime) / (double)g_chart->beatSplit);
        if (curBeatRow != g_prevBlindRow) {
            g_prevBlindRow = curBeatRow;
            g_blindTimer[0] = 10;
            g_blindTimer[1] = 10;
        }
    }
    for (int p = 0; p < 2; p++) {
        if (g_blindTimer[p] > 0) g_blindTimer[p]--;
    }

    // ===== Detecção de fim de música =====
    // Rastreia o maior g_songTime visto e conta frames estagnados
    if (g_songTime > g_maxSongTime) {
        g_maxSongTime = g_songTime;
        g_stagnantFrames = 0;
    } else {
        g_stagnantFrames++;
    }

    // ORIGINAL: g_songTime parou por 2s (120 frames) → música acabou
    if (g_stagnantFrames >= 60 && g_songTime > 5.0) {
        Log_Print("GP: song ended (stagnant %.1fs for %d frames)\n", g_songTime, g_stagnantFrames);
        BGM_Stop();
        Game_ChangeState(STATE_DANCE_GRADE_ENTER);
        return;
    }

    // MCI: g_songTime continua avançando (+= dt), verificar por timeout
    if (!BGM_IsDSActive() && g_hasAudio) {
        // Tempo estimado da música
        double expectedEnd = 0;
        if (g_totalSongSeconds > 0)
            expectedEnd = g_totalSongSeconds;
        else if (g_game.bgm.useMCI && g_game.bgm.durationMs > 0)
            expectedEnd = g_game.bgm.durationMs / 1000.0;
        if (expectedEnd > 0 && g_songTime >= expectedEnd + 5.0) {
            Log_Print("GP: audio ended via MCI/DirectSound timeout (%.1f >= %.1f)\n", g_songTime, expectedEnd);
            BGM_Stop();
            Game_ChangeState(STATE_DANCE_GRADE_ENTER);
            return;
        }
    }

    // Sem audio: esperar o STX terminar
    if (!g_hasAudio) {
        bool canTransition = false;
        if (g_chart && g_chart->rowCount > 0) {
            float scrollRow = (float)getRowAtTimeFloat(g_songTime);
            canTransition = (scrollRow >= (float)(g_chart->rowCount - 1));
        } else {
            canTransition = (g_totalSongSeconds > 0 && g_songTime >= g_totalSongSeconds);
        }
        if (canTransition) {
            Game_ChangeState(STATE_DANCE_GRADE_ENTER);
            return;
        }
    }
}

void Gameplay_Render(void)
{
    if (g_game.state != STATE_GAMEPLAY) return;

    if (!g_songLoaded)
    {
        Font_DrawStringCentered(g_game.screenWidth/2, g_game.screenHeight/2,
            "Loading...", 1,1,1,1);
        return;
    }

    int receptorY = 38;
    bool isHalfDouble = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                         strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "HALFDOUBLE") == 0);
    bool isDoubleOrNightmare = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
                               (strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "DOUBLE") == 0 ||
                                strcmp(g_game.songDB.modes[g_game.selectedModeIndex].name, "NIGHTMARE") == 0));
    int sprReceptor = isHalfDouble ? g_fontSprHD01 : (isDoubleOrNightmare ? g_fontSprW01 : g_fontSpr01);
    int sprBlind    = isHalfDouble ? g_fontSprHD02 : (isDoubleOrNightmare ? g_fontSprW02 : g_fontSpr02);
    int sprLifeBord = isHalfDouble ? g_fontSprHD03 : (isDoubleOrNightmare ? g_fontSprW03 : g_fontSpr03);
    int sprLifeGlow = isHalfDouble ? g_fontSprHD05 : (isDoubleOrNightmare ? g_fontSprW05 : g_fontSpr05);
    int scrollBottom = 480;

    // Current segment and actual scrollRow for timing-dependent calculations
    int currentSeg = 0;
    double currentSpr = g_secondsPerRow;
    for (int s = g_chart->segmentCount - 1; s >= 0; s--)
    {
        if (g_songTime >= getRowTime(g_chart->segments[s].rowStart))
        {
            currentSpr = getSegmentSpr(s);
            currentSeg = s;
            break;
        }
    }
    double actualScrollRow = getRowAtTimeFloat(g_songTime);

    // Visual scroll row: BPM-based, beatSplit-normalized for constant visual speed
    // Espaçamento original: 60.0 / beatSplit px/row × speedMult (confirmado Ghidra).
    float g_baseRowSpacing = (g_baseBeatSplit > 0) ? (60.0f / (float)g_baseBeatSplit) : 15.0f;
    float pixelsPerRow = g_baseRowSpacing * g_scrollSpeedX;
    double visualScrollRow = 0;
    if (g_visualRow && g_visualRowCount > 0) {
        int vr = (int)actualScrollRow;
        if (vr < 0) vr = 0;
        if (vr >= g_visualRowCount) vr = g_visualRowCount - 1;
        double frac = actualScrollRow - floor(actualScrollRow);
        visualScrollRow = g_visualRow[vr];
        if (vr + 1 < g_visualRowCount)
            visualScrollRow += (g_visualRow[vr + 1] - g_visualRow[vr]) * frac;
    }
    float currentPixelsPerSec = (float)(pixelsPerRow / currentSpr);

    // Determine visible actual row range (uses actualScrollRow, which is in actual-row space)
    int startRow = (int)actualScrollRow - (int)(480 / pixelsPerRow) - 2;
    if (startRow < 0) startRow = 0;

    /* endRow: percorre g_visualRow para achar o ultimo actual row visivel na tela.
     * Necessario porque quando beatSplit aumenta (ex: 4->8), cada actual row ocupa
     * menos espaco visual — o calculo simples (480/pixelsPerRow) fica curto e as
     * setas aparecem do nada em vez de surgir organicamente pelo fundo da tela. */
    int endRow;
    if (g_visualRow && g_visualRowCount > 0) {
        float targetVisualEnd = (float)visualScrollRow
            + (float)(scrollBottom + PANEL_SIZE - receptorY) / pixelsPerRow;
        endRow = (int)actualScrollRow;
        for (int _ri = (int)actualScrollRow; _ri < g_visualRowCount; _ri++) {
            if ((float)g_visualRow[_ri] >= targetVisualEnd) {
                endRow = _ri + 2;
                break;
            }
            endRow = _ri;
        }
    } else {
        endRow = (int)actualScrollRow + (int)((scrollBottom - receptorY) / pixelsPerRow) + 4;
    }
    if (endRow >= (int)g_chart->rowCount) endRow = g_chart->rowCount - 1;

    // Compute judgment zone half-heights using current BPM-based scroll speed
    float jZoneHalf[4];
    float jWindows[4] = { JUDGE_BAD, JUDGE_GOOD, JUDGE_GREAT, JUDGE_PERFECT };
    for (int j = 0; j < 4; j++)
        jZoneHalf[j] = jWindows[j] * currentPixelsPerSec;

    /* Para HD/DN: sempre p=0, layout especial.
     * Para modos single (Normal/Hard/Crazy/Battle com 2P): loop pelos players ativos.
     *   P1 sozinho  (0x1): p=0
     *   P2 sozinho  (0x2): p=1
     *   P1+P2       (0x3): p=0 e p=1
     * Posições: P1 solo → centro; P1 com P2 → esquerda; P2 → direita. */
    bool twoPlayers = (g_game.activePlayerMask == 0x3) && !isHalfDouble && !isDoubleOrNightmare;
    int pRend0 = (isHalfDouble || isDoubleOrNightmare) ? 0 :
                 ((g_game.activePlayerMask == 0x2) ? 1 : 0);
    int pRend1 = (isHalfDouble || isDoubleOrNightmare) ? 1 :
                 (twoPlayers ? 2 : pRend0 + 1);

    for (int p = pRend0; p < pRend1; p++)
    {
        /* Posições e contagem de painéis para este player/iteração */
        float posX[10];
        int pW[10];
        int panelCount;
        int centerX;

        if (isHalfDouble) {
            panelCount = 6;
            for (int i = 0; i < 6; i++) { posX[i] = 171.0f + i * 48.0f + (i >= 3 ? 7.0f : 0.0f); pW[i] = 54; }
            centerX = 320;
        } else if (isDoubleOrNightmare) {
            panelCount = 10;
            for (int i = 0; i < 10; i++) pW[i] = 54;
            for (int i = 0; i < 5; i++) posX[i] = 74.0f + i * 48.0f;
            for (int i = 5; i < 10; i++) posX[i] = 323.0f + (i-5) * 48.0f;
            centerX = 320;
        } else {
            panelCount = 5;
            for (int i = 0; i < 5; i++) pW[i] = 54;
            if (p == 1) {
                /* P2 (sozinho ou com P1): lado direito, espelhado de P1.
                 * P1 centro=161, P2 centro=479 (simetrico em 640px).
                 * P2[0]=356, ..., P2[4]=548. Gap entre P1(284) e P2(356) = 72px. */
                for (int i = 0; i < 5; i++) posX[i] = 358.0f + i * 48.0f;
                centerX = P2_CENTER_X;
            } else {
                /* P1 (sozinho ou com P2): posição padrão esquerda (mesma do solo) */
                posX[0] = 38.0f;
                for (int i = 1; i < 5; i++) posX[i] = 38.0f + i * 48.0f;
                centerX = P1_CENTER_X;
            }
        }

        /* // Zonas de acerto (julgamento) - desativadas, 01.SPR substitui
        {
            float jColors[4][4] = {
                {1, 0.3f, 0.3f, 0.50f},
                {1, 1, 0, 0.55f},
                {0.5f, 1, 0.5f, 0.55f},
                {0.5f, 0.8f, 1, 0.60f},
            };
            for (int j = 0; j < 4; j++)
            {
                int halfH = (int)(jZoneHalf[j] + 0.5f);
                for (int panel = 0; panel < 5; panel++)
                    Render_Rect(posX[panel] + 1, (float)(receptorY - halfH + 2), (float)(pW[panel] - 2), (float)(halfH * 2 - 4),
                        (uint8_t)(jColors[j][0] * 255), (uint8_t)(jColors[j][1] * 255),
                        (uint8_t)(jColors[j][2] * 255), (uint8_t)(jColors[j][3] * 255));
                }
            }
        }
        */

        /* Grid de fundo (compasso/batida/sub-batida) - desativado
        if (g_chart->beatPerMeasure > 0 && g_chart->beatSplit > 0)
        {
            int gx0 = baseX - 22;
            int gx1 = baseX + 162;
            for (int ri = startRow; ri <= endRow; ri++)
            {
                float gy = (float)(receptorY + (ri - scrollRow) * pixelsPerRow);
                if (gy < receptorY - PANEL_SIZE || gy > scrollBottom + PANEL_SIZE) continue;

                int blockLine;
                int blockNum = getBlockInfo(ri, &blockLine);
                if (blockLine == 1)
                {
                    Render_Rect((float)gx0, (float)gy, (float)(gx1 - gx0), 3, 255, 255, 255, 255);
                    char num[8];
                    snprintf(num, sizeof(num), "%d", blockNum);
                    int numY = g_game.screenHeight - (int)gy - 6;
                    Font_DrawStringScaled(gx0 - 30, numY, num, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f);
                }
                else if ((blockLine - 1) % g_chart->beatSplit == 0)
                {
                    for (int x = gx0; x < gx1; x += 16)
                        Render_Rect((float)x, (float)gy, 8, 2, 255, 255, 255, 200);
                }
                else
                {
                    for (int x = gx0; x < gx1; x += 24)
                        Render_Rect((float)x, (float)gy, 4, 1, 200, 200, 200, 150);
                }
            }
        }
        */

        /* // Receptor no topo - desativado, 01.SPR substitui
        int rh = 57;
        for (int panel = 0; panel < 5; panel++)
        {
            float px = posX[panel];
            int rw = pW[panel];
            uint8_t rr, rg, rb;
            if (panel == 0 || panel == 4)      { rr = 52; rg = 120; rb = 200; }
            else if (panel == 1 || panel == 3) { rr = 200; rg = 60; rb = 60; }
            else                                { rr = 220; rg = 200; rb = 40; }
            Render_Rect(px, receptorY, (float)rw, (float)rh, rr, rg, rb, 180);
            Render_Rect(px + 2, receptorY + 2, (float)(rw - 4), (float)(rh - 4), 0, 0, 0, 120);
        }
        */

        // 01.SPR receptor (g_fontSpr01) — renderiza ANTES das notas (abaixo delas)
        // Para single (não HD/DN): srcX baked para P1-solo (base=38). Offset por player.
        {
            float recOffX = (isHalfDouble || isDoubleOrNightmare) ? 0.0f : (posX[0] - 38.0f);
            if (sprReceptor >= 0) {
                int cnt = sprTileCount(sprReceptor);
                for (int t = cnt - 1; t >= 0; t--) {
                    int idx = sprReceptor + t;
                    float sx = (float)g_game.sprTiles[idx].srcX + recOffX;
                    float sy = (float)g_game.sprTiles[idx].srcY;
                    float sw = (float)g_game.sprTiles[idx].srcW;
                    float sh = (float)g_game.sprTiles[idx].srcH;
                    Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, 1.0f);
                }
            }
            // 02.SPR blind (g_fontSpr02) — também antes das notas
            if (sprBlind >= 0 && g_blindTimer[p] > 0) {
                float blindA = (float)g_blindTimer[p] / 10.0f;
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                int cnt = sprTileCount(sprBlind);
                for (int t = cnt - 1; t >= 0; t--) {
                    int idx = sprBlind + t;
                    float sx = (float)g_game.sprTiles[idx].srcX + recOffX;
                    float sy = (float)g_game.sprTiles[idx].srcY;
                    float sw = (float)g_game.sprTiles[idx].srcW;
                    float sh = (float)g_game.sprTiles[idx].srcH;
                    Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, blindA);
                }
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
        }

        /* Tile "p1" do ARROW54X.SP2 — borda branca/cinza da seta.
         * Original (Ghidra): ao pressionar o botão (borda de subida), aparece com
         * zoom (1.3x→1.0x) e some. IsPadHit dispara g_p1FlashTimer. */
        {
            for (int pan = 0; pan < panelCount; pan++) {
                int ft = g_p1FlashTimer[p][pan];
                if (ft <= 0) continue;

                int arrowType;
                if (isHalfDouble) {
                    arrowType = (pan == 0 || pan == 5) ? 2 : (pan == 1) ? 3 : (pan == 2) ? 4 : (pan == 3) ? 0 : 1;
                } else if (isDoubleOrNightmare) {
                    arrowType = pan % 5;
                } else {
                    arrowType = pan;
                }
                int baseIdx = (arrowType == 0) ? g_fontArrow542 :
                              (arrowType == 1) ? g_fontArrow541 :
                              (arrowType == 2) ? g_fontArrow545 :
                              (arrowType == 3) ? g_fontArrow543 :
                              (arrowType == 4) ? g_fontArrow544 : -1;
                if (baseIdx < 0) continue;
                int p1Idx = baseIdx + 6;  /* tile "p1" = índice 6 no SP2 */
                if (p1Idx >= g_game.sprTileCount) continue;
                float sw = (float)g_game.sprTiles[p1Idx].srcW;
                float sh = (float)g_game.sprTiles[p1Idx].srcH;
                int p1Pan = isDoubleOrNightmare ? (pan % 5) : (isHalfDouble ? arrowType : pan);
                static const float p1OffXReg[5] = {-7.0f, -6.0f, -5.0f, -6.0f, -7.0f};
                static const float p1OffXHD[5]  = {-5.0f, -6.0f, -7.0f, -6.0f, -5.0f};
                const float* p1OffX = (isHalfDouble || isDoubleOrNightmare) ? p1OffXHD : p1OffXReg;

                /* t: 1.0 no início → 0.0 no fim da animação */
                float t = (float)ft / 15.0f;
                float scale = 1.1f - 0.3f * t;  /* 0.8x → 1.1x (zoom out) */
                float alpha = t;                  /* fade out */
                float cx = posX[pan] + p1OffX[p1Pan] + sw / 2.0f;
                float cy = (float)(receptorY + 28);
                Sprite_DrawTileUV(p1Idx, cx, cy, sw * scale, sh * scale, alpha);
            }
        }

        int rh = 57;
        // Notas (scroll do fundo para o topo)
        int rh2 = 57;
        static const int kPanelOrder[5] = {1, 3, 0, 2, 4};
        static const int kBodyTile[5] = {12, 16, 20, 18, 14};
        static const int kTailTile[5] = {13, 17, 21, 19, 15};
        static const float kBodyOffX[5] = {-4.0f, -3.0f, 0.0f, 3.0f, 4.0f};
        // HD mappings: pos 0=CN, 1=UR, 2=DR, 3=DL, 4=UL, 5=CN
        static const int kHDBodyTile[6] = {20, 18, 14, 12, 16, 20};
        static const int kHDTailTile[6] = {21, 19, 15, 13, 17, 21};
        static const float kHDBodyOffX[6] = {0.0f, 3.0f, 4.0f, -4.0f, -3.0f, 0.0f};

        // Pass 0: Hold bodies (esticados entre runs de NT_HOLD_B)
        if (g_fontArrowETC >= 0) {
            for (int panel = 0; panel < panelCount; panel++)
            {
                int arrowIdx = isDoubleOrNightmare ? (panel % 5) : panel;
                for (int ri = startRow; ri <= endRow; ri++)
                {
                    uint8_t val = isHalfDouble ? getNoteHD(&g_chart->rows[ri], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                    if (val != NT_HOLD_B) continue;
                    if (ri == g_lastPerfectRow[p][panel]) continue;

                    int endRi = ri + 1;
                    while (endRi < (int)g_chart->rowCount) {
                        uint8_t nv = isHalfDouble ? getNoteHD(&g_chart->rows[endRi], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[endRi], panel) : getPanelValue(&g_chart->rows[endRi], panel, p));
                        if (nv != NT_HOLD_B) break;
                        endRi++;
                    }

                    float vri = (ri < g_visualRowCount && g_visualRow) ? (float)g_visualRow[ri] : (float)ri;
                    float vendRi = (endRi < g_visualRowCount && g_visualRow) ? (float)g_visualRow[endRi] : (float)endRi;
                    float y1 = (float)(receptorY + rh2 / 2 + (vri - visualScrollRow) * pixelsPerRow);
                    float y2 = (float)(receptorY + rh2 / 2 + (vendRi - visualScrollRow) * pixelsPerRow);
                    if (y2 < y1) { float t = y1; y1 = y2; y2 = t; }
                    float totalH = y2 - y1;
                    if (totalH <= 0) { ri = endRi - 1; continue; }

                    int idx = g_fontArrowETC + (isHalfDouble ? kHDBodyTile[panel] : kBodyTile[arrowIdx]);
                    float sw = (float)g_game.sprTiles[idx].srcW;
                    float offX = isHalfDouble ? kHDBodyOffX[panel] : kBodyOffX[arrowIdx];
                    Sprite_DrawTileUV(idx, posX[panel] + sw / 2.0f + offX, y1 + totalH / 2.0f, sw, totalH, 1.0f);
                    ri = endRi - 1;
                }
            }
        }

        // Pass 1: Hold tails
        for (int ri = startRow; ri <= endRow; ri++)
        {
            if (g_fontArrowETC < 0) break;
                float vri = (ri < g_visualRowCount && g_visualRow) ? (float)g_visualRow[ri] : (float)ri;
            float y = (float)(receptorY + rh2 / 2 + (vri - visualScrollRow) * pixelsPerRow);
            if (y < receptorY - rh2 / 2 - 50 || y > scrollBottom + PANEL_SIZE) continue;
            for (int rio = 0; rio < panelCount; rio++)
            {
                int panel, arrowIdx;
                if (isDoubleOrNightmare) {
                    int halfBase = (rio < 5) ? 0 : 5;
                    int localIdx = kPanelOrder[rio % 5];
                    panel = halfBase + localIdx;
                    arrowIdx = panel % 5;
                } else if (isHalfDouble) {
                    panel = rio;
                    arrowIdx = panel;
                } else {
                    panel = kPanelOrder[rio];
                    arrowIdx = panel;
                }
                uint8_t val = isHalfDouble ? getNoteHD(&g_chart->rows[ri], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (val != NT_HOLD_T) continue;
                int idx = g_fontArrowETC + (isHalfDouble ? kHDTailTile[panel] : kTailTile[arrowIdx]);
                float sw = (float)g_game.sprTiles[idx].srcW;
                float sh = (float)g_game.sprTiles[idx].srcH;
                Sprite_DrawTileUV(idx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
            }
        }
        // Pass 2: Taps/HoldHeads
        for (int ri = startRow; ri <= endRow; ri++)
        {
            float vri = (ri < g_visualRowCount && g_visualRow) ? (float)g_visualRow[ri] : (float)ri;
            float y = (float)(receptorY + rh2 / 2 + (vri - visualScrollRow) * pixelsPerRow);
            if (y < receptorY - rh2 / 2 - 50 || y > scrollBottom + PANEL_SIZE) continue;
            for (int rio = 0; rio < panelCount; rio++)
            {
                int panel, arrowIdx;
                if (isDoubleOrNightmare) {
                    int halfBase = (rio < 5) ? 0 : 5;
                    int localIdx = kPanelOrder[rio % 5];
                    panel = halfBase + localIdx;
                    arrowIdx = panel % 5;
                } else if (isHalfDouble) {
                    panel = rio;
                    arrowIdx = panel;
                } else {
                    panel = kPanelOrder[rio];
                    arrowIdx = panel;
                }
                uint8_t val = isHalfDouble ? getNoteHD(&g_chart->rows[ri], panel) : (isDoubleOrNightmare ? getDNPanelValue(&g_chart->rows[ri], panel) : getPanelValue(&g_chart->rows[ri], panel, p));
                if (!val || val == NT_HOLD_B || val == NT_HOLD_T) continue;

                // HD: pos 0=CN(545), 1=UR(543), 2=DR(544), 3=DL(542), 4=UL(541), 5=CN(545)
                int arrowGroup;
                if (isHalfDouble) {
                    arrowGroup = (panel == 0 || panel == 5) ? 2 : (panel == 1) ? 3 : (panel == 2) ? 4 : (panel == 3) ? 0 : 1;
                } else {
                    arrowGroup = arrowIdx;
                }
                int arrowSpr = (arrowGroup == 0) ? g_fontArrow542 :
                               (arrowGroup == 1) ? g_fontArrow541 :
                               (arrowGroup == 2) ? g_fontArrow545 :
                               (arrowGroup == 3) ? g_fontArrow543 :
                               (arrowGroup == 4) ? g_fontArrow544 : -1;
                if (arrowSpr >= 0) {
                    int af = (g_game.frameCounter / 3) % 6;
                    int aidx = arrowSpr + af;
                    float sw = (float)g_game.sprTiles[aidx].srcW;
                    float sh = (float)g_game.sprTiles[aidx].srcH;
                    Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
            }
        }

        int centerY = g_game.screenHeight / 2;
    int receptorY = 38; // Same as in rendering loop


        /* Mode/Modifier sprites do ARROW541.SP2.
         * P2 sozinho (HD/DN/qualquer modo) → lado direito; caso contrário → esquerdo. */
        if (g_fontArrow541 >= 0) {
            bool hudRight = (g_game.activePlayerMask == 0x2);
            const char* modeName = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount) ? g_game.songDB.modes[g_game.selectedModeIndex].name : "EASY";
            int modeOff = 31; // modeez (default)
            if (strcmp(modeName, "HARD") == 0) modeOff = 32;
            else if (strcmp(modeName, "CRAZY") == 0) modeOff = 33;
            int modeIdx = g_fontArrow541 + modeOff;
            if (modeIdx < g_game.sprTileCount) {
                float sw = (float)g_game.sprTiles[modeIdx].srcW;
                float sh = (float)g_game.sprTiles[modeIdx].srcH;
                float hx = hudRight ? (640.0f - 18.0f - sw/2.0f) : (18.0f + sw/2.0f);
                Sprite_DrawTileUV(modeIdx, hx, 152 + sh/2, sw, sh, 1.0f);
            }
            int speedOff = 36; // accel1
            if (g_scrollSpeedTarget >= 4.0f) speedOff = 9; // accel4
            else if (g_scrollSpeedTarget >= 3.0f) speedOff = 8; // accel3
            else if (g_scrollSpeedTarget >= 2.0f) speedOff = 7; // accel2
            int speedIdx = g_fontArrow541 + speedOff;
            if (speedIdx < g_game.sprTileCount) {
                float sw = (float)g_game.sprTiles[speedIdx].srcW;
                float sh = (float)g_game.sprTiles[speedIdx].srcH;
                float hx = hudRight ? (640.0f - 18.0f - sw/2.0f) : (18.0f + sw/2.0f);
                Sprite_DrawTileUV(speedIdx, hx, 184 + sh/2, sw, sh, 1.0f);
            }
            int disOffsets[4] = { 34, 35, 37, 38 };
            float disY[4] = { 216, 248, 280, 312 };
            for (int di = 0; di < 4; di++) {
                int didx = g_fontArrow541 + disOffsets[di];
                if (didx < g_game.sprTileCount) {
                    float sw = (float)g_game.sprTiles[didx].srcW;
                    float sh = (float)g_game.sprTiles[didx].srcH;
                    float hx = hudRight ? (640.0f - 18.0f - sw/2.0f) : (18.0f + sw/2.0f);
                    Sprite_DrawTileUV(didx, hx, (float)(disY[di] + sh/2), sw, sh, 1.0f);
                }
            }
        }

        // Stage indicator sprite (M01-M05)
        // loading.c decrementa stageCount ANTES do gameplay:
        // Stage 1: stageCount=2, Stage 2: stageCount=1, Final: stageCount=0, Bonus: isBonusSong
        int stageSpr = -1;
        if (g_game.isBonusSong) stageSpr = g_fontSprM05;
        else if (g_game.stageCount == 2) stageSpr = g_fontSprM01;
        else if (g_game.stageCount == 1) stageSpr = g_fontSprM02;
        else if (g_game.stageCount == 0) stageSpr = g_fontSprM04;
        if (stageSpr >= 0 && g_game.sprTileCount > stageSpr) {
            float sx = (float)g_game.sprTiles[stageSpr].srcX;
            float sy = (float)g_game.sprTiles[stageSpr].srcY;
            float sw = (float)g_game.sprTiles[stageSpr].srcW;
            float sh = (float)g_game.sprTiles[stageSpr].srcH;
            if (isHalfDouble && sprLifeBord >= 0) {
                sx = (float)g_game.sprTiles[sprLifeBord].srcX - sw - 10.0f;
            } else if (isDoubleOrNightmare && g_fontSprW04 >= 0) {
                sx = (float)g_game.sprTiles[g_fontSprW04].srcX - sw - 10.0f;
            }
            Sprite_DrawTileUV(stageSpr, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, 1.0f);
        }

        // Judge + combo display (animacao 3 fases — original FUN_0040dd70)
        if (g_judgeDisplayTimer[p] > 0)
        {
            JudgeType jt = g_judgeDisplayType[p];
            int decTimer = g_judgeFrame[p]; // decremented timer (0..24 normal, 0..39 P/G)
            if (decTimer > 39) decTimer = 39;
            int isPG = (jt == JT_GREAT || jt == JT_PERFECT);

            // Tabelas do original (Ghidra DAT_004428d4 / 00442850 / 00442910)
            // normalScaleTable[decTimer] para decTimer 11..24 (pop-in uniform)
            static float normalScale[25] = {
                0.0f,0.0f,0.0f,0.0f,0.0f, 0.0f,0.0f,0.0f,0.0f,0.0f,
                0.0f, // [10]=1.0 (<=10)
                0.99f, 0.98f, 0.97f, 0.98f,  // [11..14]
                0.99f, 1.01f, 1.03f, 1.06f,  // [15..18]
                1.10f, 1.15f, 1.21f, 1.28f,  // [19..22]
                1.35f, 1.43f                   // [23..24]
            };
            // squeezeXTable[decTimer] para decTimer 0..8 (esmagamento X)
            static float squeezeXTable[9] = {
                1.35f, 1.30f, 1.25f, 1.20f, 1.15f,
                1.10f, 1.05f, 1.00f, 1.52f
            };

            float uniformScale;
            float squeezeX = 1.0f;
            float spriteAlpha;

            if (decTimer <= 10) {
                // FASE 2 (timer 0..10): scale uniforme = 1.0
                uniformScale = 1.0f;
            } else if (isPG) {
                if (decTimer > 25) {
                    // P/G EXTENDED (timer 26..39): tabela DAT_00442910
                    // Mapeia 26→11, 39→24 (mesmos valores da normalScale)
                    int idx = decTimer - 15;
                    if (idx < 11) idx = 11;
                    if (idx > 24) idx = 24;
                    uniformScale = normalScale[idx];
                } else {
                    // P/G timer 11..25: constante 0.99 (DAT_004428a8)
                    uniformScale = 0.99f;
                }
            } else {
                // FASE 1 (timer 11..24): tabela normal DAT_004428d4
                int idx = decTimer;
                if (idx > 24) idx = 24;
                if (idx < 11) idx = 11;
                uniformScale = normalScale[idx];
            }
            spriteAlpha = 1.0f;

            if (decTimer < 9) {
                // FASE 3 - Esmagamento (timer 0..8): X squeeze + alpha fade
                squeezeX = squeezeXTable[decTimer];
                spriteAlpha = (float)decTimer * 0.125f;
            }

            // Scale implicito de 0.8x (do glPushMatrix/glScalef interno do original)
            float finalScaleX = uniformScale * squeezeX * 0.8f;
            float finalScaleY = uniformScale * 1.0f * 0.8f;

            // Desenha o sprite do julgamento
            int judgeSpriteIdx = g_fontArrow542 + g_judgeSpriteIndices[jt];
            if (g_fontArrow542 >= 0 && judgeSpriteIdx >= 0 && judgeSpriteIdx < g_game.sprTileCount) {
                float ow = (float)g_game.sprTiles[judgeSpriteIdx].srcW;
                float oh = (float)g_game.sprTiles[judgeSpriteIdx].srcH;

                if (decTimer < 9) {
                    // Additive blend no esmagamento
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                }
                Sprite_DrawTileUV(judgeSpriteIdx, centerX, centerY, ow * finalScaleX, oh * finalScaleY, spriteAlpha);
                if (decTimer < 9) {
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
            }

            // Combo digits usando DEC00 (com a mesma animacao do julgamento)
            int comboVal = 0;
            bool showCombo = false;
            if (jt == JT_MISS) {
                comboVal = g_game.stats.missCombo[p];
                showCombo = comboVal > 3;  /* Ghidra: "if (3 < count)" — exibe a partir de 4 */
            } else {
                comboVal = g_game.stats.combo[p];
                showCombo = comboVal > 3;  /* Ghidra: "if (3 < count)" — exibe a partir de 4 */
            }

            if (showCombo)
            {
                /* Ghidra Judge_RenderAnim (0x40dd70) — layout fiel ao original:
                 *
                 * Dígitos do combo:
                 *   glTranslatef(0, -70, 0) no espaço anim-scaled, depois Combo_RenderValue:
                 *     glTranslatef(24, 0, 0) + glScalef(1.1) + glifos em (0,0)-(44,45)
                 *   → dígito BOTTOM world Y = 240-70 = 170 → screen Y = centerY+70
                 *   → dígito TOP world Y = 219.5 → screen Y = centerY+20.5
                 *   → tens LEFT em local = X_player-20; spacing = 44px (= T(-40) dentro de S(1.1))
                 *
                 * Sprite COMBO_ (label "COMBO" em DEC00.TGA):
                 *   Inner block: glPushMatrix + glScalef(0.8) + RenderSPRData(0x44606c)
                 *   combo_ em ARROW542.SP2: srcX=-67 srcY=-105 srcW=126 srcH=42
                 *   centro local Y-UP = (-4, -84); após 0.8x → screen (centerX-3, centerY+67)
                 */
                float comboScaleX = uniformScale * squeezeX;
                float comboScaleY = uniformScale;

                /* Dígitos — Y: bottom = centerY+70*scale, top = centerY+20.5*scale */
                float dy = (float)centerY + 70.0f * uniformScale - 49.5f * comboScaleY;
                /* X: T(24)*S(1.1)*T(-40 per digit) → tens LEFT = X-20, spacing = 44px */
                float spacing = 44.0f * comboScaleX;
                float tensLeft = (float)centerX - 20.0f * comboScaleX;
                float ux = tensLeft + spacing;   /* units LEFT = X+24 */
                float hx = tensLeft - spacing;   /* hundreds LEFT = X-64 */

                int d3 = comboVal % 10;
                int d2 = (comboVal / 10) % 10;
                int d1 = comboVal / 100;
                float cr = 1.0f;
                float cg = (jt == JT_MISS) ? 0.3f : 1.0f;
                float cb = (jt == JT_MISS) ? 0.3f : 1.0f;
                Font_DrawDecDigit(g_fontDec00Id, ux,       dy, d3, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);
                Font_DrawDecDigit(g_fontDec00Id, tensLeft, dy, d2, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);
                Font_DrawDecDigit(g_fontDec00Id, hx,       dy, d1, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);

                /* Sprite COMBO_ (label): inner 0.8x scale, srcX/srcY em local Y-UP
                 * Centro: (srcX+srcW/2, srcY+srcH/2) = (-4, -84) → screen (centerX-3, centerY+67) */
                if (g_fontArrow542 >= 0) {
                    int comboIdx = g_fontArrow542 + g_judgeSpriteIndices[5] + 1;
                    if (comboIdx < g_game.sprTileCount) {
                        SPRTileDef* ct = &g_game.sprTiles[comboIdx];
                        float sw = (float)ct->srcW * comboScaleX * 0.8f;
                        float sh = (float)ct->srcH * comboScaleY * 0.8f;
                        /* Posição calculada a partir do srcX/srcY do tile (local Y-UP, inner 0.8x): */
                        float ctCX = (float)(ct->srcX + ct->srcW / 2);   /* centro local X ≈ -4 */
                        float ctCY = (float)(ct->srcY + ct->srcH / 2);   /* centro local Y ≈ -84 (Y-UP) */
                        float comboTextX = (float)centerX + ctCX * squeezeX * 0.8f * uniformScale;
                        float comboTextY = (float)centerY - ctCY * 0.8f * uniformScale; /* Y-UP → screen Y-DOWN */
                        Sprite_DrawTileUV(comboIdx, comboTextX, comboTextY, sw, sh, spriteAlpha);
                    }
                }
            }
        }  // end judge/combo

        // Pop-up de score (catch effect)
        for (int i = 0; i < MAX_POPUPS; i++) {
            if (!g_popups[i].active || g_popups[i].player != p) continue;
            char buf[32];
            int popCenterX = centerX;
            snprintf(buf, sizeof(buf), "+%d", g_popups[i].score);
            Font_DrawStringCenteredScaled(popCenterX, (int)g_popups[i].y, buf, 1,1,0, g_popups[i].alpha, 1.2f);
            if (g_popups[i].combo > 1) {
                snprintf(buf, sizeof(buf), "%d", g_popups[i].combo);
                Font_DrawStringCenteredScaled(popCenterX, (int)g_popups[i].y - 16, buf, 1,1,1, g_popups[i].alpha * 0.7f, 0.8f);
            }
        }
    }  // end for p

    // Life bars (03/04/05 ou W03/W04/W05) — renderizadas DEPOIS de todos os players
    if (isDoubleOrNightmare) {
        /* DN lifebar: mesma lógica de pulse BPM + glow que single/halfdouble.
         * W04 = única sprite de fill que cresce da ESQUERDA proporcional à vida.
         * Ordem: fill → glow → border (igual single mode). */
        int lifeValDN = g_game.stats.life[0]; /* DN cooperativo: usa vida de P1 */

        /* BPM pulse — fórmula subtrativa original Ghidra (array 0x442758).
         * displayF = clamp(life/LIFE_INITIAL - (1-bpmTiming)*0.05, 0, 1)
         * bpmTiming cai de 1.0 (inicio do beat) a 0.0 (fim do beat).
         * Com vida baixa o clamp em 0 evita que o pulse apareca — comportamento original. */
        static const float bpmTimingArrDN[60] = {
            1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f, /* 0-9  */
            0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f, /* 10-19 */
            0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f, /* 20-29 */
            0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f, /* 30-39 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f, /* 40-49 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f  /* 50-59 */
        };
        float bpmTimingDN = 1.0f;
        if (g_chart && g_songLoaded && (float)g_songTime > 0.1f) {
            float curBpm = (float)g_chart->segments[0].bpm;
            double acc = g_chartDelay;
            for (int s = 0; s < g_chart->segmentCount; s++) {
                double segDur = g_chart->segments[s].rowCount * getSegmentSpr(s) + getSegmentDelay(s);
                if (g_songTime < acc + segDur || s == g_chart->segmentCount - 1) {
                    curBpm = (float)g_chart->segments[s].bpm; break;
                } acc += segDur;
            }
            if (curBpm > 0.0f) {
                float beatPeriodSec    = 60.0f / curBpm;
                float beatPeriodFrames = beatPeriodSec * 60.0f;
                float frameInBeat      = fmodf((float)g_songTime * 60.0f, beatPeriodFrames);
                int   beatIdx          = (int)(frameInBeat / beatPeriodFrames * 60.0f);
                if (beatIdx < 0)  beatIdx = 0;
                if (beatIdx > 59) beatIdx = 59;
                bpmTimingDN = bpmTimingArrDN[beatIdx];
            }
        }
        float displayFDN = (float)lifeValDN / (float)LIFE_INITIAL - (1.0f - bpmTimingDN) * 0.05f;
        if (displayFDN < 0.0f) displayFDN = 0.0f;
        if (displayFDN > 1.0f) displayFDN = 1.0f;

        /* iVar5 para lifeIsFull — fórmula original (igual single/halfdouble) */
        float scaledDN   = displayFDN * (-256.0f);
        int roundedDN    = (int)(scaledDN > 0.0f ? scaledDN + 0.5f : scaledDN - 0.5f);
        int roundAbsDN   = (roundedDN >= 0) ? roundedDN : -roundedDN;
        int quotientDN   = roundAbsDN / 6;
        int iVar5DN      = 253 - quotientDN * 6;
        if (iVar5DN < 1) iVar5DN = 1;
        bool lifeIsFullDN = (iVar5DN < 2);

        /* W04: barra contínua formada por N tiles sequenciais.
         * Tile 0 preenche primeiro; tile 1 só começa após tile 0 estar cheio (~50%).
         * fillW = totalW * displayFDN pixels a preencher no total. */
        if (g_fontSprW04 >= 0) {
            int tileCnt = sprTileCount(g_fontSprW04);
            /* Soma largura total dos tiles */
            float totalW = 0.0f;
            for (int t = 0; t < tileCnt; t++)
                totalW += (float)g_game.sprTiles[g_fontSprW04 + t].srcW;
            float fillW    = totalW * displayFDN; /* pixels totais a preencher */
            float consumed = 0.0f;
            for (int t = 0; t < tileCnt; t++) {
                int idx = g_fontSprW04 + t;
                SPRTileDef* tile = &g_game.sprTiles[idx];
                float tileW = (float)tile->srcW;
                float remaining = fillW - consumed;
                if (remaining <= 0.0f) break;       /* tiles seguintes ficam ocultos */
                if (tile->texId < 0) { consumed += tileW; continue; }
                int tw = Texture_GetWidth(tile->texId);  if (tw <= 0) tw = 256;
                int th = Texture_GetHeight(tile->texId); if (th <= 0) th = 256;
                float u1 = tile->u1 * (float)tw;
                float v1 = tile->v1 * (float)th;
                float u2 = tile->u2 * (float)tw;
                float v2 = tile->v2 * (float)th;
                float w_draw = (remaining < tileW) ? remaining : tileW;
                float frac   = w_draw / tileW;          /* 0-1 dentro deste tile */
                float uRight = u1 + (u2 - u1) * frac;   /* UV proporcional */
                Texture_DrawUV(tile->texId, (float)tile->srcX, (float)tile->srcY,
                               w_draw, (float)tile->srcH,
                               u1, v1, uRight, v2, 1.0f, 1.0f, 1.0f, 1.0f);
                consumed += tileW;
            }
        }

        /* W05 glow — mesma lógica que single/halfdouble */
        if (sprLifeGlow >= 0) {
            #define DRAW_GLOW_DN(R, G, B, A) do { \
                int _cnt = sprTileCount(sprLifeGlow); \
                for (int _t = 0; _t < _cnt; _t++) { \
                    int _idx = sprLifeGlow + _t; \
                    SPRTileDef* _gt = &g_game.sprTiles[_idx]; \
                    if (_gt->texId < 0) continue; \
                    int _tw = Texture_GetWidth(_gt->texId);  if (_tw <= 0) _tw = 256; \
                    int _th = Texture_GetHeight(_gt->texId); if (_th <= 0) _th = 256; \
                    Texture_DrawUV(_gt->texId, (float)_gt->srcX, (float)_gt->srcY, \
                                   (float)_gt->srcW, (float)_gt->srcH, \
                                   _gt->u1*(float)_tw, _gt->v1*(float)_th, \
                                   _gt->u2*(float)_tw, _gt->v2*(float)_th, (R),(G),(B),(A)); \
                } \
            } while(0)
            /* Branco: barra cheia → pisca a cada 3 frames */
            if (lifeIsFullDN && (g_game.frameCounter % 3 == 0)) {
                DRAW_GLOW_DN(1.0f, 1.0f, 1.0f, 0.9f);
            }
            /* Vermelho: vida em perigo → pisca a cada 2 frames */
            if (lifeValDN < LIFE_DANGER && (g_game.frameCounter & 1) == 0) {
                DRAW_GLOW_DN(1.0f, 0.0f, 0.0f, 0.8f);
            }
            #undef DRAW_GLOW_DN
        }

        /* W03 border — por último, sobrepõe fill e glow (igual single mode) */
        if (sprLifeBord >= 0) {
            int cnt = sprTileCount(sprLifeBord);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = sprLifeBord + t;
                SPRTileDef* bt = &g_game.sprTiles[idx];
                float bsx = (float)bt->srcX;
                float bsy = (float)bt->srcY;
                float bsw = (float)bt->srcW;
                float bsh = (float)bt->srcH;
                Sprite_DrawTileUV(idx, bsx + bsw / 2.0f, bsy + bsh / 2.0f, bsw, bsh, 1.0f);
            }
        }
    } else {
    for (int p = pRend0; p < pRend1; p++) {
        float px = (p == 0) ? 0.0f : 320.0f;

        int lifeValP = g_game.stats.life[p];

        /* Fórmula subtrativa original Ghidra (array 0x442758):
         * displayF = clamp(life/LIFE_INITIAL - (1-bpmTiming)*0.05, 0, 1)
         * bpmTiming cai de 1.0 (inicio do beat) a 0.0 (fim do beat).
         * Com vida baixa o clamp em 0 impede que o pulse apareca — comportamento original. */
        static const float bpmTimingArr[60] = {
            1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f, /* 0-9  */
            0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f,0.6f, /* 10-19 */
            0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f, /* 20-29 */
            0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f, /* 30-39 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f, /* 40-49 */
            0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f  /* 50-59 */
        };
        float bpmTimingP = 1.0f;
        if (g_chart && g_songLoaded && (float)g_songTime > 0.1f) {
            float curBpm = (float)g_chart->segments[0].bpm;
            double acc = g_chartDelay;
            for (int s = 0; s < g_chart->segmentCount; s++) {
                double segDur = g_chart->segments[s].rowCount * getSegmentSpr(s) + getSegmentDelay(s);
                if (g_songTime < acc + segDur || s == g_chart->segmentCount - 1) {
                    curBpm = (float)g_chart->segments[s].bpm;
                    break;
                }
                acc += segDur;
            }
            if (curBpm > 0.0f) {
                float beatPeriodSec    = 60.0f / curBpm;
                float beatPeriodFrames = beatPeriodSec * 60.0f;
                float frameInBeat      = fmodf((float)g_songTime * 60.0f, beatPeriodFrames);
                int   beatIdx          = (int)(frameInBeat / beatPeriodFrames * 60.0f);
                if (beatIdx < 0)  beatIdx = 0;
                if (beatIdx > 59) beatIdx = 59;
                bpmTimingP = bpmTimingArr[beatIdx];
            }
        }
        float displayF = (float)lifeValP / (float)LIFE_INITIAL - (1.0f - bpmTimingP) * 0.05f;
        if (displayF < 0.0f) displayF = 0.0f;
        if (displayF > 1.0f) displayF = 1.0f;

        float scaled = displayF * (-256.0f);
        int rounded = (int)(scaled > 0.0f ? scaled + 0.5f : scaled - 0.5f);
        int roundedAbs = (rounded >= 0) ? rounded : -rounded;
        int quotient = roundedAbs / 6;
        int iVar5 = 253 - quotient * 6;
        if (iVar5 < 1) iVar5 = 1;

        bool lifeIsFull = (iVar5 < 2);
        float lifePct = (256.0f - (float)iVar5) / 255.0f;

        /* Half-double: fill via ST02.png — HD04.SPR hipotetico:
         * seg1: srcX=172 srcY=9 srcW=154 srcH=14  tex(0,144,154,158)
         * seg2: srcX=325 srcY=9 srcW=145 srcH=14  tex(0,160,145,174)
         * displayW proporcional a vida (0-299). Texture_DrawUV espera pixels. */
        if (isHalfDouble && g_fontSpr04 >= 0 && g_fontSpr03 >= 0 && sprLifeBord >= 0) {
            int texHd = g_game.sprTiles[sprLifeBord].texId;
            if (texHd >= 0) {
                int th = Texture_GetHeight(texHd); if (th <= 0) th = 256;
                float fillOffX = (float)(g_game.sprTiles[sprLifeBord].srcX - g_game.sprTiles[g_fontSpr03].srcX);
                float fillX = px + fillOffX + (float)g_game.sprTiles[g_fontSpr04].srcX - 2.0f;
                float fillY = (float)g_game.sprTiles[g_fontSpr04].srcY;
                int displayW = (int)(displayF * (154.0f + 145.0f) + 0.5f);
                int seg1w = (displayW > 154) ? 154 : displayW;
                if (seg1w > 0) {
                    Texture_DrawUV(texHd, fillX, fillY, (float)seg1w, 14.0f,
                        0, (float)(th - 1 - 158), (float)seg1w, (float)(th - 1 - 144), 1,1,1,1);
                }
                if (displayW > 154) {
                    int seg2w = displayW - 154;
                    Texture_DrawUV(texHd, fillX + 153.0f, fillY, (float)seg2w, 14.0f,
                        0, (float)(th - 1 - 174), (float)seg2w, (float)(th - 1 - 160), 1,1,1,1);
                }
            }
        } else if (g_fontSpr04 >= 0) {  /* 04.SPR fill — non-coop */
            int cnt = sprTileCount(g_fontSpr04);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = g_fontSpr04 + t;
                SPRTileDef* tile = &g_game.sprTiles[idx];
                float sx = px + (float)tile->srcX - (p == 0 ? 2.0f : 0.0f);
                float sy = (float)tile->srcY;
                float sw = (float)tile->srcW;
                float sh = (float)tile->srcH;
                if (tile->texId < 0) continue;
                int tw = Texture_GetWidth(tile->texId);
                int th = Texture_GetHeight(tile->texId);
                if (tw <= 0) tw = 256;
                if (th <= 0) th = 256;
                float u1 = (float)tile->u1 * (float)tw;
                float v1 = (float)tile->v1 * (float)th;
                float u2 = (float)tile->u2 * (float)tw;
                float v2 = (float)tile->v2 * (float)th;
                /* P1: ancora u1 (valor alto = u=1.0 do atlas) na DIREITA — ponta vermelha fica à direita.
                 * Ghidra P1: right x=256 u=1.0 FIXO; left x=iVar5 u=iVar5/256 (crescente L→R).
                 * Atlas armazena o sprite espelhado (u1>u2): u1=atlas-right=borda vermelha, u2=atlas-left.
                 * Para display correto: uLeft de u2 até u1, crescente da esquerda para a direita. */
                float x_draw = sx + sw * (1.0f - lifePct);
                float w_draw = sw * lifePct;
                float uLeft  = u2 + (u1 - u2) * (1.0f - lifePct);
                if (w_draw <= 0.0f) continue;
                Texture_DrawUV(tile->texId, x_draw, sy, w_draw, sh,
                              uLeft, v1, u1, v2, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }
        /* 05.SPR glow — Ghidra: MESMO sprite (0x9e0250) para BRANCO e VERMELHO.
         * g_nP1Connected = frame counter simples (++no fim de GameplayUpdate).
         * BRANCO: iVar5<2 (barra cheia) && g_nP1Connected%3==0 → a cada 3 frames, cor branca.
         * VERMELHO: HP<0xb4=180 && (g_nP1Connected&1)==0 → a cada 2 frames, cor vermelha. */
        if (sprLifeGlow >= 0) {
            /* Renderizar helper inline para não duplicar código */
            #define DRAW_GLOW(R, G, B, A) do { \
                int _cnt = sprTileCount(sprLifeGlow); \
                for (int _t = _cnt - 1; _t >= 0; _t--) { \
                    int _idx = sprLifeGlow + _t; \
                    SPRTileDef* _gt = &g_game.sprTiles[_idx]; \
                    if (_gt->texId < 0) continue; \
                    int _tw = Texture_GetWidth(_gt->texId); if (_tw <= 0) _tw = 256; \
                    int _th = Texture_GetHeight(_gt->texId); if (_th <= 0) _th = 256; \
                    Texture_DrawUV(_gt->texId, px+(float)_gt->srcX, (float)_gt->srcY, \
                                   (float)_gt->srcW, (float)_gt->srcH, \
                                   _gt->u1*(float)_tw, _gt->v1*(float)_th, \
                                   _gt->u2*(float)_tw, _gt->v2*(float)_th, (R),(G),(B),(A)); \
                } \
            } while(0)

            /* Branco: vida cheia (lifePct≈1.0) → flash a cada 3 frames */
            if (lifeIsFull && (g_game.frameCounter % 3 == 0)) {
                DRAW_GLOW(1.0f, 1.0f, 1.0f, 0.9f);
            }
            /* Vermelho: vida em perigo (HP < LIFE_DANGER=180) → flash a cada 2 frames */
            if (g_game.stats.life[p] < LIFE_DANGER && (g_game.frameCounter & 1) == 0) {
                DRAW_GLOW(1.0f, 0.0f, 0.0f, 0.8f);
            }
            #undef DRAW_GLOW
        }
        /* 03.SPR border — renderizado por último, sobrepõe fill e glow */
        if (sprLifeBord >= 0) {
            int cnt = sprTileCount(sprLifeBord);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = sprLifeBord + t;
                SPRTileDef* bt = &g_game.sprTiles[idx];
                float bsx = px + (float)bt->srcX;
                float bsy = (float)bt->srcY;
                float bsw = (float)bt->srcW;
                float bsh = (float)bt->srcH;
                Sprite_DrawTileUV(idx, bsx + bsw / 2.0f, bsy + bsh / 2.0f, bsw, bsh, 1.0f);
            }
        }
        }  // end for p (life bars)
    }

    // Explosao: seta congelada + ARROWF (depois de tudo, sobrepoe tudo)
    for (int pe = pRend0; pe < pRend1; pe++) {
        float expPosX[10];
        int expPanels;
        if (isHalfDouble) {
            expPanels = 6;
            for (int i = 0; i < 6; i++) expPosX[i] = 171.0f + i * 48.0f + (i >= 3 ? 7.0f : 0.0f);
        } else if (isDoubleOrNightmare) {
            expPanels = 10;
            for (int i = 0; i < 5; i++) expPosX[i] = 74.0f + i * 48.0f;
            for (int i = 5; i < 10; i++) expPosX[i] = 323.0f + (i-5) * 48.0f;
        } else {
            expPanels = 5;
            if (pe == 1) {
                for (int i = 0; i < 5; i++) expPosX[i] = 358.0f + i * 48.0f;
            } else {
                for (int i = 0; i < 5; i++) expPosX[i] = 38.0f + i * 48.0f;
            }
        }
        float erY = 38.0f + 28.0f;
        static const float expOffXReg[5] = {-7.0f, -6.0f, -5.0f, -6.0f, -7.0f};
        static const float expOffXHD[6]  = {-5.0f, -6.0f, -7.0f, -7.0f, -6.0f, -5.0f};
        for (int pan = 0; pan < expPanels; pan++) {
            if (g_noteState[pe][pan] != 1) continue;
            int base;
            if (isHalfDouble) {
                int arrow = (pan == 0 || pan == 5) ? 2 : (pan == 1) ? 3 : (pan == 2) ? 4 : (pan == 3) ? 0 : 1;
                base = (arrow == 0) ? g_fontArrow542 :
                       (arrow == 1) ? g_fontArrow541 :
                       (arrow == 2) ? g_fontArrow545 :
                       (arrow == 3) ? g_fontArrow543 :
                       (arrow == 4) ? g_fontArrow544 : -1;
            } else {
                int arrowIdx = isDoubleOrNightmare ? (pan % 5) : pan;
                base = (arrowIdx == 0) ? g_fontArrow542 :
                       (arrowIdx == 1) ? g_fontArrow541 :
                       (arrowIdx == 2) ? g_fontArrow545 :
                       (arrowIdx == 3) ? g_fontArrow543 :
                                         g_fontArrow544;
            }
            if (base < 0) continue;
            float ef = (float)g_noteExplodeFrame[pe][pan];
            float eAlpha = ef < 20.0f ? 1.0f : 1.0f - (ef - 19.0f) / 5.0f;
            int af = (g_game.frameCounter / 3) % 6;
            int aSpr = base + af;
            float sw = (float)g_game.sprTiles[aSpr].srcW;
            float sh = (float)g_game.sprTiles[aSpr].srcH;
            Sprite_DrawTileUV(aSpr, expPosX[pan] + sw / 2.0f, erY, sw, sh, eAlpha);
            if (g_fontArrowF >= 0) {
                int fCnt = sprTileCount(g_fontArrowF);
                if (fCnt > 0) {
                    int fArrow;
                    if (isHalfDouble) {
                        fArrow = (pan == 0 || pan == 5) ? 2 : (pan == 1) ? 3 : (pan == 2) ? 4 : (pan == 3) ? 0 : 1;
                    } else if (isDoubleOrNightmare) {
                        fArrow = pan % 5;
                    } else {
                        fArrow = pan;
                    }
                    int fIdx = fArrow < fCnt ? fArrow : 0;
                    int fSpr = g_fontArrowF + fIdx;
                    float fw = (float)g_game.sprTiles[fSpr].srcW;
                    float fh = (float)g_game.sprTiles[fSpr].srcH;
                    float esc = 0.8f + ef * 0.02f;
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    Sprite_DrawTileUV(fSpr, expPosX[pan] + expOffXReg[fArrow] + fw / 2.0f, erY, fw * esc, fh * esc, eAlpha);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
            }
        }

        /* ARROWF.SPR — brilho circular aditivo em PERFECT/GREAT (por player). */
        if (g_fontArrowF >= 0) {
            int fCnt = sprTileCount(g_fontArrowF);
            if (fCnt > 0) {
                float erY = 38.0f + 28.0f;
                for (int pan = 0; pan < expPanels; pan++) {
                    int ht = g_glowTimer[pe][pan];
                    if (ht <= 0) continue;
                    float alpha2 = (ht <= 4) ? (float)ht / 4.0f : 1.0f;
                    int arrowType;
                    if (isHalfDouble) {
                        arrowType = (pan == 0 || pan == 5) ? 2 : (pan == 1) ? 3 : (pan == 2) ? 4 : (pan == 3) ? 0 : 1;
                    } else if (isDoubleOrNightmare) {
                        arrowType = pan % 5;
                    } else {
                        arrowType = pan;
                    }
                    int fIdx = arrowType < fCnt ? arrowType : 0;
                    int fSpr = g_fontArrowF + fIdx;
                    if (fSpr >= g_game.sprTileCount) continue;
                    float fw = (float)g_game.sprTiles[fSpr].srcW;
                    float fh = (float)g_game.sprTiles[fSpr].srcH;
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    Sprite_DrawTileUV(fSpr, expPosX[pan] + fw / 2.0f, erY, fw, fh, alpha2);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
            }
        }
    }

    /* Timer regressivo - desativado
    {
        double totalSec = g_totalSongSeconds;
        if (totalSec <= 0) totalSec = g_songTime + 30.0;
        double secLeft = totalSec - g_songTime;
        if (secLeft < 0) secLeft = 0;
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%.0f:%02.0f", secLeft/60, fmod(secLeft, 60));
        Font_DrawStringCentered(g_game.screenWidth/2, g_game.screenHeight/2 - 40, timeBuf, 1, 1, 1, 0.7f);
        Font_DrawStringCentered(g_game.screenWidth/2, 10, timeBuf, 0.7f, 0.7f, 0.7f, 1.0f);
    }
    */

    if (anyAutoPanel()) {
        char buf[64] = {0}; int pos = 0;
        int apAn = isDoubleOrNightmare ? 10 : (isHalfDouble ? 6 : 5);
        for (int a = 0; a < apAn; a++)
            if (g_autoPanel[a]) {
                int l = snprintf(buf+pos, sizeof(buf)-pos, "%d ", a);
                if (l > 0) pos += l;
            }
        Font_DrawStringCentered(g_game.screenWidth/2, 28, buf, 0, 1, 0, 0.7f);
    }
}


// Original combo rendering functions from PUMPY.EXE

// FUN_00411b40: Main combo rendering function
void FUN_00411b40(int comboValue)
{
    // Bind the font texture (original uses DAT_0079e70c)
    Texture_Bind(g_fontTexId);
    
    // Special cases for combo comparison sprites (1000, 2000, 3000)
    if (comboValue == 1000) {
        // Player 1 has higher combo - show "COMBO" sprite
        glTranslatef(0x43800000, 0, 0);  // X position from original
        FUN_00411a90(0);  // Special sprite type 0
        return;
    }
    
    if (comboValue == 2000) {
        // Both players have same combo - show "MAX COMBO" sprite  
        glTranslatef(0x43800000, 0, 0);  // X position from original
        FUN_00411a90(1);  // Special sprite type 1
        return;
    }
    
    if (comboValue == 3000) {
        // Player 2 has higher combo - show "COMBO" sprite
        glTranslatef(0x43800000, 0, 0);  // X position from original
        FUN_00411a90(2);  // Special sprite type 2
        return;
    }
    
    // Regular combo numbers - break down into digits
    int digitPos = 3;  // Start with 3 digits (hundreds place)
    do {
        int digit = comboValue % 10;  // Get the rightmost digit
        FUN_004119d0(digit);         // Render the digit
        glTranslatef(0xc2080000, 0, 0);  // Move left for next digit (from original)
        digitPos--;
        comboValue = comboValue / 10;  // Remove the rightmost digit
    } while (digitPos > 0);
}

// FUN_00411a90: Special combo sprite rendering function
void FUN_00411a90(int spriteType)
{
    float u1, u2;
    
    if (spriteType == 0) {
        // COMBO sprite (Player 1 higher)
        u1 = 0x3f200000;  // 0.125f
        u2 = 0x3f480000;  // 0.28125f
    } 
    else if (spriteType == 1) {
        // MAX COMBO sprite (Both players equal)
        u1 = 0x3f480000;  // 0.28125f
        u2 = 0x3f700000;  // 0.4375f
    } 
    else if (spriteType == 2) {
        // COMBO sprite (Player 2 higher)
        u1 = 0x3f480000;  // 0.28125f
        u2 = 0x3f480000;  // 0.28125f + dynamic width
        u2 = 0.78125f - g_game.sprTiles[g_fontArrow542 + 12].srcW / 256.0f;
    }
    else {
        u2 = g_game.sprTiles[g_fontArrow542 + 12].srcW / 256.0f;
        if (spriteType == 2) {
            u1 = 0x3f480000;  // 0.28125f
            u2 = 0.78125f - g_game.sprTiles[g_fontArrow542 + 12].srcW / 256.0f;
        }
    }
    
    glBegin(GL_QUADS);
    glTexCoord2f(u1, 0x3f530000);  // V = 0.328125f (top)
    glVertex2i(0, 0x30);           // Y = 48 (bottom)
    glTexCoord2f(u1, 0x3f818000);  // V = 0.5078125f (bottom)
    glVertex2i(0, 0);              // Y = 0 (top)
    glTexCoord2f(u2, 0x3f818000);  // V = 0.5078125f (bottom)
    glVertex2i(0x28, 0);          // Y = 0 (top)
    glTexCoord2f(u2, 0x3f530000);  // V = 0.328125f (top)
    glVertex2i(0x28, 0x30);       // Y = 48 (bottom)
    glEnd();
}

// FUN_004119d0: Individual digit rendering function
void FUN_004119d0(int digit)
{
    // Bind the dec00 texture (same as original)
    Texture_Bind(g_fontDec00Id);
    
    // Original texture coordinates (5 colunas)
    float tileWidth = 0.1875f;    // 48/256
    float tileHeight = 0.203125f; // 52/256  
    float vOffset = 0.15625f;     // 40/256
    
    float u1 = (float)(digit % 5) * tileWidth;
    float v1 = (float)(digit / 5) * tileHeight + vOffset;
    float u2 = u1 + tileWidth;
    float v2 = v1 + tileHeight;
    
    glBegin(GL_QUADS);
    glTexCoord2f(u1, v1);
    glVertex2i(0, 0x2d);           // Y = 45
    glTexCoord2f(u1, v2);
    glVertex2i(0, 0);              // Y = 0
    glTexCoord2f(u2, v2);
    glVertex2i(0x2c, 0);          // Y = 0
    glTexCoord2f(u2, v1);
    glVertex2i(0x2c, 0x2d);       // Y = 45
    glEnd();
}

//force
