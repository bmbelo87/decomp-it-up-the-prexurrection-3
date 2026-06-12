#include "pumpy.h"

extern bool Window_Create(HINSTANCE hInstance, int width, int height, bool fullscreen);
extern void Window_Destroy(void);
extern void Window_SwapBuffers(void);
extern bool Window_ProcessMessages(void);

static void InitSystems(void) {
    Log_Print("=== PUMP IT UP v%s (%s %s) ===\n",
              GAME_VERSION, GAME_BUILD_DATE, GAME_BUILD_TIME);

    timeBeginPeriod(1);

    if (!Window_Create(g_game.hInstance, 640, 480, false)) {
        MessageBoxA(NULL, "Failed to create OpenGL window", "Error", MB_OK | MB_ICONERROR);
        exit(1);
    }

    Log_Print("Systems initialized\n");
}

static void ShutdownSystems(void) {
    Log_Print("Shutting down...\n");
    Window_Destroy();
    timeEndPeriod(1);
    Log_Flush();
}

static int findBGALoopStart(void) {
    int loopStart = 999999;
    int loopEnd = 0;
    for (int p = 0; p < g_game.bgaPicCount; p++) {
        BGAPicture* pic = &g_game.bgaPics[p];
        for (int l = 0; l < pic->layerCount; l++) {
            BGALayer* layer = &pic->layers[l];
            for (int k = 0; k < layer->kfCount; k++) {
                BGAKeyframe* kf = &layer->keyframes[k];
                if (kf->type != 0 && kf->a > 0.01f) {
                    if (kf->frame < loopStart) loopStart = kf->frame;
                    if (kf->frame > loopEnd) loopEnd = kf->frame;
                }
            }
        }
    }
    if (loopStart > loopEnd) return 0;
    return loopStart;
}

static int findBGALoopEnd(void) {
    int loopStart = 999999;
    int loopEnd = 0;
    for (int p = 0; p < g_game.bgaPicCount; p++) {
        BGAPicture* pic = &g_game.bgaPics[p];
        for (int l = 0; l < pic->layerCount; l++) {
            BGALayer* layer = &pic->layers[l];
            for (int k = 0; k < layer->kfCount; k++) {
                BGAKeyframe* kf = &layer->keyframes[k];
                if (kf->type != 0 && kf->a > 0.01f) {
                    if (kf->frame < loopStart) loopStart = kf->frame;
                    if (kf->frame > loopEnd) loopEnd = kf->frame;
                }
            }
        }
    }
    if (loopStart > loopEnd) return g_game.bgaMaxFrame;
    return loopEnd;
}

static void LoadBGAForState(GameState state) {
    const char* bgaName = NULL;
    switch (state) {
    case STATE_INIT_WARNING: bgaName = "R_WARN"; break;
    case STATE_LOGO_ANIM:    bgaName = "81"; break;
    case STATE_MENU_TRANSITION: bgaName = "82w"; break;
    //case STATE_SONG_SELECT:  bgaName = "099"; break;
    case STATE_GAMEPLAY_PREP:
    case STATE_GAMEPLAY:     bgaName = "00"; break;
    case STATE_RESULT:       bgaName = "83"; break;
    case STATE_GAMEOVER:     bgaName = "84"; break;
    default: break;
    }

    if (bgaName) {
        if (g_game.bgaPicCount == 0 || _stricmp(g_game.bgaPics[0].name, bgaName) != 0) {
            Resource_SwitchBGA(bgaName);
        }
    }
    
    g_game.bgaLoop = (state == STATE_MENU_TRANSITION || state == STATE_MENU_IDLE ||
                      state == STATE_MENU_INPUT || state == STATE_SONG_SELECT ||
                      state == STATE_SONG_SELECT_B);
    if (g_game.bgaLoop) {
        if (g_game.bgaPicCount == 0 || (bgaName && _stricmp(g_game.bgaPics[0].name, bgaName) != 0)) {
            g_game.bgaLoopStart = findBGALoopStart();
            g_game.bgaLoopEnd = findBGALoopEnd();
            g_game.bgaFrame = g_game.bgaLoopStart;
        }
    } else {
        g_game.bgaLoop = false;
    }
}

void Game_ChangeState(GameState newState) {
    Log_Print("State: %s -> %s\n",
              State_ToString(g_game.state), State_ToString(newState));
    g_game.state = newState;
    g_game.nextState = newState;
    g_game.stateFrame = 0;
    LoadBGAForState(newState);
}

extern void Gamestate_UpdateWarning(float dt);
extern void Gamestate_UpdateLogo(float dt);
extern void Gamestate_UpdateMenu(float dt);
extern void Gamestate_UpdateSongSelect(float dt);
extern void Gamestate_RenderSongSelect(void);
extern void Gameplay_Update(float dt);
extern void Gameplay_Render(void);

void Game_Init(HINSTANCE hInstance) {
    memset(&g_game, 0, sizeof(g_game));
    g_game.hInstance = hInstance;
    g_game.screenWidth = 640;
    g_game.screenHeight = 480;
    g_game.globalScaleX = 1.0f;
    g_game.globalScaleY = 1.0f;
    g_game.globalAlpha = 1.0f;
    GetCurrentDirectoryA(MAX_PATH, g_game.currentDirectory);
    InitSystems();
    Font_Init();
    Texture_Init();

    Log_Print("Loading song database...\n");
    char cfgPath[MAX_PATH];
    char stepDir[MAX_PATH];
    snprintf(cfgPath, sizeof(cfgPath), "%s\\Stage.cfg", g_game.currentDirectory);
    snprintf(stepDir, sizeof(stepDir), "%s\\STEP", g_game.currentDirectory);
    if (Song_LoadDatabase(cfgPath, &g_game.songDB))
    {
        Log_Print("Song database: %d songs, %d modes\n",
            g_game.songDB.songCount, g_game.songDB.modeCount);
        for (int i = 0; i < g_game.songDB.songCount; i++)
        {
            SongEntry* e = &g_game.songDB.songs[i];
            char stxPath[MAX_PATH];
            snprintf(stxPath, sizeof(stxPath), "%s\\%d.STX", stepDir, e->id);
            FILE* test = fopen(stxPath, "rb");
            if (test) { fclose(test); e->hasChart = true; }
        }
        g_game.selectedSongIndex = 0;
        g_game.selectedModeIndex = Song_FindMode(&g_game.songDB, "EASY");
        if (g_game.selectedModeIndex < 0 && g_game.songDB.modeCount > 0)
            g_game.selectedModeIndex = 0;
        g_game.songSelectScroll = 0;
        g_game.songSelectHighlighted = 0;
        g_game.selectedDifficulty = 0;
        g_game.previewSongId = -1;
    }
    else
    {
        Log_Print("WARNING: Failed to load song database from '%s'\n", cfgPath);
    }

    Game_ChangeState(STATE_INIT_WARNING);
    g_game.lastTime = timeGetTime();
}

void Game_Shutdown(void) {
    BGM_Shutdown();
    Font_Shutdown();
    ShutdownSystems();
}



void Game_Update(float dt) {
    Input_Update();
    g_game.frameCounter++;
    g_game.stateFrame++;


    if (g_game.bgaPicCount > 0 && g_game.state != STATE_WARNING_END) {
        g_game.bgaTimer += dt;
        if (g_game.bgaTimer >= 1.0f / 60.0f) {
            g_game.bgaTimer -= 1.0f / 60.0f;
            if (g_game.bgaLoop && g_game.bgaLoopEnd > g_game.bgaLoopStart) {
                g_game.bgaFrame++;
                if (g_game.bgaFrame > g_game.bgaLoopEnd) {
                    g_game.bgaFrame = g_game.bgaLoopStart;
                }
            } else {
                if (g_game.bgaFrame < g_game.bgaMaxFrame) {
                    g_game.bgaFrame++;
                }
            }
        }
    }

    switch (g_game.state) {
    case STATE_INIT_WARNING:
    case STATE_WARNING_ANIM:
    case STATE_WARNING_WAIT:
    case STATE_WARNING_END:
        Gamestate_UpdateWarning(dt);
        break;
    case STATE_LOGO_ANIM:
    case STATE_LOGO_WAIT:
        Gamestate_UpdateLogo(dt);
        break;
    case STATE_MENU_TRANSITION:
    case STATE_MENU_IDLE:
    case STATE_MENU_INPUT:
        Gamestate_UpdateMenu(dt);
        break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B:
        Gamestate_UpdateSongSelect(dt);
        break;
    case STATE_GAMEPLAY_PREP:
        if (g_game.stateFrame == 0) Gameplay_Enter();
        if (g_game.stateFrame > 30) Game_ChangeState(STATE_GAMEPLAY);
        break;
    case STATE_GAMEPLAY:
        Gameplay_Update(dt);
        break;
    case STATE_EXIT:
        PostQuitMessage(0);
        break;
    default:
        break;
    }
}

static void Render_StateInfo(void) {
    char buf[256];
    int y = 8;
    
    // State info
    snprintf(buf, sizeof(buf), "State: %s  Frame: %u/%u",
             State_ToString(g_game.state), g_game.stateFrame, g_game.frameCounter);
    Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
    y += 16;
    
    snprintf(buf, sizeof(buf), "P1: Q(UL) E(UR) S(C) Z(DL) C(DR) | P2: NumPad7/9/5/1/3 (NumLock OFF)");
    Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
    y += 16;
    
    if (g_game.bgaPicCount > 0) {
        BGAPicture* pic = &g_game.bgaPics[0];
        snprintf(buf, sizeof(buf), "BGA: %s | layers=%d | frame=%d/%d | tiles=%d",
                 pic->name, pic->layerCount, g_game.bgaFrame, g_game.bgaMaxFrame, g_game.sprTileCount);
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    } else {
        snprintf(buf, sizeof(buf), "BGA: (none loaded)");
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    }
    
    if (g_game.bgm.buffer) {
        snprintf(buf, sizeof(buf), "BGM: %s | playing=%d | size=%lu",
                 g_game.bgm.name[0] ? g_game.bgm.name : "(unnamed)",
                 g_game.bgm.playing, g_game.bgm.dataSize);
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    }
    
    if (g_game.state == STATE_SONG_SELECT || g_game.state == STATE_SONG_SELECT_B) {
        if (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount) {
            SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
            snprintf(buf, sizeof(buf), "Mode: %s | Song %d/%d | Preview ID=%d",
                     mode->name, g_game.songSelectHighlighted + 1, mode->songCount,
                     g_game.previewSongId);
            Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
            y += 16;
        }
    }
}

void Game_Render(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    // No background fill for states that have full-screen BGA
    if (g_game.state != STATE_INIT_WARNING && g_game.state != STATE_WARNING_ANIM && 
        g_game.state != STATE_WARNING_WAIT && g_game.state != STATE_WARNING_END) {
        float bgR = 0.2f, bgG = 0.2f, bgB = 0.4f;
        switch (g_game.state) {
        case STATE_LOGO_ANIM:
        case STATE_LOGO_WAIT:
            bgR = 0.2f; bgG = 0.2f; bgB = 0.2f; break;
        case STATE_MENU_TRANSITION:
        case STATE_MENU_IDLE:
        case STATE_MENU_INPUT:
            bgR = 0.0f; bgG = 0.4f; bgB = 0.6f; break;
        case STATE_SONG_SELECT:
        case STATE_SONG_SELECT_B:
            bgR = 0.4f; bgG = 0.0f; bgB = 0.6f; break;
        case STATE_GAMEPLAY_PREP:
        case STATE_GAMEPLAY:
            bgR = 0.1f; bgG = 0.1f; bgB = 0.3f; break;
        default:
            break;
        }
        glColor4f(bgR, bgG, bgB, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(640, 0);
        glVertex2f(640, 480);
        glVertex2f(0, 480);
        glEnd();
        glColor4f(1, 1, 1, 1);
    }

    if (g_game.state == STATE_INIT_WARNING || g_game.state == STATE_WARNING_ANIM ||
        g_game.state == STATE_WARNING_WAIT || g_game.state == STATE_WARNING_END) {
        if (g_game.bgaPicCount > 0) {
            BGA_Render(0, g_game.bgaFrame);
        }
    } else {
        if (g_game.bgaPicCount > 0) {
            BGA_Render(0, g_game.bgaFrame);
        }
    }

    switch (g_game.state) {
    case STATE_MENU_TRANSITION:
    case STATE_MENU_IDLE:
    case STATE_MENU_INPUT:
        Gamestate_RenderMenu();
        break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B:
        Gamestate_RenderSongSelect();
        break;
    case STATE_GAMEPLAY:
        Gameplay_Render();
        break;
    default:
        break;
    }

    Render_StateInfo();
    Window_SwapBuffers();
}

void Game_MainLoop(void) {
    bool running = true;
    while (running) {
        uint32_t frameStart = timeGetTime();
        if (!Window_ProcessMessages()) {
            running = false;
            break;
        }
        float dt = Timer_GetDelta();
        Game_Update(dt);
        Game_Render();
        uint32_t elapsed = timeGetTime() - frameStart;
        if (elapsed < FRAME_TIME_MS) {
            Sleep(FRAME_TIME_MS - elapsed);
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    __try {
        Game_Init(hInstance);
        Game_MainLoop();
        Game_Shutdown();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log_Print("CRASH: unhandled exception (0x%08lx)\n", GetExceptionCode());
        Log_Flush();
        MessageBoxA(NULL, "Game crashed. Check pumpy.log for details.", "Error", MB_OK | MB_ICONERROR);
    }
    return 0;
}
