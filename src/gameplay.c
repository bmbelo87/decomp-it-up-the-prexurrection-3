#include "pumpy.h"
#include "vsl.h"

#define APPROACH_SECONDS 1.8f

#define PANEL_SIZE 30
#define P1_CENTER_X 160
#define P2_CENTER_X 480

#define MISS_WINDOW 0.25f

#define JUDGE_PERFECT  0.055f
#define JUDGE_GREAT    0.110f
#define JUDGE_GOOD     0.165f
#define JUDGE_BAD      0.220f

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
    {0,0,0}, {1,1,0}, {0,1,0}, {0,0.5f,1}, {1,0.5f,0}, {1,0,0}
};

typedef struct {
    int rowIndex;
    bool judged;
    JudgeType judgment;
    double hitTime;
} NoteHit;

static StepSong g_playSong;
static int g_chartIdx;
static StepChart* g_chart;
static bool g_songLoaded;

static double g_songTime;
static double g_secondsPerRow;
static double g_totalSongSeconds;
static double g_chartDelay;
static bool g_autoplay;

static NoteHit g_noteHits[2][5][2048];
static int g_noteHitCount[2][5];
static int g_nextNoteRow[2][5];

static float g_judgeDisplayTimer[2];
static JudgeType g_judgeDisplayType[2];
static int g_judgeDisplayCombo[2];

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
    g_chartDelay = g_chart->delay / 100.0;
    float bpm = g_chart->bpm;
    if (bpm <= 0) bpm = 120.0f;
    uint32_t subdiv = g_chart->beatSplit;
    if (subdiv == 0) subdiv = 4;

    g_secondsPerRow = 60.0 / ((double)bpm * (double)subdiv);
    g_totalSongSeconds = g_chart->rowCount * g_secondsPerRow + g_chartDelay;
    g_autoplay = g_game.input.autoplay;

    memset(g_noteHits, 0, sizeof(g_noteHits));
    memset(g_noteHitCount, 0, sizeof(g_noteHitCount));
    memset(g_nextNoteRow, 0, sizeof(g_nextNoteRow));

    Log_Print("GP: chart %d: BPM=%.1f subdiv=%d rows=%d panels=%d time=%.1fs spR=%.4f\n",
        g_chartIdx, bpm, subdiv, g_chart->rowCount, g_chart->panelCount, g_totalSongSeconds, g_secondsPerRow);
    g_songLoaded = true;
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

static void processInput(int player)
{
    if (!g_songLoaded) return;

    for (int b = 0; b < PAD_BUTTONS_PER_PLAYER; b++)
    {
        int panel = getPanelForButton((PadButton)b);
        if (panel < 0) continue;

        if (!Input_IsPadHit(player, (PadButton)b)) continue;

        double bestDiff = 999;
        int bestRow = -1;

        for (int ri = g_nextNoteRow[player][panel]; ri < (int)g_chart->rowCount; ri++)
        {
            uint8_t val = getPanelValue(&g_chart->rows[ri], panel, player);
            if (!val) continue;

            double rowTime = ri * g_secondsPerRow + g_chartDelay;
            double diff = g_songTime - rowTime;
            if (diff < -JUDGE_BAD) break;
            if (diff > JUDGE_BAD) { g_nextNoteRow[player][panel] = ri + 1; continue; }

            double ad = diff < 0 ? -diff : diff;
            if (ad < bestDiff) { bestDiff = ad; bestRow = ri; }
        }

        if (bestRow >= 0)
        {
            JudgeType jt = evaluateTiming(bestDiff);
            g_nextNoteRow[player][panel] = bestRow + 1;

            NoteHit* nh = &g_noteHits[player][panel][g_noteHitCount[player][panel]++];
            nh->rowIndex = bestRow;
            nh->judged = true;
            nh->judgment = jt;
            nh->hitTime = g_songTime;

            g_judgeDisplayType[player] = jt;
            g_judgeDisplayTimer[player] = 0.6f;

            if (jt <= JT_GREAT)
                g_judgeDisplayCombo[player] = ++g_game.stats.combo[player];
            else
                g_judgeDisplayCombo[player] = (g_game.stats.combo[player] = 0);

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
    }
}

static void processAutoplay(void)
{
    if (!g_songLoaded || !g_autoplay) return;

    for (int p = 0; p < 2; p++)
    {
        for (int panel = 0; panel < 5; panel++)
        {
            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                if (!val) continue;

                double rowTime = ri * g_secondsPerRow + g_chartDelay;
                double diff = g_songTime - rowTime;
                if (diff < -JUDGE_PERFECT) break;
                if (diff > JUDGE_PERFECT) { g_nextNoteRow[p][panel] = ri + 1; continue; }

                g_nextNoteRow[p][panel] = ri + 1;
                NoteHit* nh = &g_noteHits[p][panel][g_noteHitCount[p][panel]++];
                nh->rowIndex = ri;
                nh->judged = true;
                nh->judgment = JT_PERFECT;
                nh->hitTime = g_songTime;

                g_judgeDisplayType[p] = JT_PERFECT;
                g_judgeDisplayTimer[p] = 0.6f;
                g_judgeDisplayCombo[p] = ++g_game.stats.combo[p];

                g_game.stats.score[p] += 1000;
                if (g_game.stats.combo[p] > g_game.stats.maxCombo[p])
                    g_game.stats.maxCombo[p] = g_game.stats.combo[p];
                break;
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
        for (int panel = 0; panel < 5; panel++)
        {
            for (int ri = g_nextNoteRow[p][panel]; ri < (int)g_chart->rowCount; ri++)
            {
                uint8_t val = getPanelValue(&g_chart->rows[ri], panel, p);
                if (!val) continue;

                double rowTime = ri * g_secondsPerRow + g_chartDelay;
                if (missThreshold <= rowTime) break;

                g_nextNoteRow[p][panel] = ri + 1;
                NoteHit* nh = &g_noteHits[p][panel][g_noteHitCount[p][panel]++];
                nh->rowIndex = ri;
                nh->judged = true;
                nh->judgment = JT_MISS;
                nh->hitTime = g_songTime;

                g_game.stats.combo[p] = 0;
                g_game.stats.missCount[p]++;
            }
        }
    }
}

void Gameplay_Start(int songId)
{
    memset(&g_game.stats, 0, sizeof(g_game.stats));
    g_game.stats.life[0] = 100;
    g_game.stats.life[1] = 100;
    memset(g_judgeDisplayTimer, 0, sizeof(g_judgeDisplayTimer));

    g_game.bgaFrame = 0;

    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int diffTier = g_game.selectedDifficulty;
    loadChartForSong(songId, diffTier, mode->name);

    Log_Print("Gameplay: started song %d\n", songId);
}

void Gameplay_Enter(void)
{
    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int songId = mode->songIds[g_game.songSelectHighlighted];
    Gameplay_Start(songId);

    char bgaPath[MAX_PATH];
    snprintf(bgaPath, sizeof(bgaPath), "%s\\BGA\\%d.DAT", g_game.currentDirectory, songId);
    Log_Print("Gameplay: loading BGA/VSL from '%s'\n", bgaPath);
    Resource_LoadBGADirect(bgaPath);

    BGM_Stop();
    if (BGM_LoadAUD(songId, false)) {
        BGM_Play(false);
    }

    Log_Print("Gameplay: enter\n");
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
    processAutoplay();
    processMisses();

    for (int p = 0; p < 2; p++)
    {
        if (g_judgeDisplayTimer[p] > 0)
            g_judgeDisplayTimer[p] -= dt;
    }

    bool audioDone = !BGM_IsPlaying();
    bool bgaDone = (g_game.bgaMaxFrame > 0) ? (g_game.bgaFrame >= g_game.bgaMaxFrame) : true;

    if (audioDone && bgaDone)
    {
        Game_ChangeState(STATE_RESULT);
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

    int receptorY = 32;
    int scrollBottom = 480;
    float scrollPixelsPerSec = (float)(scrollBottom - receptorY) / APPROACH_SECONDS;

    int rowToShow = (int)((g_songTime + APPROACH_SECONDS - g_chartDelay) / g_secondsPerRow);
    if (rowToShow < 0) rowToShow = 0;
    int startRow = (int)((g_songTime - JUDGE_BAD - g_chartDelay) / g_secondsPerRow) - 1;
    if (startRow < 0) startRow = 0;
    int endRow = rowToShow + 2;
    if (endRow >= (int)g_chart->rowCount) endRow = g_chart->rowCount - 1;

    for (int p = 0; p < 2; p++)
    {
        int centerX = (p == 0) ? P1_CENTER_X : P2_CENTER_X;
        int baseX = centerX - 80;

        float posX[5];
        posX[0] = (float)(baseX + 0 * 40);  // DL
        posX[1] = (float)(baseX + 1 * 40);  // UL
        posX[2] = (float)(baseX + 2 * 40);  // CN
        posX[3] = (float)(baseX + 3 * 40);  // UR
        posX[4] = (float)(baseX + 4 * 40);  // DR

        // Grid de fundo (compasso/batida/sub-batida)
        if (g_chart->beatPerMeasure > 0 && g_chart->beatSplit > 0)
        {
            int rowsPerMeasure = g_chart->beatPerMeasure * g_chart->beatSplit;
            int rowsPerBeat = g_chart->beatSplit;
            int gx0 = baseX - 22;
            int gx1 = baseX + 162;
            int gridStartRow = startRow > 0 ? startRow - 1 : 0;
            int gridEndRow = endRow < (int)g_chart->rowCount - 1 ? endRow + 1 : endRow;
            for (int ri = gridStartRow; ri <= gridEndRow; ri++)
            {
                double gy = receptorY + (ri * g_secondsPerRow + g_chartDelay - g_songTime) * scrollPixelsPerSec;
                if (gy < receptorY - PANEL_SIZE || gy > scrollBottom + PANEL_SIZE) continue;

                if (ri % rowsPerMeasure == 0)
                {
                    Render_Rect((float)gx0, (float)gy, (float)(gx1 - gx0), 3, 80, 80, 80, 180);
                    int blockNum = ri / rowsPerMeasure + 1;
                    char num[8];
                    snprintf(num, sizeof(num), "%d", blockNum);
                    int numY = g_game.screenHeight - (int)gy - 6;
                    Font_DrawString(gx0 - 22, numY, num, 0.5f, 0.5f, 0.5f, 0.8f);
                }
                else if (ri % rowsPerBeat == 0)
                {
                    for (int x = gx0; x < gx1; x += 16)
                        Render_Rect((float)x, (float)gy, 8, 2, 60, 60, 60, 120);
                }
                else
                {
                    for (int x = gx0; x < gx1; x += 24)
                        Render_Rect((float)x, (float)gy, 4, 1, 40, 40, 40, 70);
                }
            }
        }

        // Receptor no topo
        for (int panel = 0; panel < 5; panel++)
        {
            float px = posX[panel];
            Render_Rect(px - 18, receptorY - 4, 36, 8, 100, 100, 100, 200);
            Render_Rect(px - 16, receptorY - 3, 32, 6, 150, 200, 255, 255);
        }

        // Notas (scroll do fundo para o topo)
        for (int ri = startRow; ri <= endRow; ri++)
        {
            StepRow* row = &g_chart->rows[ri];
            double rowTime = ri * g_secondsPerRow + g_chartDelay;
            double timeToHit = rowTime - g_songTime;

            if (timeToHit > APPROACH_SECONDS) continue;
            if (timeToHit < -JUDGE_BAD - 0.2f) continue;

            float y = (float)(receptorY + timeToHit * scrollPixelsPerSec);
            if (y < receptorY - PANEL_SIZE || y > scrollBottom + PANEL_SIZE) continue;

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
            if (!anyNote) continue;

            for (int panel = 0; panel < 5; panel++)
            {
                uint8_t val = getPanelValue(row, panel, p);
                if (!val) continue;

                bool isJudged = false;
                JudgeType jt = JT_NONE;
                for (int h = 0; h < g_noteHitCount[p][panel]; h++)
                {
                    if (g_noteHits[p][panel][h].rowIndex == ri)
                    {
                        isJudged = true;
                        jt = g_noteHits[p][panel][h].judgment;
                        break;
                    }
                }

                float sc = isJudged ? 1.5f : 1.0f;
                float a = alpha * (isJudged ? 0.5f : 0.85f);
                float hw = 15 * sc;
                float hh = 8 * sc;
                uint8_t aa = (uint8_t)(a * 255);
                Render_Rect(posX[panel] - hw - 1, y - hh - 1, (hw + 1) * 2, (hh + 1) * 2, 0, 0, 0, aa);
                Render_Rect(posX[panel] - hw, y - hh, hw * 2, hh * 2, 255, 255, 255, aa);
            }
        }

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

        if (g_game.stats.combo[p] > 1)
        {
            char comboBuf[32];
            snprintf(comboBuf, sizeof(comboBuf), "%u COMBO!", g_game.stats.combo[p]);
            Font_DrawStringCentered(centerX, receptorY + 40,
                comboBuf, 1, 1, 0, 1);
        }

        if (g_judgeDisplayTimer[p] > 0)
        {
            JudgeType jt = g_judgeDisplayType[p];
            float a = g_judgeDisplayTimer[p] / 0.6f;
            Font_DrawStringCentered(centerX, receptorY + 20,
                g_judgeNames[jt],
                g_judgeColors[jt][0], g_judgeColors[jt][1], g_judgeColors[jt][2], a);
        }

        float lifeW = (g_game.stats.life[p] / 100.0f) * 120;
        int lx = (p == 0) ? 10 : (g_game.screenWidth - 130);
        Render_Rect((float)lx, (float)(g_game.screenHeight - 20), 120, 10, 60, 60, 60, 200);
        Render_Rect((float)lx, (float)(g_game.screenHeight - 20), lifeW, 10,
            (uint8_t)(255 - lifeW * 2.55f), (uint8_t)(lifeW * 2.55f), 40, 200);
    }

    double secLeft = g_totalSongSeconds - g_songTime;
    if (secLeft < 0) secLeft = 0;
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%.0f:%02.0f", secLeft/60, fmod(secLeft, 60));
    Font_DrawStringCentered(g_game.screenWidth/2, 10, timeBuf, 0.7f, 0.7f, 0.7f, 1);

    if (g_autoplay)
        Font_DrawStringCentered(g_game.screenWidth/2, 28, "AUTOPLAY", 0, 1, 0, 0.7f);
}
