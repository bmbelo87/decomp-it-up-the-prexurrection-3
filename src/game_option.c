#include "pumpy.h"
#include <stdio.h>

static int go_counter;
static int go_animCounter;

/* ---------- persistência em PUMPY.INI ---------- */

void GameOption_Save(void)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\PUMPY.INI", g_game.currentDirectory);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "[GameOption]\n");
    fprintf(f, "Difficulty=%d\n",    g_game.optionDifficulty);
    fprintf(f, "StageBreak=%d\n",    g_game.optionToggle1);
    fprintf(f, "ShowHelp=%d\n",      g_game.optionToggle2);
    fprintf(f, "AudioOffset=%d\n",   g_game.audioOffsetMs);
    fclose(f);
    Log_Print("GameOption: saved (diff=%d sb=%d help=%d audio=%dms)\n",
              g_game.optionDifficulty, g_game.optionToggle1, g_game.optionToggle2,
              g_game.audioOffsetMs);
}

void GameOption_Load(void)
{
    /* Defaults */
    g_game.optionDifficulty = 1;   /* Normal */
    g_game.optionToggle1    = 1;   /* Stage Break On */
    g_game.optionToggle2    = 0;   /* Show Help Off */
    g_game.audioOffsetMs    = 80;  /* 80ms — latência típica de áudio moderna */

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\PUMPY.INI", g_game.currentDirectory);
    FILE* f = fopen(path, "r");
    if (!f) {
        Log_Print("GameOption: PUMPY.INI not found, using defaults\n");
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (sscanf(line, "Difficulty=%d",  &v) == 1) g_game.optionDifficulty = v;
        if (sscanf(line, "StageBreak=%d",  &v) == 1) g_game.optionToggle1    = v;
        if (sscanf(line, "ShowHelp=%d",    &v) == 1) g_game.optionToggle2    = v;
        if (sscanf(line, "AudioOffset=%d", &v) == 1) g_game.audioOffsetMs    = v;
    }
    fclose(f);
    Log_Print("GameOption: loaded (diff=%d sb=%d help=%d audio=%dms)\n",
              g_game.optionDifficulty, g_game.optionToggle1, g_game.optionToggle2,
              g_game.audioOffsetMs);
}

/* ---------- init / update / render ---------- */

void Gamestate_InitGameOption(void)
{
    /* Nao sobrescreve os valores — ja foram carregados por GameOption_Load() na inicializacao */
    go_counter = 0;
    go_animCounter = 0;
}

void Gamestate_UpdateGameOption(float dt)
{
    (void)dt;

    if (g_game.state == STATE_GAMEOPTION_ENTER)
    {
        go_animCounter = 0;
        g_game.optionCurrentItem = 0;
        Resource_SwitchBGA("086");

        // GO_C05 (layer 27) KF2 has hx=0,hy=0 instead of hx=430,hy=120
        // causing sprite to slide during breath animation
        if (g_game.bgaPicCount > 0) {
            BGAPicture* pic = &g_game.bgaPics[0];
            if (pic->layerCount > 27) {
                for (int k = 0; k < pic->layers[27].kfCount; k++) {
                    if (pic->layers[27].keyframes[k].hotx == 0.0f &&
                        pic->layers[27].keyframes[k].hoty == 0.0f &&
                        pic->layers[27].keyframes[k].frame > 120) {
                        pic->layers[27].keyframes[k].hotx = 430.0f;
                        pic->layers[27].keyframes[k].hoty = 120.0f;
                    }
                }
            }
        }

        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s\\AUDIO\\086.AUD", g_game.currentDirectory);
            BGM_Stop();
            if (BGM_LoadAUDDirect(path)) BGM_Play(true);
        }

        Render_SetGlobalColor(0.0f, 0.0f, 0.0f, 0.0f);
        go_counter = 0;
        g_game.state = STATE_GAMEOPTION_ANIM;
        g_game.stateFrame = 0;
        return;
    }

    if (g_game.state == STATE_GAMEOPTION_ANIM)
    {
        go_animCounter++;
        if (go_animCounter > 30)
        {
            go_animCounter = 0;
            g_game.state = STATE_GAMEOPTION;
            g_game.stateFrame = 0;
        }
        return;
    }

    if (g_game.state == STATE_GAMEOPTION)
    {
        go_counter++;

        bool hitDL = Input_IsPadHit(0, PAD_DL) || Input_IsPadHit(1, PAD_DL);
        bool hitDR = Input_IsPadHit(0, PAD_DR) || Input_IsPadHit(1, PAD_DR);
        bool hitC  = Input_IsPadHit(0, PAD_C)  || Input_IsPadHit(1, PAD_C);

        if (hitDL)
        {
            if (g_game.optionCurrentItem == 0)
                g_game.optionCurrentItem = 4;
            else
                g_game.optionCurrentItem--;
            return;
        }

        if (hitDR)
        {
            if (g_game.optionCurrentItem == 4)
                g_game.optionCurrentItem = 0;
            else
                g_game.optionCurrentItem++;
            return;
        }

        if (hitC)
        {
            switch (g_game.optionCurrentItem)
            {
            case 0:
                if (g_game.optionDifficulty == 2)
                    g_game.optionDifficulty = 0;
                else
                    g_game.optionDifficulty++;
                GameOption_Save();
                break;
            case 1:
                g_game.optionToggle1 = !g_game.optionToggle1;
                GameOption_Save();
                break;
            case 2:
                g_game.optionToggle2 = !g_game.optionToggle2;
                GameOption_Save();
                break;
            case 3:
                g_game.optionDifficulty = 1;
                g_game.optionToggle1 = 1;
                g_game.optionToggle2 = 0;
                GameOption_Save();
                break;
            case 4:
                Audio_Play(g_waveSoundIds[SND_2_1], false);
                go_counter = 0;
                Menu_ResetState();
                Game_ChangeState(STATE_MENU_ENTER);
                return;
            }
        }
        return;
    }

    if (g_game.state == STATE_GAMEOPTION_EXIT)
    {
        go_counter = 0;
        Menu_ResetState();
        Game_ChangeState(STATE_MENU_ENTER);
        return;
    }
}

void Gamestate_RenderGameOption(void)
{
    if (g_game.state == STATE_GAMEOPTION_ENTER)
        return;

    if (g_game.state == STATE_GAMEOPTION_ANIM)
    {
        BGA_SetEventFrame(0, go_animCounter);
        return;
    }

    if (g_game.state != STATE_GAMEOPTION)
        return;

    int sel = g_game.optionCurrentItem;
    int cnt = go_counter;
    int anim = (cnt % 45) + 90;

    BGA_SetEventFrame(0, 60);

    if (sel == 0)
    {
        BGA_SetEventLayer(0, anim, 3);
        BGA_SetEventLayer(0, anim, 4);
        BGA_SetEventLayer(0, anim, 33);
    }
    else
    {
        BGA_SetEventLayer(0, 30, 3);
        BGA_SetEventLayer(0, 30, 4);
    }

    if (sel == 1)
    {
        BGA_SetEventLayer(0, anim, 7);
        BGA_SetEventLayer(0, anim, 8);
        BGA_SetEventLayer(0, anim, 30);
    }
    else
    {
        BGA_SetEventLayer(0, 30, 7);
        BGA_SetEventLayer(0, 30, 8);
    }

    if (sel == 2)
    {
        BGA_SetEventLayer(0, anim, 11);
        BGA_SetEventLayer(0, anim, 12);
        BGA_SetEventLayer(0, anim, 26);
    }
    else
    {
        BGA_SetEventLayer(0, 30, 11);
        BGA_SetEventLayer(0, 30, 12);
    }

    if (sel == 3)
    {
        BGA_SetEventLayer(0, anim, 15);
        BGA_SetEventLayer(0, anim, 16);
    }
    else
    {
        BGA_SetEventLayer(0, 30, 15);
        BGA_SetEventLayer(0, 30, 16);
    }

    if (sel == 4)
    {
        BGA_SetEventLayer(0, anim, 19);
        BGA_SetEventLayer(0, anim, 20);
    }
    else
    {
        BGA_SetEventLayer(0, 30, 19);
        BGA_SetEventLayer(0, 30, 20);
    }

    BGA_SetEventLayer(0, (cnt % 255) + 240, 22);

    BGA_SetEventLayer(0, 45, 43);
    BGA_SetEventLayer(0, 45, 44);
    BGA_SetEventLayer(0, 45, 45);
    BGA_SetEventLayer(0, 45, 46);
    BGA_SetEventLayer(0, 45, 47);

    if (g_game.optionToggle1)
    {
        BGA_SetEventLayer(0, anim, 31);
        BGA_SetEventLayer(0, (cnt % 30) + 90, 39);
    }
    else
    {
        BGA_SetEventLayer(0, anim, 32);
        BGA_SetEventLayer(0, (cnt % 30) + 90, 38);
    }

    switch (g_game.optionDifficulty)
    {
    case 0:
        BGA_SetEventLayer(0, anim, 27);
        BGA_SetEventLayer(0, (cnt % 30) + 90, 42);
        break;
    case 1:
        BGA_SetEventLayer(0, anim, 28);
        BGA_SetEventLayer(0, (cnt % 30) + 90, 41);
        break;
    case 2:
        BGA_SetEventLayer(0, anim, 29);
        BGA_SetEventLayer(0, (cnt % 30) + 90, 40);
        break;
    }

    if (g_game.optionToggle2)
    {
        BGA_SetEventLayer(0, anim, 34);
        BGA_SetEventLayer(0, (cnt % 30) + 90, 37);
    }
    else
    {
        BGA_SetEventLayer(0, anim, 35);
        BGA_SetEventLayer(0, (cnt % 30) + 90, 36);
    }
}
