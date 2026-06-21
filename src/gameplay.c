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
static int g_blindTimer[2];
static int g_prevBlindRow;
static int g_lastPerfectRow[2][5];

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
    if (jt == JT_PERFECT || jt == JT_GREAT) {
        memset(&g_chart->rows[row], 0, sizeof(StepRow));
        for (int pan = 0; pan < 5; pan++)
            if (getPanelValue(&g_chart->rows[row], pan, player))
                g_lastPerfectRow[player][pan] = row;
    }
    g_judgeDisplayType[player] = jt;
    g_judgeDisplayTimer[player] = 0.6f;
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
        case JT_PERFECT: g_game.stats.perfectCount[player]++; g_game.stats.score[player] += 1000; break;
        case JT_GREAT:   g_game.stats.greatCount[player]++;   g_game.stats.score[player] += 500;  break;
        case JT_GOOD:    g_game.stats.goodCount[player]++;    g_game.stats.score[player] += 200;  break;
        case JT_BAD:     g_game.stats.badCount[player]++;     g_game.stats.score[player] += 0;    break;
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
        g_nextNoteRow[player][0] = g_pending[i].row + 1;
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

    // Collect hits per row (key=row, value={bestDiff, jt, anyValid})
    #define MAX_ROW_HITS 256
    static int hitRows[MAX_ROW_HITS];
    static double hitDiffs[MAX_ROW_HITS];
    static int hitPanels[MAX_ROW_HITS];
    static int hitCount = 0;
    hitCount = 0;

    for (int b = 0; b < PAD_BUTTONS_PER_PLAYER; b++)
    {
        int panel = getPanelForButton((PadButton)b);
        if (panel < 0) continue;

        bool isNewHit = Input_IsPadHit(player, (PadButton)b);
        bool isHeld = Input_IsPadDown(player, (PadButton)b);
        if (!isNewHit && !isHeld) continue;

        double bestDiff = 999;
        int bestRow = -1;

        if (isNewHit)
        {
            for (int ri = g_nextNoteRow[player][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, player);
                if (!val) continue;
                if (val == NT_HOLD_B || val == NT_HOLD_T) continue;

                double rowTime = getRowTime(ri);
                double diff = g_songTime - rowTime;
                if (diff < -JUDGE_BAD) break;
                if (diff > JUDGE_BAD) { g_nextNoteRow[player][panel] = ri + 1; continue; }

                double ad = diff < 0 ? -diff : diff;
                if (ad < bestDiff) { bestDiff = ad; bestRow = ri; }
            }
        }
        else if (isHeld)
        {
            for (int ri = g_nextNoteRow[player][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, player);
                if (!val || val == NT_HOLD_B || val == NT_HOLD_T) continue;

                double rowTime = getRowTime(ri);
                double diff = g_songTime - rowTime;

                if (val == NT_HOLD_H)
                {
                    if (diff > -JUDGE_BAD - 0.001f && diff <= JUDGE_BAD + 0.001f)
                    {
                        bestDiff = 0;
                        bestRow = ri;
                        break;
                    }
                    else if (diff > JUDGE_BAD)
                    {
                        g_nextNoteRow[player][panel] = ri + 1;
                        continue;
                    }
                    else break;
                }
                else
                {
                    if (diff > JUDGE_BAD)
                    {
                        g_nextNoteRow[player][panel] = ri + 1;
                        continue;
                    }
                    else break;
                }
            }
        }

        if (bestRow >= 0)
        {
            // Conta quantas setas tem nesta linha
            int arrowCount = 0;
            for (int pan = 0; pan < 5; pan++)
                if (getPanelValue(&g_chart->rows[bestRow], pan, player)) arrowCount++;

            if (arrowCount > 1) {
                // Multi-seta: adiar julgamento
                g_nextNoteRow[player][panel] = bestRow + 1;
                // Procura entrada pendente existente ou cria nova
                int slot = -1;
                for (int i = 0; i < MAX_PENDING; i++) {
                    if (g_pending[i].active && g_pending[i].row == bestRow) { slot = i; break; }
                }
                if (slot < 0) {
                    for (int i = 0; i < MAX_PENDING; i++) {
                        if (!g_pending[i].active) { slot = i; break; }
                    }
                }
                if (slot >= 0) {
                    g_pending[slot].row = bestRow;
                    g_pending[slot].deadline = g_songTime + JUDGE_BAD;
                    g_pending[slot].totalMask = 0;
                    for (int pan = 0; pan < 5; pan++)
                        if (getPanelValue(&g_chart->rows[bestRow], pan, player))
                            g_pending[slot].totalMask |= (1 << pan);
                    g_pending[slot].hitMask |= (1 << panel);
                    double adBest = bestDiff < 0 ? -bestDiff : bestDiff;
                    if (adBest > g_pending[slot].worstDiff)
                        g_pending[slot].worstDiff = adBest;
                    if (!g_pending[slot].active) {
                        g_pending[slot].active = true;
                        g_pendingCount++;
                    }
                }
                continue;  // Não processa agora, aguarda outras setas
            }

            // 1 seta apenas: processa imediatamente (comportamento atual)
            g_nextNoteRow[player][panel] = bestRow + 1;
            JudgeType hitJt = evaluateTiming(bestDiff);
            if (hitJt == JT_PERFECT || hitJt == JT_GREAT) {
                memset(&g_chart->rows[bestRow], 0, sizeof(StepRow));
                g_lastPerfectRow[player][panel] = bestRow;
                Log_Print("SET PERFECT ROW: p=%d pan=%d row=%d\n", player, panel, bestRow);
            }
            hitRows[hitCount] = bestRow;
            hitDiffs[hitCount] = bestDiff;
            hitPanels[hitCount] = panel;
            hitCount++;
        }
    }

    // Deduplicate by row: keep WORST timing per row, count 1 combo per row
    static int dedupRows[MAX_ROW_HITS];
    static double dedupDiffs[MAX_ROW_HITS];
    static int dedupCount = 0;
    dedupCount = 0;

    for (int i = 0; i < hitCount; i++)
    {
        // Check if this row was already hit by another panel
        int existing = -1;
        for (int d = 0; d < dedupCount; d++)
            if (dedupRows[d] == hitRows[i]) { existing = d; break; }

        if (existing >= 0)
        {
            // Keep the WORST diff (larger absolute = worse timing)
            if (hitDiffs[i] > dedupDiffs[existing])
                dedupDiffs[existing] = hitDiffs[i];
        }
        else
        {
            dedupRows[dedupCount] = hitRows[i];
            dedupDiffs[dedupCount] = hitDiffs[i];
            dedupCount++;
        }
    }

    // Process each deduplicated row
    for (int i = 0; i < dedupCount; i++)
    {
        JudgeType jt = evaluateTiming(dedupDiffs[i]);
        int bestRow = dedupRows[i];

        // Mark all panels in this row as judged
        for (int pan = 0; pan < 5; pan++)
        {
            uint8_t val = getPanelValue(&g_chart->rows[bestRow], pan, player);
            if (val)
            {
                NoteHit* nh = &g_noteHits[player][pan][g_noteHitCount[player][pan]++];
                nh->rowIndex = bestRow;
                nh->judged = true;
                nh->judgment = jt;
                nh->hitTime = g_songTime;
            }
        }

        // Perfect/Great: remover a nota do chart pra não renderizar mais
        if (jt == JT_PERFECT || jt == JT_GREAT) {
            memset(&g_chart->rows[bestRow], 0, sizeof(StepRow));
        }

        g_judgeDisplayType[player] = jt;
        g_judgeDisplayTimer[player] = 0.6f;

        switch (jt)
        {
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
            default:
                break;
        }

        switch (jt)
        {
            case JT_PERFECT: g_game.stats.perfectCount[player]++; g_game.stats.score[player] += 1000; break;
            case JT_GREAT:   g_game.stats.greatCount[player]++;   g_game.stats.score[player] += 500;  break;
            case JT_GOOD:    g_game.stats.goodCount[player]++;    g_game.stats.score[player] += 200;  break;
            case JT_BAD:     g_game.stats.badCount[player]++;     g_game.stats.score[player] += 0;    break;
            default: break;
        }

        if (g_game.stats.combo[player] > g_game.stats.maxCombo[player])
            g_game.stats.maxCombo[player] = g_game.stats.combo[player];
    }

    // Process holds: check if any dedup'd row has a hold head
    // (hold detection uses the row we just hit, not nextNoteRow)
    for (int i = 0; i < dedupCount; i++)
    {
        int hitRow = dedupRows[i];
        for (int panel = 0; panel < 5; panel++)
        {
            uint8_t val = getPanelValue(&g_chart->rows[hitRow], panel, player);
            if (val == NT_HOLD_H)
            {
                g_holdRows[player][panel] = hitRow;
                Log_Print("GP: hold started P%d Panel%d row %d\n", player, panel, hitRow);
            }
        }
    }
}

static void processAutoplay(void)
{
    if (!g_songLoaded || !g_autoplay) return;

    for (int p = 0; p < 2; p++)
    {
        // Collect autoplay hits per row
        int hitRows[10], hitCount = 0;
        for (int panel = 0; panel < 5; panel++)
        {
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
            g_judgeDisplayCombo[p] = ++g_game.stats.combo[p];
            g_game.stats.missCombo[p] = 0;
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

            // Autoplay sempre Perfect: limpa a nota do chart
            memset(&g_chart->rows[hitRows[i]], 0, sizeof(StepRow));
        }
    }
}

static void processHolds(void)
{
    if (!g_songLoaded) return;
    for (int p = 0; p < 2; p++)
    {
        for (int panel = 0; panel < 5; panel++)
        {
            // Reverse panel-to-button mapping: panel 0=DL=3, 1=UL=0, 2=C=2, 3=UR=1, 4=DR=4
            static const PadButton panelToBtn[5] = { PAD_DL, PAD_UL, PAD_C, PAD_UR, PAD_DR };
            bool held = g_autoplay ? true : Input_IsPadDown(p, panelToBtn[panel]);

            // Auto-capture: if no active hold but button held, check for hold head
            if (g_holdRows[p][panel] < 0 && held)
            {
                int ri = g_nextNoteRow[p][panel];
                if (ri < (int)g_chart->rowCount)
                {
                    uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                    if (val == NT_HOLD_H)
                    {
                        double rowTime = getRowTime(ri);
                        double diff = g_songTime - rowTime;
                        if (diff > -JUDGE_BAD - 0.001f && diff <= JUDGE_BAD + 0.001f)
                        {
                            g_holdRows[p][panel] = ri;
                            g_nextNoteRow[p][panel] = ri + 1;
                            NoteHit* nh = &g_noteHits[p][panel][g_noteHitCount[p][panel]++];
                            nh->rowIndex = ri;
                            nh->judged = true;
                            nh->judgment = JT_PERFECT;
                            nh->hitTime = g_songTime;
                            g_game.stats.combo[p]++;
                            g_game.stats.missCombo[p] = 0;
                            g_game.stats.score[p] += 1000;
                            g_game.stats.perfectCount[p]++;
                            if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
                                g_game.stats.maxCombo[p] = g_game.stats.combo[p];
                            g_judgeDisplayType[p] = JT_PERFECT;
                            g_judgeDisplayTimer[p] = 0.6f;
                            g_judgeDisplayCombo[p] = g_game.stats.combo[p];
                        }
                        else if (diff > JUDGE_BAD)
                        {
                            g_nextNoteRow[p][panel] = ri + 1;
                        }
                    }
                }
            }

            if (g_holdRows[p][panel] < 0) continue;

            // Scan forward from head to find body/tail rows that need processing
            for (int ri = g_holdRows[p][panel] + 1; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                if (val != NT_HOLD_B && val != NT_HOLD_T) break;

                double rowTime = getRowTime(ri);
                if (g_songTime < rowTime - 0.05) break; // too early

                // Check if this body/tail row was already judged
                bool alreadyJudged = false;
                for (int h = 0; h < g_noteHitCount[p][panel]; h++)
                    if (g_noteHits[p][panel][h].rowIndex == ri) { alreadyJudged = true; break; }
                if (alreadyJudged) continue;

                // If button is still held, auto-perfect; otherwise miss
                JudgeType jt = held ? JT_PERFECT : JT_MISS;
                NoteHit* nh = &g_noteHits[p][panel][g_noteHitCount[p][panel]++];
                nh->rowIndex = ri;
                nh->judged = true;
                nh->judgment = jt;
                nh->hitTime = g_songTime;

                if (!held)
                {
                    g_game.stats.combo[p] = 0;
                    g_game.stats.missCount[p]++;
                    g_game.stats.missCombo[p]++;
                }
                else
                {
                    g_game.stats.combo[p]++;
                    g_game.stats.missCombo[p] = 0;
                    g_game.stats.score[p] += 1000;
                    g_game.stats.perfectCount[p]++;
                    if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
                        g_game.stats.maxCombo[p] = g_game.stats.combo[p];
                    g_judgeDisplayType[p] = JT_PERFECT;
                    g_judgeDisplayTimer[p] = 0.6f;
                    g_judgeDisplayCombo[p] = g_game.stats.combo[p];
                }

                if (val == NT_HOLD_T || !held)
                {
                    g_holdRows[p][panel] = -1; // hold complete
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
                if (val == NT_HOLD_B || val == NT_HOLD_T)
                {
                    if (g_holdRows[p][panel] >= 0) continue;
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

    if (Input_IsKeyHit(VK_F1)) {
        g_autoplay = !g_autoplay;
        Log_Print("GP: autoplay %s\n", g_autoplay ? "ON" : "OFF");
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
    processInput(1);
    processPendingRows(0);
    processPendingRows(1);
    processAutoplay();
    processHolds();
    processMisses();

    for (int p = 0; p < 2; p++)
    {
        if (g_judgeDisplayTimer[p] > 0)
            g_judgeDisplayTimer[p] -= dt;
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

    for (int p = 0; p < 2; p++)
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

        // Grid de fundo (compasso/batida/sub-batida)
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

        int rh = 57;
        // Notas (scroll do fundo para o topo)
        static const int kPanelOrder[5] = {1, 3, 0, 2, 4};  // UL/UR atras, DL/CN/DR frente
        for (int ri = startRow; ri <= endRow; ri++)
        {
            StepRow* row = &g_chart->rows[ri];
            float y = (float)(receptorY + rh / 2 + (ri - scrollRow) * pixelsPerRow);
            if (y < receptorY - rh / 2 - 50 || y > scrollBottom + PANEL_SIZE) continue;

            double timeToHit = getRowTime(ri) - g_songTime;

            float alpha = 1.0f;
            if (y > scrollBottom - PANEL_SIZE)
                alpha = (scrollBottom + PANEL_SIZE - y) / (float)(PANEL_SIZE * 1.5f);
            if (alpha > 1) alpha = 1;
            if (alpha < 0) alpha = 0;

            bool anyNote = false;
            for (int panel = 0; panel < 5; panel++)
            {
                if (getPanelValue(row, panel, p)) { anyNote = true; break; }
            }
            // Perfect/Great consumiu esta row? Não mostrar NADA desta row
            // Ordem: UL/UR atras, DL/CN/DR na frente
            for (int rio = 0; rio < 5; rio++)
            {
                int panel = kPanelOrder[rio];
                if (ri == g_lastPerfectRow[p][panel]) { 
                    if (p == 0) Log_Print("BLOCKED ROW: ri=%d panel=%d g_lastPerfectRow=%d\n", ri, panel, g_lastPerfectRow[p][panel]);
                    anyNote = false; break; 
                }
            }
            if (!anyNote) continue;

            static const int kPanelOrder[5] = {1, 3, 0, 2, 4};
            for (int rio = 0; rio < 5; rio++)
            {
                int panel = kPanelOrder[rio];
                uint8_t val = getPanelValue(row, panel, p);
                if (!val) continue;

                float a = alpha * 0.85f;
                float nw = (float)pW[panel];
                float nh = (float)rh;
                uint8_t aa = (uint8_t)(a * 255);
                uint8_t nr, ng, nb;
                int nt = (int)val;
                
                if (panel == 0 || panel == 4)      { nr = 52;  ng = 120; nb = 200; }
                else if (panel == 1 || panel == 3) { nr = 200; ng = 60;  nb = 60; }
                else                                { nr = 220; ng = 200; nb = 40; }

                if (nt == NT_HOLD_B)
                {
                    float bodyH = rh * 2.5f;
                    uint8_t ba = (uint8_t)(a * 128);
                    Render_Rect(posX[panel], y - bodyH / 2, nw, bodyH, nr, ng, nb, ba);
                }
                else if (panel == 0 && g_fontArrow542 >= 0) {
                    float sw = (float)g_game.sprTiles[g_fontArrow542].srcW;
                    float sh = (float)g_game.sprTiles[g_fontArrow542].srcH;
                    Sprite_DrawTileUV(g_fontArrow542, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 1 && g_fontArrow541 >= 0) {
                    float sw = (float)g_game.sprTiles[g_fontArrow541].srcW;
                    float sh = (float)g_game.sprTiles[g_fontArrow541].srcH;
                    Sprite_DrawTileUV(g_fontArrow541, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 2 && g_fontArrow545 >= 0) {
                    float sw = (float)g_game.sprTiles[g_fontArrow545].srcW;
                    float sh = (float)g_game.sprTiles[g_fontArrow545].srcH;
                    Sprite_DrawTileUV(g_fontArrow545, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 3 && g_fontArrow543 >= 0) {
                    float sw = (float)g_game.sprTiles[g_fontArrow543].srcW;
                    float sh = (float)g_game.sprTiles[g_fontArrow543].srcH;
                    Sprite_DrawTileUV(g_fontArrow543, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (panel == 4 && g_fontArrow544 >= 0) {
                    float sw = (float)g_game.sprTiles[g_fontArrow544].srcW;
                    float sh = (float)g_game.sprTiles[g_fontArrow544].srcH;
                    Sprite_DrawTileUV(g_fontArrow544, posX[panel] + sw / 2.0f, y, sw, sh, 1.0f);
                }
                else if (nt == NT_HOLD_H || nt == NT_HOLD_T)
                {
                    float hh = rh * 1.0f;
                    Render_Rect(posX[panel] - 1, y - hh / 2 - 1, nw + 2, hh + 2, 0, 0, 0, aa);
                    Render_Rect(posX[panel], y - hh / 2, nw, hh, nr, ng, nb, aa);
                }
                else
                {
                    Render_Rect(posX[panel] - 1, y - nh / 2 - 1, nw + 2, nh + 2, 0, 0, 0, aa);
                    Render_Rect(posX[panel], y - nh / 2, nw, nh, nr, ng, nb, aa);
                }
            }
        }

        int centerY = g_game.screenHeight / 2;

        // Score (small, top corner)
        char scoreBuf[64];
        if (p == 0)
        {
            snprintf(scoreBuf, sizeof(scoreBuf), "P1: %u", g_game.stats.score[p]);
            Font_DrawString(10, 10, scoreBuf, 1, 1, 0, 1);
        }
        else
        {
            snprintf(scoreBuf, sizeof(scoreBuf), "P2: %u", g_game.stats.score[p]);
            Font_DrawString(g_game.screenWidth - 120, 10, scoreBuf, 1, 1, 0, 1);
        }

        // Judge + combo display (3-line, timed)
        if (g_judgeDisplayTimer[p] > 0)
        {
            JudgeType jt = g_judgeDisplayType[p];
            float a = g_judgeDisplayTimer[p] / 0.6f;
            float fc = g_judgeColors[jt][0], gc = g_judgeColors[jt][1], bc = g_judgeColors[jt][2];

            // Line 1: Judge text (larger scale, top)
            float brightA = a * 0.9f + 0.1f;
            Font_DrawStringCenteredScaled(centerX, centerY + 55, g_judgeNames[jt], fc, gc, bc, brightA, 2.5f);

            // Line 2 & 3: combo number + "COMBO"
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
                float decW = 48.4f;
                float decSpacing = -8.0f;
                float step = decW + decSpacing;  // 32
                // Ghidra: unidades primeiro (direita), centenas por ultimo (esquerda)
                int d3 = comboVal % 10;          // unidades
                int d2 = (comboVal / 10) % 10;   // dezenas
                int d1 = comboVal / 100;         // centenas
                float totalW = decW * 3 + decSpacing * 2;
                float dx = (float)centerX - totalW / 2.0f;
                float dy = (float)(centerY + 20);
                float da = brightA;
                // Renderiza da direita pra esquerda: unidades → dezenas → centenas
                Font_DrawDecDigit(g_fontDec00Id, dx + step * 2, dy, d3, da);
                Font_DrawDecDigit(g_fontDec00Id, dx + step, dy, d2, da);
                Font_DrawDecDigit(g_fontDec00Id, dx, dy, d1, da);
                Font_DrawStringCenteredScaled(centerX, centerY - 12, "COMBO", 1.0f, 1.0f, 1.0f, brightA, 1.6f);
            }
        }  // end judge/combo
    }  // end for p

    // Life bars (03/04/05) — renderizadas DEPOIS de todos os players, por cima de tudo
    for (int p = 0; p < 2; p++) {
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
                float sx = px + (float)g_game.sprTiles[idx].srcX;
                float sy = (float)g_game.sprTiles[idx].srcY;
                float sw = (float)g_game.sprTiles[idx].srcW;
                float sh = (float)g_game.sprTiles[idx].srcH;
                Sprite_DrawTileUV(idx, sx + sw / 2.0f, sy + sh / 2.0f, sw * lifePct, sh, 1.0f);
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

    // Timer regressivo
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

    if (g_autoplay)
        Font_DrawStringCentered(g_game.screenWidth/2, 28, "AUTOPLAY", 0, 1, 0, 0.7f);
}


//force
