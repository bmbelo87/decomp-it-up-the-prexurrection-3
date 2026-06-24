#include "pumpy.h"
#include "vsl.h"

#define PANEL_SIZE 30
#define P1_CENTER_X 160
#define P2_CENTER_X 480

#define MISS_WINDOW 0.25f

#define JUDGE_PERFECT  0.055f
#define JUDGE_GREAT    0.110f
#define JUDGE_GOOD     0.165f
#define JUDGE_BAD      0.220f

#define BASE_ROW_SPACING 18.0f
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
static double g_maxSongTime;
static int g_stagnantFrames;
static uint32_t g_lastPosMs;
static int g_lastNoteRow;
static bool g_hasAudio;
static bool g_autoplay;
static bool g_autoPanel[5]; // per-panel autoplay: DL, UL, CN, UR, DR
static float g_scrollSpeedX; // current (interpolated) speed
static float g_scrollSpeedTarget; // target speed from keypress

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

static NoteHit g_noteHits[2][5][2048];
static int g_noteHitCount[2][5];
static int g_nextNoteRow[2][5];

// Hold tracking: which rows have active hold heads per player/panel
static int g_holdRows[2][5]; // row index of active hold (-1 = none)

static float g_judgeDisplayTimer[2];
static JudgeType g_judgeDisplayType[2];
static int g_judgeDisplayCombo[2];
static int g_judgeFrame[2]; // frame counter 25->0 for judge animation
static int g_hitTimer[2][5]; // hit flash animation timer (p1)

// Maquina de estados da nota
static int g_noteState[2][5]; // 0=normal, 1=exploding, 2=dead
static int g_noteExplodeRow[2][5]; // row index for clearing when dead
static int g_noteExplodeFrame[2][5]; // explosion frame counter 0..15
static int g_blindTimer[2];
static int g_prevBlindRow;
static int g_lastPerfectRow[2][5];

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
    g_scrollSpeedX = 1.0f;
    g_scrollSpeedTarget = 1.0f;

    memset(g_noteHits, 0, sizeof(g_noteHits));
    memset(g_noteHitCount, 0, sizeof(g_noteHitCount));
    memset(g_nextNoteRow, 0, sizeof(g_nextNoteRow));
    for (int p = 0; p < 2; p++)
        for (int pan = 0; pan < 5; pan++)
            g_holdRows[p][pan] = -1;

    Log_Print("GP: chart %d: BPM=%.1f subdiv=%d rows=%d panels=%d time=%.1fs spR=%.4f\n",
        g_chartIdx, bpm, subdiv, g_chart->rowCount, g_chart->panelCount, g_totalSongSeconds, g_secondsPerRow);
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

// Processa o julgamento final de uma linha
static void processRowJudgment(int player, int row, JudgeType jt) {
    int receptorY = 38;
    // So Perfect/Great consomem a nota (ela some). Good/Bad/Miss passam reto.
    if (jt == JT_PERFECT || jt == JT_GREAT) {
        StepRow* r = &g_chart->rows[row];
        for (int pan = 0; pan < 5; pan++) {
            if (getPanelValue(r, pan, player)) {
                g_noteState[player][pan] = 1; // EXPLODING
                g_noteExplodeRow[player][pan] = row;
                g_noteExplodeFrame[player][pan] = 0;
            }
            g_lastPerfectRow[player][pan] = row;
        }
        memset(r, 0, sizeof(StepRow)); // Limpa a linha pra nao conflitar com processamento
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
        if (g_pending[i].hitMask == g_pending[i].totalMask)
            jt = evaluateTiming(g_pending[i].worstDiff);
        else
            jt = JT_MISS;
        // Row com HH/HB/HT nao capturado
        for (int pan = 0; pan < 5; pan++) {
            uint8_t pv = getPanelValue(&g_chart->rows[g_pending[i].row], pan, player);
            if (!pv) continue;
            if ((pv == NT_HOLD_H || pv == NT_HOLD_B || pv == NT_HOLD_T) && g_holdRows[player][pan] < 0) {
                static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                if (!Input_IsPadDown(player, btnMap[pan]))
                    jt = JT_MISS;
                else {
                    g_holdRows[player][pan] = g_pending[i].row;
                    StepHalf* hh = (player == 0) ? &g_chart->rows[g_pending[i].row].half1 : &g_chart->rows[g_pending[i].row].half2;
                    switch (pan) { case 0: hh->dl = 0; break; case 1: hh->ul = 0; break; case 2: hh->cn = 0; break; case 3: hh->ur = 0; break; case 4: hh->dr = 0; break; }
                }
            }
        }
        // Setar hold rows para hold heads na linha (mesmo em MISS)
        for (int pan = 0; pan < 5; pan++)
            if (getPanelValue(&g_chart->rows[g_pending[i].row], pan, player) == NT_HOLD_H)
                g_holdRows[player][pan] = g_pending[i].row;
        for (int pan = 0; pan < 5; pan++) {
            uint8_t pv = getPanelValue(&g_chart->rows[g_pending[i].row], pan, player);
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

static void processInput(int player)
{
    if (!g_songLoaded) return;

    // Depois: isNewHit para taps (hold heads sao capturados pelo processHolds)
    static int logCtr = 0; logCtr++;
    for (int b = 0; b < PAD_BUTTONS_PER_PLAYER; b++)
    {
        int panel = getPanelForButton((PadButton)b);
        if (panel < 0) continue;
        if (!Input_IsPadHit(player, (PadButton)b)) continue;
        g_hitTimer[player][panel] = 17; // p1 borda no receptor

        double bestDiff = 999;
        int bestRow = -1;

        for (int ri = g_nextNoteRow[player][panel]; ri < (int)g_chart->rowCount; ri++)
        {
            uint8_t val = getPanelValue(&g_chart->rows[ri], panel, player);
            if (!val || val == NT_HOLD_B || val == NT_HOLD_T) continue;

            double rowTime = getRowTime(ri);
            double diff = g_songTime - rowTime;
            if (diff < -JUDGE_BAD) break;
            if (diff > JUDGE_BAD) { g_nextNoteRow[player][panel] = ri + 1; continue; }

            double ad = diff < 0 ? -diff : diff;
            if (ad < bestDiff) { bestDiff = ad; bestRow = ri; }
        }

        if (bestRow < 0) continue;

        // DEBUG: tap hit
        Log_Print("TAPHIT: p=%d pan=%d row=%d diff=%.3f\n", player, panel, bestRow, bestDiff);

        // Conta quantas setas na linha
        int arrowsInRow = 0;
        for (int pan = 0; pan < 5; pan++)
            if (getPanelValue(&g_chart->rows[bestRow], pan, player)) arrowsInRow++;

        if (arrowsInRow > 1) {
            // Pendente: aguardar todas as setas
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
                    for (int pan = 0; pan < 5; pan++) {
                        uint8_t pv = getPanelValue(&g_chart->rows[bestRow], pan, player);
                        // So contar taps (ignorar HH=10, HB=11, HT=12)
                        if (pv && pv < NT_HOLD_H)
                            g_pending[slot].totalMask |= (1 << pan);
                    }
                    g_pendingCount++;
                }
                g_pending[slot].hitMask |= (1 << panel);
                g_pending[slot].worstDiff = bestDiff;

                // Se todas apertadas, processa AGORA
                if ((g_pending[slot].hitMask & g_pending[slot].totalMask) == g_pending[slot].totalMask) {
                    g_pending[slot].active = false;
                    g_pendingCount--;
                    JudgeType pjt = evaluateTiming(g_pending[slot].worstDiff);
                    // Se row tem HH/HB/HT nao capturado
                    for (int pan = 0; pan < 5; pan++) {
                        uint8_t pv = getPanelValue(&g_chart->rows[bestRow], pan, player);
                        if (!pv) continue;
                        if ((pv == NT_HOLD_H || pv == NT_HOLD_B || pv == NT_HOLD_T) && g_holdRows[player][pan] < 0) {
                            static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                            if (!Input_IsPadDown(player, btnMap[pan]))
                                pjt = JT_MISS;
                            else {
                                g_holdRows[player][pan] = bestRow;
                                // Limpa SÓ este painel (catch visual)
                                StepHalf* hh = (player == 0) ? &g_chart->rows[bestRow].half1 : &g_chart->rows[bestRow].half2;
                                switch (pan) { case 0: hh->dl = 0; break; case 1: hh->ul = 0; break; case 2: hh->cn = 0; break; case 3: hh->ur = 0; break; case 4: hh->dr = 0; break; }
                            }
                        }
                    }
                    if (pjt == JT_PERFECT || pjt == JT_GREAT) {
                        for (int pan = 0; pan < 5; pan++)
                            if (getPanelValue(&g_chart->rows[bestRow], pan, player)) {
                                g_noteState[player][pan] = 1; // EXPLODING
                                g_noteExplodeRow[player][pan] = bestRow;
                                g_noteExplodeFrame[player][pan] = 0;
                            }
                        memset(&g_chart->rows[bestRow], 0, sizeof(StepRow));
                    }
                    g_judgeDisplayType[player] = pjt;
                    g_judgeDisplayTimer[player] = 0.6f;
                    g_judgeFrame[player] = (pjt == JT_GREAT || pjt == JT_PERFECT) ? 40 : 25;
                    { int sc = 0, cb = g_game.stats.combo[player];
                      int receptorY = 38; // Receptor Y position for combo popup calculation
                      switch (pjt) {
                        case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
                        case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
                        default: break;
} if (sc > 0) popupCreate(player, sc, cb, 178.0f); } // Y=80 (Ghidra) + 98 offset = Y=178 (game reality)
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
                    if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
                        g_game.stats.maxCombo[player] = g_game.stats.combo[player];
                }
            }
            continue;
        }

        // 1 seta: processa direto
        g_nextNoteRow[player][panel] = bestRow + 1;
        JudgeType jt = evaluateTiming(bestDiff);
        if (jt == JT_PERFECT || jt == JT_GREAT) {
            for (int pan = 0; pan < 5; pan++)
                if (getPanelValue(&g_chart->rows[bestRow], pan, player)) {
                    g_noteState[player][pan] = 1; // EXPLODING
                    g_noteExplodeRow[player][pan] = bestRow;
                    g_noteExplodeFrame[player][pan] = 0;
                }
            memset(&g_chart->rows[bestRow], 0, sizeof(StepRow));
        }
        g_judgeDisplayType[player] = jt;
        g_judgeDisplayTimer[player] = 0.6f;
        g_judgeFrame[player] = (jt == JT_GREAT || jt == JT_PERFECT) ? 40 : 25;
        { int sc = 0, cb = g_game.stats.combo[player];
          int receptorY = 38; // Receptor Y position for combo popup calculation
          switch (jt) {
            case JT_PERFECT: sc = 1000; if (cb > 3) sc += 1000; cb++; break;
            case JT_GREAT:   sc = 500;  if (cb > 3) sc += 1000; cb++; break;
            default: break;
} if (sc > 0) popupCreate(player, sc, cb, 178.0f); } // Y=80 (Ghidra) + 98 offset = Y=178 (game reality)
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
        if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
            g_game.stats.maxCombo[player] = g_game.stats.combo[player];
    }
}

static bool anyAutoPanel(void)
{
    for (int a = 0; a < 5; a++)
        if (g_autoPanel[a]) return true;
    return false;
}

static void processAutoplay(void)
{
    if (!g_songLoaded || !anyAutoPanel()) return;

    for (int p = 0; p < 1; p++)
    {
        // Collect autoplay hits per row
        int hitRows[10], hitCount = 0;
        for (int panel = 0; panel < 5; panel++)
        {
            if (!g_autoPanel[panel]) continue;

            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
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

        // Deduplicate by row (1 combo per row)
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
                // Update combo comparison variables (original logic)
                if (p == 0) {
                    g_game.stats.combo_0 = g_game.stats.combo[p];
                } else {
                    g_game.stats.combo_1 = g_game.stats.combo[p];
                }
            g_game.stats.missCombo[p] = 0;
            g_game.stats.score[p] += 1000;
            if (g_game.stats.combo[p] > 3)
                g_game.stats.score[p] += 1000;
            if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
                g_game.stats.maxCombo[p] = g_game.stats.combo[p];

            // Start holds for hold heads
            for (int panel = 0; panel < 5; panel++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[hitRows[i]], panel, p);
                if (val == NT_HOLD_H)
                    g_holdRows[p][panel] = hitRows[i];

                // Autoplay: marca row como consumida pra não renderizar
                if (val)
                    g_lastPerfectRow[p][panel] = hitRows[i];
            }

            // Autoplay: limpa SÓ os painéis com autoplay
            for (int panel = 0; panel < 5; panel++) {
                if (!g_autoPanel[panel]) continue;
                uint8_t val = getPanelValue(&g_chart->rows[hitRows[i]], panel, p);
                if (!val) continue;
                StepHalf* h = (p == 0) ? &g_chart->rows[hitRows[i]].half1 : &g_chart->rows[hitRows[i]].half2;
                switch (panel) {
                    case 0: h->dl = 0; break;
                    case 1: h->ul = 0; break;
                    case 2: h->cn = 0; break;
                    case 3: h->ur = 0; break;
                    case 4: h->dr = 0; break;
                }
            }
        }
    }
}
                
static void processHolds(void)
{
    if (!g_songLoaded) return;
    for (int p = 0; p < 1; p++)
    {
        for (int panel = 0; panel < 5; panel++)
        {
            static const PadButton panelToBtn[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
            bool held = g_autoPanel[panel] ? true : Input_IsPadDown(p, panelToBtn[panel]);

            // Auto-capture: botao segurado e HH ou HB/HT nao capturado (re-press)
            if (g_holdRows[p][panel] < 0 && held)
            {
                for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
                {
                    uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                    if (!val) continue;
                    double rt = getRowTime(ri);
                    if (g_songTime < rt - JUDGE_BAD) break; // muito no futuro
                    // So captura se for nota de hold (HH, HB, HT)
                    if (val == NT_HOLD_H || val == NT_HOLD_B || val == NT_HOLD_T) {
                        Log_Print("AUTOCAPTURE: p=%d pan=%d ri=%d val=%d held=%d\n", p, panel, ri, val, held);
                        g_holdRows[p][panel] = ri;
                        g_nextNoteRow[p][panel] = ri + 1;
                        // Limpa SÓ este painel (catch visual), deixa outros paineis (taps) intactos
                        StepHalf* hh = (p == 0) ? &g_chart->rows[ri].half1 : &g_chart->rows[ri].half2;
                        switch (panel) { case 0: hh->dl = 0; break; case 1: hh->ul = 0; break; case 2: hh->cn = 0; break; case 3: hh->ur = 0; break; case 4: hh->dr = 0; break; }
                        g_noteState[p][panel] = 1; // EXPLODING
                        g_noteExplodeRow[p][panel] = ri;
                        g_noteExplodeFrame[p][panel] = 0;
                        int hasTap = 0;
                        for (int pan = 0; pan < 5; pan++)
                            if (pan != panel && getPanelValue(&g_chart->rows[ri], pan, p)) { hasTap = 1; break; }
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

            // Scan forward from head to find body/tail rows
            { int db=0, ub=0, cb=0, urb=0, drb=0;
              if (Input_IsPadDown(p, PAD_DL)) db=1; if (Input_IsPadDown(p, PAD_UL)) ub=1;
              if (Input_IsPadDown(p, PAD_C)) cb=1; if (Input_IsPadDown(p, PAD_UR)) urb=1;
              if (Input_IsPadDown(p, PAD_DR)) drb=1;
              Log_Print("BODYSCAN: p=%d pan=%d start=%d holdRows=%d isDown=%d%d%d%d%d\n", p, panel, g_holdRows[p][panel] + 1, g_holdRows[p][panel], db, ub, cb, urb, drb); }
            for (int ri = g_holdRows[p][panel] + 1; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                if (val != 0 && val != NT_HOLD_B && val != NT_HOLD_T) break;
                if (val == 0) continue;

                double rowTime = getRowTime(ri);
                if (g_songTime < rowTime - 0.01) break;

                // Ja julgado? (por processMisses ou anterior)
                bool alreadyJudged = false;
                for (int h = 0; h < g_noteHitCount[p][panel]; h++)
                    if (g_noteHits[p][panel][h].rowIndex == ri) { alreadyJudged = true; break; }
                if (alreadyJudged) continue;

                if (!held)
                {
                    Log_Print("HOLD MISS: p=%d pan=%d ri=%d held=%d\n", p, panel, ri, held);
                    g_game.stats.combo[p] = 0;
                    g_game.stats.missCount[p]++;
                    g_game.stats.missCombo[p]++;
                }
                else
                {
                    // Verifica se tem tap na mesma row (se sim, nao da score - o tap dita)
                    bool hasUnjudgedTap = false;
                    for (int op = 0; op < 5; op++) {
                        if (op == panel) continue;
                        uint8_t ov = getPanelValue(&g_chart->rows[ri], op, p);
                        if (ov && ov != NT_HOLD_B && ov != NT_HOLD_T) { hasUnjudgedTap = true; break; }
                    }
                    // Held body sempre limpa SÓ este painel (catch visual), mesmo com tap
                    StepHalf* hh = (p == 0) ? &g_chart->rows[ri].half1 : &g_chart->rows[ri].half2;
                    switch (panel) { case 0: hh->dl = 0; break; case 1: hh->ul = 0; break; case 2: hh->cn = 0; break; case 3: hh->ur = 0; break; case 4: hh->dr = 0; break; }
                    g_noteState[p][panel] = 1; // EXPLODING
                    g_noteExplodeRow[p][panel] = ri;
                    g_noteExplodeFrame[p][panel] = 0;
                    if (!hasUnjudgedTap) {
                        Log_Print("HOLD PERF: p=%d pan=%d ri=%d held=%d\n", p, panel, ri, held);
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

    double missThreshold = g_songTime - JUDGE_BAD;
    for (int p = 0; p < 2; p++)
    {
        int missedRows[256], missCount = 0;
        for (int panel = 0; panel < 5; panel++)
        {
            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                if (!val) continue;
                if (val == NT_HOLD_H || val == NT_HOLD_B || val == NT_HOLD_T)
                {
                    if (g_holdRows[p][panel] >= 0) continue;
                    // Se botao segurado, pula tambem (auto-capture ou pending vai processar)
                    static const PadButton btnMap[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
                    if (Input_IsPadDown(p, btnMap[panel])) continue;
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
            {
                for (int pan = 0; pan < 5; pan++)
                {
                    uint8_t val = getPanelValue(&g_chart->rows[missedRows[m]], pan, p);
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
    g_game.stats.life[0] = 100;
    g_game.stats.life[1] = 100;
    g_game.stats.missCombo[0] = 0;
    g_game.stats.missCombo[1] = 0;
    memset(g_judgeDisplayTimer, 0, sizeof(g_judgeDisplayTimer));
    memset(g_hitTimer, 0, sizeof(g_hitTimer));
    memset(g_noteState, 0, sizeof(g_noteState));
    memset(g_noteExplodeFrame, 0, sizeof(g_noteExplodeFrame));
    for (int p = 0; p < 2; p++)
        for (int pan = 0; pan < 5; pan++)
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
    Log_Print("Gameplay: exit\n");
}

void Gameplay_Update(float dt)
{
    if (g_game.state != STATE_GAMEPLAY) return;
    if (!g_songLoaded) return;
    if (dt > 0.05f) dt = 0.05f;

    {
        int autoKeys[5] = { VK_F1, VK_F2, VK_F3, VK_F4, VK_F5 };
        static const char* autoNames[5] = { "DL", "UL", "CN", "UR", "DR" };
        for (int a = 0; a < 5; a++)
        {
            if (Input_IsKeyHit(autoKeys[a]))
            {
                g_autoPanel[a] = !g_autoPanel[a];
                Log_Print("GP: autoplay %s %s\n", autoNames[a], g_autoPanel[a] ? "ON" : "OFF");
            }
        }
        g_autoplay = g_autoPanel[0] && g_autoPanel[1] && g_autoPanel[2] && g_autoPanel[3] && g_autoPanel[4];
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
    //processInput(1);  // P2 desativado
    processPendingRows(0);
    //processPendingRows(1);
    processAutoplay();
    processHolds();
    processMisses();

    for (int p = 0; p < 2; p++)
    {
        if (g_judgeDisplayTimer[p] > 0)
            g_judgeDisplayTimer[p] -= dt;
        if (g_judgeFrame[p] > 0)
            g_judgeFrame[p]--;
        for (int pan = 0; pan < 5; pan++) {
            if (g_hitTimer[p][pan] > 0)
                g_hitTimer[p][pan]--;
            if (g_noteState[p][pan] == 1) { // EXPLODING
                g_noteExplodeFrame[p][pan]++;
                if (g_noteExplodeFrame[p][pan] >= 15)
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
    int scrollBottom = 480;
    float pixelsPerRow = BASE_ROW_SPACING * g_scrollSpeedX;
    double scrollRow = getRowAtTimeFloat(g_songTime);

    // Find current segment for dynamic scroll speed (judgment zone sizing)
    double currentSpr = g_secondsPerRow;
    for (int s = g_chart->segmentCount - 1; s >= 0; s--)
    {
        if (g_songTime >= getRowTime(g_chart->segments[s].rowStart))
        {
            currentSpr = getSegmentSpr(s);
            break;
        }
    }
    float currentPixelsPerSec = (float)(pixelsPerRow / currentSpr);

    // Determine visible row range
    int startRow = (int)scrollRow - (int)(480 / pixelsPerRow) - 2;
    if (startRow < 0) startRow = 0;
    int endRow = (int)scrollRow + (int)((scrollBottom - receptorY) / pixelsPerRow) + 4;
    if (endRow >= (int)g_chart->rowCount) endRow = g_chart->rowCount - 1;

    // Compute judgment zone half-heights using current BPM-based scroll speed
    float jZoneHalf[4];
    float jWindows[4] = { JUDGE_BAD, JUDGE_GOOD, JUDGE_GREAT, JUDGE_PERFECT };
    for (int j = 0; j < 4; j++)
        jZoneHalf[j] = jWindows[j] * currentPixelsPerSec;

    for (int p = 0; p < 1; p++)
    {
        int centerX = (p == 0) ? P1_CENTER_X : P2_CENTER_X;
        int baseX = centerX - 80;

        float posX[5];
        int pW[5] = {54, 54, 54, 54, 54};
        posX[0] = 38.0f;  // DL
        posX[1] = posX[0] + 48.0f;  // UL
        posX[2] = posX[1] + 48.0f;  // CN
        posX[3] = posX[2] + 48.0f;  // UR
        posX[4] = posX[3] + 48.0f;  // DR

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
        if (g_fontSpr01 >= 0) {
            int cnt = sprTileCount(g_fontSpr01);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = g_fontSpr01 + t;
                float sx = (float)g_game.sprTiles[idx].srcX;
                float sy = (float)g_game.sprTiles[idx].srcY;
                float sw = (float)g_game.sprTiles[idx].srcW;
                float sh = (float)g_game.sprTiles[idx].srcH;
                Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, 1.0f);
            }
        }
        // 02.SPR blind (g_fontSpr02) — também antes das notas
        if (g_fontSpr02 >= 0 && g_blindTimer[p] > 0) {
            float blindA = (float)g_blindTimer[p] / 10.0f;
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            int cnt = sprTileCount(g_fontSpr02);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = g_fontSpr02 + t;
                float sx = (float)g_game.sprTiles[idx].srcX;
                float sy = (float)g_game.sprTiles[idx].srcY;
                float sw = (float)g_game.sprTiles[idx].srcW;
                float sh = (float)g_game.sprTiles[idx].srcH;
                Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, blindA);
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // Hit flash (p1 tile) nos receptores acertados
        static int kHitBaseInit = 0;
        static int kHitBase[5];
        if (!kHitBaseInit) {
            kHitBase[0] = g_fontArrow542;
            kHitBase[1] = g_fontArrow541;
            kHitBase[2] = g_fontArrow545;
            kHitBase[3] = g_fontArrow543;
            kHitBase[4] = g_fontArrow544;
            kHitBaseInit = 1;
        }
        for (int pan = 0; pan < 5; pan++) {
            int ht = g_hitTimer[p][pan];
            if (ht <= 0 || kHitBase[pan] < 0) continue;
            int p1Idx = kHitBase[pan] + 6;
            if (p1Idx >= g_game.sprTileCount) continue;
            float sw = (float)g_game.sprTiles[p1Idx].srcW;
            float sh = (float)g_game.sprTiles[p1Idx].srcH;
            static const float p1OffX[5] = {-7.0f, -6.0f, -5.0f, -6.0f, -7.0f};
            float startScale = 54.0f / sw;
            float sc, alpha;
            if (ht > 2) {
                float t = 1.0f - (float)(ht - 2) / 15.0f;
                sc = startScale + (1.0f - startScale) * t;
                alpha = 1.0f;
            } else {
                sc = 1.0f;
                alpha = (float)ht / 2.0f;
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            Sprite_DrawTileUV(p1Idx, posX[pan] + p1OffX[pan] + sw / 2.0f, (float)(receptorY + 28), sw * sc, sh * sc, alpha);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        int rh = 57;
        // Notas (scroll do fundo para o topo)
        int rh2 = 57;
        static const int kPanelOrder[5] = {1, 3, 0, 2, 4};
        static const int kBodyTile[5] = {12, 16, 20, 18, 14};
        static const int kTailTile[5] = {13, 17, 21, 19, 15};
        static const float kBodyOffX[5] = {-4.0f, -3.0f, 0.0f, 3.0f, 4.0f};

        // Pass 0: Hold bodies (esticados entre runs de NT_HOLD_B)
        if (g_fontArrowETC >= 0) {
            for (int panel = 0; panel < 5; panel++)
            {
                for (int ri = startRow; ri <= endRow; ri++)
                {
                    uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                    if (val != NT_HOLD_B) continue;
                    if (ri == g_lastPerfectRow[p][panel]) continue;

                    int endRi = ri + 1;
                    while (endRi < (int)g_chart->rowCount) {
                        uint8_t nv = getPanelValue(&g_chart->rows[endRi], panel, p);
                        if (nv != NT_HOLD_B) break;
                        endRi++;
                    }

                    float y1 = (float)(receptorY + rh2 / 2 + (ri - scrollRow) * pixelsPerRow);
                    float y2 = (float)(receptorY + rh2 / 2 + (endRi - scrollRow) * pixelsPerRow);
                    if (y2 < y1) { float t = y1; y1 = y2; y2 = t; } // garantir Y1 < Y2 (Y-UP)
                    float totalH = y2 - y1;
                    if (totalH <= 0) { ri = endRi - 1; continue; }

                    int idx = g_fontArrowETC + kBodyTile[panel];
                    float sw = (float)g_game.sprTiles[idx].srcW;
                    Sprite_DrawTileUV(idx, posX[panel] + sw / 2.0f + kBodyOffX[panel], y1 + totalH / 2.0f, sw, totalH, 1.0f);
                    ri = endRi - 1;
                }
            }
        }

        // Pass 1: Hold tails
        for (int ri = startRow; ri <= endRow; ri++)
        {
            if (g_fontArrowETC < 0) break;
            float y = (float)(receptorY + rh2 / 2 + (ri - scrollRow) * pixelsPerRow);
            if (y < receptorY - rh2 / 2 - 50 || y > scrollBottom + PANEL_SIZE) continue;
            for (int rio = 0; rio < 5; rio++)
            {
                int panel = kPanelOrder[rio];
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                if (val != NT_HOLD_T) continue;
                int idx = g_fontArrowETC + kTailTile[panel];
                float sw = (float)g_game.sprTiles[idx].srcW;
                float sh = (float)g_game.sprTiles[idx].srcH;
                Sprite_DrawTileUV(idx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
            }
        }
        // Pass 2: Taps/HoldHeads
        for (int ri = startRow; ri <= endRow; ri++)
        {
            float y = (float)(receptorY + rh2 / 2 + (ri - scrollRow) * pixelsPerRow);
            if (y < receptorY - rh2 / 2 - 50 || y > scrollBottom + PANEL_SIZE) continue;
            for (int rio = 0; rio < 5; rio++)
            {
                int panel = kPanelOrder[rio];
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                if (!val || val == NT_HOLD_B || val == NT_HOLD_T) continue;

                if (panel == 0 && g_fontArrow542 >= 0) {
                    int af = (g_game.frameCounter / 3) % 6;
                    int aidx = g_fontArrow542 + af;
                    float sw = (float)g_game.sprTiles[aidx].srcW;
                    float sh = (float)g_game.sprTiles[aidx].srcH;
                    Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 1 && g_fontArrow541 >= 0) {
                    int af = (g_game.frameCounter / 3) % 6;
                    int aidx = g_fontArrow541 + af;
                    float sw = (float)g_game.sprTiles[aidx].srcW;
                    float sh = (float)g_game.sprTiles[aidx].srcH;
                    Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 2 && g_fontArrow545 >= 0) {
                    int af = (g_game.frameCounter / 3) % 6;
                    int aidx = g_fontArrow545 + af;
                    float sw = (float)g_game.sprTiles[aidx].srcW;
                    float sh = (float)g_game.sprTiles[aidx].srcH;
                    Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 3 && g_fontArrow543 >= 0) {
                    int af = (g_game.frameCounter / 3) % 6;
                    int aidx = g_fontArrow543 + af;
                    float sw = (float)g_game.sprTiles[aidx].srcW;
                    float sh = (float)g_game.sprTiles[aidx].srcH;
                    Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 4 && g_fontArrow544 >= 0) {
                    int af = (g_game.frameCounter / 3) % 6;
                    int aidx = g_fontArrow544 + af;
                    float sw = (float)g_game.sprTiles[aidx].srcW;
                    float sh = (float)g_game.sprTiles[aidx].srcH;
                    Sprite_DrawTileUV(aidx, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                // SPRs cobrem todos os tipos de nota
            }
        }

        int centerY = g_game.screenHeight / 2;
    int receptorY = 38; // Same as in rendering loop


        // Mode/Modifier sprites do ARROW541.SP2
        if (g_fontArrow541 >= 0) {
            const char* modeName = (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount) ? g_game.songDB.modes[g_game.selectedModeIndex].name : "EASY";
            int modeOff = 31; // modeez (default)
            if (strcmp(modeName, "HARD") == 0) modeOff = 32;
            else if (strcmp(modeName, "CRAZY") == 0) modeOff = 33;
            int modeIdx = g_fontArrow541 + modeOff;
            if (modeIdx < g_game.sprTileCount) {
                float sw = (float)g_game.sprTiles[modeIdx].srcW;
                float sh = (float)g_game.sprTiles[modeIdx].srcH;
                Sprite_DrawTileUV(modeIdx, 18 + sw/2, 152 + sh/2, sw, sh, 1.0f);
            }
            int speedOff = 36; // accel1
            if (g_scrollSpeedTarget >= 4.0f) speedOff = 9; // accel4
            else if (g_scrollSpeedTarget >= 3.0f) speedOff = 8; // accel3
            else if (g_scrollSpeedTarget >= 2.0f) speedOff = 7; // accel2
            int speedIdx = g_fontArrow541 + speedOff;
            if (speedIdx < g_game.sprTileCount) {
                float sw = (float)g_game.sprTiles[speedIdx].srcW;
                float sh = (float)g_game.sprTiles[speedIdx].srcH;
                Sprite_DrawTileUV(speedIdx, 18 + sw/2, 184 + sh/2, sw, sh, 1.0f);
            }
            int disOffsets[4] = { 34, 35, 37, 38 };
            float disY[4] = { 216, 248, 280, 312 };
            for (int di = 0; di < 4; di++) {
                int didx = g_fontArrow541 + disOffsets[di];
                if (didx < g_game.sprTileCount) {
                    float sw = (float)g_game.sprTiles[didx].srcW;
                    float sh = (float)g_game.sprTiles[didx].srcH;
                    Sprite_DrawTileUV(didx, 18 + sw/2, (float)(disY[di] + sh/2), sw, sh, 1.0f);
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
                showCombo = comboVal > 0;
            } else {
                comboVal = g_game.stats.combo[p];
                showCombo = comboVal > 1;
            }

            if (showCombo)
            {
                // O combo herda o uniformScale + squeezeX (mas SEM o 0.8x do sprite interno)
                float comboScaleX = uniformScale * squeezeX;
                float comboScaleY = uniformScale;
                // O Y do combo tambem anima: original faz glTranslatef(0,-70,0) dentro do scaled space
                float comboAnimY = (float)centerY + 70.0f * uniformScale;

                float glyphW = 48.0f * comboScaleY;
                float spacing = 44.0f * comboScaleX; // glTranslatef(-40) * scale(1.1) = 44px entre bordas esquerdas
                int d3 = comboVal % 10;
                int d2 = (comboVal / 10) % 10;
                int d1 = comboVal / 100;
                // Tens centralizado, units/combo seguem o espacamento do original (edge-to-edge com 4px overlap)
                float dy = (float)(centerY + 42) + 70.0f * (uniformScale - 1.0f);
                dy -= 22.5f * 1.1f * comboScaleY; // correcao meia altura do glifo (45/2 * 1.1x)
                float tensLeft = (float)centerX - glyphW / 2.0f;
                float ux = tensLeft + spacing;
                float hx = tensLeft - spacing;
                float cr = (jt == JT_MISS) ? 1.0f : 1.0f;
                float cg = (jt == JT_MISS) ? 0.3f : 1.0f;
                float cb = (jt == JT_MISS) ? 0.3f : 1.0f;
                Font_DrawDecDigit(g_fontDec00Id, ux, dy, d3, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);
                Font_DrawDecDigit(g_fontDec00Id, tensLeft, dy, d2, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);
                Font_DrawDecDigit(g_fontDec00Id, hx, dy, d1, spriteAlpha, comboScaleX, comboScaleY, cr, cg, cb);
                // "COMBO" sprite (tambem animado)
                if (g_fontArrow542 >= 0) {
                    int comboIdx = g_fontArrow542 + g_judgeSpriteIndices[5] + 1;
                    if (comboIdx < g_game.sprTileCount) {
                        float sw = (float)g_game.sprTiles[comboIdx].srcW * comboScaleX * 0.8f;
                        float sh = (float)g_game.sprTiles[comboIdx].srcH * comboScaleY * 0.8f;
                        // Centro do TENS = centerX. 35px abaixo do centro do TENS em Y-DOWN
                        float tensCenterY = dy + (49.5f * comboScaleY) * 0.5f;
                        float comboY = tensCenterY + 35.0f;
                        Sprite_DrawTileUV(comboIdx, (float)centerX, comboY, sw, sh, spriteAlpha * 0.7f);
                    }
                }
            }
        }  // end judge/combo

        // Pop-up de score (catch effect)
        for (int i = 0; i < MAX_POPUPS; i++) {
            if (!g_popups[i].active || g_popups[i].player != p) continue;
            char buf[32];
            int centerX = (p == 0) ? 160 : 480;
            snprintf(buf, sizeof(buf), "+%d", g_popups[i].score);
            Font_DrawStringCenteredScaled(centerX, (int)g_popups[i].y, buf, 1,1,0, g_popups[i].alpha, 1.2f);
            if (g_popups[i].combo > 1) {
                snprintf(buf, sizeof(buf), "%d", g_popups[i].combo);
                Font_DrawStringCenteredScaled(centerX, (int)g_popups[i].y - 16, buf, 1,1,1, g_popups[i].alpha * 0.7f, 0.8f);
            }
        }
    }  // end for p

    // Life bars (03/04/05) — renderizadas DEPOIS de todos os players, por cima de tudo
    for (int p = 0; p < 1; p++) {
        // 03.SPR life bar border (g_fontSpr03)
        if (g_fontSpr03 >= 0) {
            float px = (p == 0) ? 0.0f : 320.0f;
            int cnt = sprTileCount(g_fontSpr03);
            for (int t = cnt - 1; t >= 0; t--) {
                int idx = g_fontSpr03 + t;
                float sx = px + (float)g_game.sprTiles[idx].srcX;
                float sy = (float)g_game.sprTiles[idx].srcY;
                float sw = (float)g_game.sprTiles[idx].srcW;
                float sh = (float)g_game.sprTiles[idx].srcH;
                Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, 1.0f);
            }
        }
        // 04.SPR life bar fill (g_fontSpr04)
        if (g_fontSpr04 >= 0) {
            float px = (p == 0) ? 0.0f : 320.0f;
            float lifePct = g_game.stats.life[p] / 100.0f;
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
                if (p == 0) {
                    // Inverte horizontalmente no P1
                    float tmp = u1; u1 = u2; u2 = tmp;
                }
                // Mantem centro fixo como Sprite_DrawTileUV faz
                float cx = sx + sw / 2.0f;
                float cw = sw * lifePct;
                Texture_DrawUV(tile->texId, cx - cw / 2.0f, sy, cw, sh,
                              u1, v1, u2, v2, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }
        // 05.SPR life bar glow (g_fontSpr05) — Ghidra: pisca em vida critica a cada 3 frames
        if (g_fontSpr05 >= 0) {
            float px = (p == 0) ? 0.0f : 320.0f;
            float lifePct = g_game.stats.life[p] / 100.0f;
            if (lifePct < 0.25f && (g_game.frameCounter % 6) < 3) {
                int cnt = sprTileCount(g_fontSpr05);
                for (int t = cnt - 1; t >= 0; t--) {
                    int idx = g_fontSpr05 + t;
                    float sx = px + (float)g_game.sprTiles[idx].srcX;
                    float sy = (float)g_game.sprTiles[idx].srcY;
                    float sw = (float)g_game.sprTiles[idx].srcW;
                    float sh = (float)g_game.sprTiles[idx].srcH;
                    Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw, sh, 0.8f);
                }
            }
        }
        }  // end for p (life bars)

    // Explosao: seta congelada + ARROWF (depois de tudo, sobrepoe tudo)
    for (int pe = 0; pe < 1; pe++) {
        float expPosX[5];
        expPosX[0] = 38.0f;
        expPosX[1] = expPosX[0] + 48.0f;
        expPosX[2] = expPosX[1] + 48.0f;
        expPosX[3] = expPosX[2] + 48.0f;
        expPosX[4] = expPosX[3] + 48.0f;
        float erY = 38.0f + 28.0f;
        static const float expOffX[5] = {-7.0f, -6.0f, -5.0f, -6.0f, -7.0f};
        static int kExpBaseInit = 0;
        static int kExpBase[5];
        if (!kExpBaseInit) {
            kExpBase[0] = g_fontArrow542;
            kExpBase[1] = g_fontArrow541;
            kExpBase[2] = g_fontArrow545;
            kExpBase[3] = g_fontArrow543;
            kExpBase[4] = g_fontArrow544;
            kExpBaseInit = 1;
        }
        for (int pan = 0; pan < 5; pan++) {
            if (g_noteState[pe][pan] != 1) continue;
            int base = kExpBase[pan];
            if (base < 0) continue;
            int af = (g_game.frameCounter / 3) % 6;
            int aSpr = base + af;
            float sw = (float)g_game.sprTiles[aSpr].srcW;
            float sh = (float)g_game.sprTiles[aSpr].srcH;
            Sprite_DrawTileUV(aSpr, expPosX[pan] + sw / 2.0f, erY, sw, sh, 1.0f);
            if (g_fontArrowF >= 0) {
                int fCnt = sprTileCount(g_fontArrowF);
                if (fCnt > 0) {
                    int fIdx = pan < fCnt ? pan : 0;
                    int fSpr = g_fontArrowF + fIdx;
                    float fw = (float)g_game.sprTiles[fSpr].srcW;
                    float fh = (float)g_game.sprTiles[fSpr].srcH;
                    float ef = (float)g_noteExplodeFrame[pe][pan];
                    float esc = 0.8f + ef * 0.02f;
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    Sprite_DrawTileUV(fSpr, expPosX[pan] + expOffX[pan] + fw / 2.0f, erY, fw * esc, fh * esc, 1.0f);
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
        static const char* pn[5] = {"DL","UL","CN","UR","DR"};
        char buf[64] = {0}; int pos = 0;
        for (int a = 0; a < 5; a++)
            if (g_autoPanel[a]) pos += snprintf(buf+pos, sizeof(buf)-pos, "%s ", pn[a]);
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
