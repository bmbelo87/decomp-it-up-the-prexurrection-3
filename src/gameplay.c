#include "pumpy.h"
#include "vsl.h"

#define APPROACH_SECONDS 1.8f
#define RECEPTOR_Y 400
#define ARROW_TOP_Y 40
#define PANEL_SIZE 30
#define P1_CENTER_X 160
#define P2_CENTER_X 480
#define P1_RECEPTOR_LX 100
#define P1_RECEPTOR_RX 220
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
static bool g_autoplay;

static NoteHit g_noteHits[2][5][2048];
static int g_noteHitCount[2][5];
static int g_nextNoteRow[2][5];

static float g_judgeDisplayTimer[2];
static JudgeType g_judgeDisplayType[2];
static int g_judgeDisplayCombo[2];

static bool loadChartForSong(int songId, int diffTier)
{
    g_songLoaded = false;
    g_chart = NULL;

    char stxPath[MAX_PATH];
    snprintf(stxPath, sizeof(stxPath), "%s\\STEP\\%d.STX",
             g_game.currentDirectory, songId);

    Log_Print("GP: loading '%s' (song %d, diff tier %d)\n", stxPath, songId, diffTier);

    if (!Step_LoadSong(stxPath, &g_playSong))
    {
        Log_Print("GP: FAILED to load STX\n");
        return false;
    }

    g_chartIdx = Step_FindChart(&g_playSong, (uint32_t)diffTier);
    if (g_chartIdx < 0)
    {
        Log_Print("GP: no chart for diff=%d, using chart 0\n", diffTier);
        g_chartIdx = 0;
    }

    g_chart = &g_playSong.charts[g_chartIdx];
    g_songTime = -APPROACH_SECONDS;
    float bpm = g_chart->bpm;
    if (bpm <= 0) bpm = 120.0f;
    uint32_t subdiv = g_chart->subdiv;
    if (subdiv == 0) subdiv = 4;

    g_secondsPerRow = 60.0 / ((double)bpm * (double)subdiv);
    g_totalSongSeconds = g_chart->rowCount * g_secondsPerRow;
    g_autoplay = g_game.input.autoplay;

    memset(g_noteHits, 0, sizeof(g_noteHits));
    memset(g_noteHitCount, 0, sizeof(g_noteHitCount));
    memset(g_nextNoteRow, 0, sizeof(g_nextNoteRow));

    Log_Print("GP: chart %d: BPM=%.1f subdiv=%d rows=%d time=%.1fs spR=%.4f\n",
        g_chartIdx, bpm, subdiv, g_chart->rowCount, g_totalSongSeconds, g_secondsPerRow);
    g_songLoaded = true;
    return true;
}

static void loadChart(void)
{
    SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
    int songId = mode->songIds[g_game.songSelectHighlighted];
    int diffTier = mode->difficulties[g_game.songSelectHighlighted];
    loadChartForSong(songId, diffTier);
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
        case PAD_UL: return 0;
        case PAD_UR: return 1;
        case PAD_C:  return 2;
        case PAD_DL: return 3;
        case PAD_DR: return 4;
        default: return -1;
    }
}

static uint8_t getPanelValue(StepRow* row, int panel, int player)
{
    if (player == 0)
    {
        switch (panel) {
            case 0: return row->half1.l;
            case 1: return row->half1.d;
            case 2: return row->half1.u;
            case 3: return row->half1.r;
            case 4: return row->half1.c;
        }
    }
    else
    {
        switch (panel) {
            case 0: return row->half2.l;
            case 1: return row->half2.d;
            case 2: return row->half2.u;
            case 3: return row->half2.r;
            case 4: return row->half2.c;
        }
    }
    return 0;
}

static void processInput(int player)
{
    if (!g_songLoaded) return;

    for (int b = 0; b < PAD_BUTTONS_PER_PLAYER; b++)
    {
        if (!Input_IsPadHit(player, (PadButton)b)) continue;
        int panel = getPanelForButton((PadButton)b);
        if (panel < 0) continue;

        double bestDiff = 999;
        int bestRow = -1;

        for (int ri = g_nextNoteRow[player][panel]; ri < (int)g_chart->rowCount; ri++)
        {
            uint8_t val = getPanelValue(&g_chart->rows[ri], panel, player);
            if (!val) continue;

            double rowTime = ri * g_secondsPerRow;
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

                double rowTime = ri * g_secondsPerRow;
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

                double rowTime = ri * g_secondsPerRow;
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

    int diffTier = g_game.selectedDifficulty;
    loadChartForSong(songId, diffTier);

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

    g_songTime += dt;

    {
        int maxFrame = g_game.bgaMaxFrame;
        if (g_game.isVSL && g_vsl.active && g_vsl.frameCount > 0)
            maxFrame = g_vsl.frameCount - 1;
        if (maxFrame > 0) {
            float bgaTime = (float)g_songTime + APPROACH_SECONDS;
            if (bgaTime < 0.0f) bgaTime = 0.0f;
            int newFrame = (int)(bgaTime * 60.0f);
            if (newFrame > maxFrame) newFrame = maxFrame;
            if (newFrame != g_game.bgaFrame) {
                Log_Print("BGA: frame %d -> %d (songTime=%.3f, max=%d)\n",
                          g_game.bgaFrame, newFrame, g_songTime, maxFrame);
            }
            g_game.bgaFrame = newFrame;
        }
    }

    if (g_game.frameCounter % 60 == 0) {
        Log_Print("BGA_STATE: frame=%d songTime=%.3f\n", g_game.bgaFrame, g_songTime);
        if (g_game.bgaPicCount > 0) {
            BGAPicture* pic = &g_game.bgaPics[0];
            for (int l = 0; l < pic->layerCount && l < 6; l++) {
                BGALayer* layer = &pic->layers[l];
                int seg = -1;
                for (int k = 0; k < layer->kfCount - 1; k++) {
                    if (layer->keyframes[k].frame <= g_game.bgaFrame &&
                        g_game.bgaFrame <= layer->keyframes[k+1].frame) {
                        seg = k; break;
                    }
                }
                Log_Print("  layer %d '%s': seg=%d kf=[%d..%d] kfCount=%d\n",
                          l, layer->filename, seg,
                          layer->kfCount > 0 ? layer->keyframes[0].frame : -1,
                          layer->kfCount > 0 ? layer->keyframes[layer->kfCount-1].frame : -1,
                          layer->kfCount);
            }
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

static void drawPanelAt(int panel, float x, float y, uint8_t val, float alpha, bool judged, JudgeType jt)
{
    (void)jt;
    float r, g, b;
    switch (panel)
    {
        case 0: r=1.0f; g=0.3f; b=0.3f; break;
        case 1: r=0.3f; g=1.0f; b=0.3f; break;
        case 2: r=1.0f; g=1.0f; b=0.3f; break;
        case 3: r=0.3f; g=0.6f; b=1.0f; break;
        case 4: r=1.0f; g=0.6f; b=0.0f; break;
        default: r=1; g=1; b=1; break;
    }

    if (judged && jt == JT_MISS)
    {
        r *= 0.4f; g *= 0.4f; b *= 0.4f;
    }

    if (val)
    {
        float s = PANEL_SIZE * (judged ? 1.5f : 1.0f);
        uint8_t a = (uint8_t)(alpha * (judged ? 128 : 220));
        Render_Rect(x - s/2, y - s/2, s, s,
            (uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255), a);
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

    float scrollPixelsPerSec = (RECEPTOR_Y - ARROW_TOP_Y) / APPROACH_SECONDS;

    int rowToShow = (int)((g_songTime + APPROACH_SECONDS) / g_secondsPerRow);
    if (rowToShow < 0) rowToShow = 0;
    int startRow = (int)((g_songTime - JUDGE_BAD) / g_secondsPerRow) - 1;
    if (startRow < 0) startRow = 0;
    int endRow = rowToShow + 2;
    if (endRow >= (int)g_chart->rowCount) endRow = g_chart->rowCount - 1;

    for (int p = 0; p < 2; p++)
    {
        int centerX = (p == 0) ? P1_CENTER_X : P2_CENTER_X;
        int baseX = centerX - 80;

        for (int ri = startRow; ri <= endRow; ri++)
        {
            StepRow* row = &g_chart->rows[ri];
            double rowTime = ri * g_secondsPerRow;
            double timeToHit = rowTime - g_songTime;

            if (timeToHit > APPROACH_SECONDS) continue;
            if (timeToHit < -JUDGE_BAD - 0.2f) continue;

            float y = (float)(RECEPTOR_Y + timeToHit * scrollPixelsPerSec);
            if (y < ARROW_TOP_Y - PANEL_SIZE || y > RECEPTOR_Y + PANEL_SIZE) continue;

            float alpha = 1.0f;
            if (y < ARROW_TOP_Y + PANEL_SIZE/2)
                alpha = (y - ARROW_TOP_Y + PANEL_SIZE) / (float)(PANEL_SIZE * 1.5f);
            if (alpha > 1) alpha = 1;
            if (alpha < 0) alpha = 0;

            bool anyNote = false;
            for (int panel = 0; panel < 5; panel++)
            {
                uint8_t val = getPanelValue(row, panel, p);
                if (val) { anyNote = true; break; }
            }
            if (!anyNote) continue;

            float posX[5] = {
                (float)(baseX + 0 * 40),
                (float)(baseX + 1 * 40),
                (float)(baseX + 2 * 40),
                (float)(baseX + 3 * 40),
                (float)(baseX + 4 * 40)
            };

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
                drawPanelAt(panel, posX[panel], y, val, alpha, isJudged, jt);
            }
        }
    }

    for (int p = 0; p < 2; p++)
    {
        int centerX = (p == 0) ? P1_CENTER_X : P2_CENTER_X;
        int baseX = centerX - 80;

        for (int panel = 0; panel < 5; panel++)
        {
            float px = (float)(baseX + panel * 40);
            Render_Rect(px - 18, RECEPTOR_Y - 4, 36, 8, 100, 100, 100, 200);
            Render_Rect(px - 16, RECEPTOR_Y - 3, 32, 6, 150, 200, 255, 255);
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
            Font_DrawStringCentered(centerX, RECEPTOR_Y + 40,
                comboBuf, 1, 1, 0, 1);
        }

        if (g_judgeDisplayTimer[p] > 0)
        {
            JudgeType jt = g_judgeDisplayType[p];
            float a = g_judgeDisplayTimer[p] / 0.6f;
            Font_DrawStringCentered(centerX, RECEPTOR_Y + 20,
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
