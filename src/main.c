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

    Render_SetOrtho(640, 480);

    Log_Print("Systems initialized\n");
}

static void ShutdownSystems(void) {
    Log_Print("Shutting down...\n");
    Window_Destroy();
    timeEndPeriod(1);
    Log_Flush();
}

#include "bga.h"

static void LoadBGAForState(GameState state) {
    const char* bgaName = NULL;
    switch (state) {
    case STATE_WARNING_INIT:
    case STATE_WARNING_ANIM: bgaName = "R_WARN"; break;
    case STATE_LOGO_ENTER:   bgaName = "81"; break;
    case STATE_MENU_ENTER:
    case STATE_MENU_INPUT:
    case STATE_LOGO_SKIP:    bgaName = "82w"; break;
    case STATE_GAME_INIT:
    case STATE_GAMEPLAY:
        if (g_game.selectedSongIndex >= 0 && g_game.selectedSongIndex < g_game.songDB.songCount) {
            static char songBGAName[16];
            snprintf(songBGAName, sizeof(songBGAName), "%d", g_game.songDB.songs[g_game.selectedSongIndex].id);
            bgaName = songBGAName;
        } else {
            bgaName = "00";
        }
        break;
    case STATE_RESULT:       bgaName = "83"; break;
    case STATE_GAMEOVER:     bgaName = "84"; break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B:
    case STATE_LOADING_PNZ:
    case STATE_LOADING_PNZ_B: bgaName = ""; break;
    case STATE_STAFF_ENTER:
    case STATE_STAFF:
    case STATE_STAFF_END:      bgaName = ""; break;
    default: break;
    }

    if (bgaName) {
        if (bgaName[0] == '\0') {
            Resource_ClearBGA();
        } else if (g_game.bgaPicCount == 0 || _stricmp(g_game.bgaPics[0].name, bgaName) != 0) {
            int idx = Resource_SwitchBGA(bgaName);
            if (idx < 0 && (state == STATE_SONG_SELECT || state == STATE_SONG_SELECT_B ||
                            state == STATE_STAFF_ENTER)) {
                char directPath[MAX_PATH];
                snprintf(directPath, sizeof(directPath), "%s\\BGA\\%s.DAT",
                         g_game.currentDirectory, bgaName);
                Log_Print("BGA: fallback loading '%s'\n", directPath);
                Resource_LoadBGADirect(directPath);
            }
        }
    }

    if (state == STATE_MENU_ENTER) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\AUDIO\\082.AUD", g_game.currentDirectory);
        BGM_Stop();
        if (BGM_LoadAUDDirect(path)) BGM_Play(true);
    } else if (state == STATE_SONG_SELECT || state == STATE_SONG_SELECT_B) {
        BGM_Stop();
        SongSelect_Reset();
    }
    
    g_game.bgaLoop = (state == STATE_MENU_ENTER || state == STATE_MENU_INPUT ||
                      state == STATE_EXIT);
    if (g_game.bgaLoop) {
        g_game.bgaLoopStart = findBGALoopStart();
        g_game.bgaLoopEnd = findBGALoopEnd();
        g_game.bgaFrame = g_game.bgaLoopStart;
        Log_Print("BGA: loop range %d -> %d (maxFrame=%d, layers=%d)\n",
                  g_game.bgaLoopStart, g_game.bgaLoopEnd,
                  g_game.bgaMaxFrame,
                  g_game.bgaPicCount > 0 ? g_game.bgaPics[0].layerCount : 0);
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
    g_game.confirmActive = false;
    g_game.confirmTimer = 0;
    Render_SetGlobalColor(0, 0, 0, 0);
    LoadBGAForState(newState);
}

void Game_Init(HINSTANCE hInstance) {
    memset(&g_game, 0, sizeof(g_game));
    g_game.hInstance = hInstance;
    g_game.screenWidth = 640;
    g_game.screenHeight = 480;
    g_game.confirmActive = false;
    g_game.confirmTimer = 0;
    g_game.globalScaleX = 1.0f;
    g_game.globalScaleY = 1.0f;
    g_game.globalAlpha = 1.0f;
    g_game.showDebug = true;
    g_game.stageBreak = 1;
    g_game.showHelp = 0;
    Render_SetGlobalColor(0, 0, 0, 0);
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

    Game_ChangeState(STATE_LOGO_ENTER);
    g_game.lastTime = timeGetTime();
}

void Game_Shutdown(void) {
    BGM_Shutdown();
    Font_Shutdown();
    ShutdownSystems();
}



void Game_Update(float dt) {
    Input_Update();
    if (Input_IsKeyHit(VK_F11)) g_game.showDebug = !g_game.showDebug;

    if (Input_IsKeyHit(VK_ESCAPE)) {
        GameState s = g_game.state;
        BGM_Stop();
        Menu_ResetState();
        memset(g_game.input.padPrevState, 0, sizeof(g_game.input.padPrevState));

        if (s == STATE_WARNING_INIT || s == STATE_WARNING_ANIM || s == STATE_WARNING_END) {
            Game_ChangeState(STATE_LOGO_ENTER);
            return;
        }

        if (s == STATE_STAFF || s == STATE_STAFF_ENTER) {
            Game_ChangeState(STATE_MENU_ENTER);
            return;
        }

        if (s == STATE_GAMEPLAY || s == STATE_GAME_INIT) {
            Resource_ClearBGA();
            Game_ChangeState(STATE_MENU_ENTER);
            return;
        }

        Resource_ClearBGA();
        Game_ChangeState(STATE_MENU_ENTER);
    }

    g_game.stateFrame++;


    if (g_game.bgaPicCount > 0 && g_game.state != STATE_WARNING_END) {
        bool manualBGA = (g_game.state == STATE_GAMEPLAY ||
                          g_game.state == STATE_GAMEOPTION_ENTER ||
                          g_game.state == STATE_GAMEOPTION_ANIM ||
                          g_game.state == STATE_GAMEOPTION ||
                          g_game.state == STATE_GAMEOPTION_EXIT ||
                          g_game.state == STATE_STAFF);
        if (!manualBGA) {
            g_game.bgaTimer += dt;
            if (g_game.bgaTimer >= 1.0f / 60.0f) {
                g_game.bgaTimer -= 1.0f / 60.0f;
                if (g_game.bgaLoop && g_game.bgaLoopEnd > g_game.bgaLoopStart) {
                    g_game.bgaFrame++;
                    if (g_game.bgaFrame > g_game.bgaLoopEnd) {
                        Log_Print("BGA: wrap %d -> %d\n", g_game.bgaFrame, g_game.bgaLoopStart);
                        g_game.bgaFrame = g_game.bgaLoopStart;
                    }
                } else {
                    if (g_game.bgaFrame < g_game.bgaMaxFrame) {
                        g_game.bgaFrame++;
                    }
                }
            }
        }
    }

    switch (g_game.state) {
    case STATE_WARNING_INIT:
    case STATE_WARNING_ANIM:
    case STATE_WARNING_END:
        Gamestate_UpdateWarning(dt);
        break;
    case STATE_LOGO_ENTER:
    case STATE_LOGO_UPDATE:
    case STATE_LOGO_SKIP:
        Gamestate_UpdateLogo(dt);
        break;
    case STATE_MENU_ENTER:
    case STATE_MENU_INPUT:
        Gamestate_UpdateMenu(dt);
        break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B:
        Gamestate_UpdateSongSelect(dt);
        break;
    case STATE_LOADING_PNZ:
    case STATE_LOADING_PNZ_B:
        Loading_Update(dt);
        break;
    case STATE_GAMEPLAY:
        Gameplay_Update(dt);
        break;
    case STATE_STAFF_ENTER:
        Staff_Enter();
        break;
    case STATE_STAFF:
        Staff_Update(dt);
        break;
    case STATE_STAFF_END:
        BGM_Stop();
        Menu_ResetState();
        Game_ChangeState(STATE_MENU_ENTER);
        break;
    case STATE_GAMEOPTION_ENTER:
    case STATE_GAMEOPTION_ANIM:
    case STATE_GAMEOPTION:
    case STATE_GAMEOPTION_EXIT:
        Gamestate_UpdateGameOption(dt);
        break;
    case STATE_RESET_WARNING:
    Game_ChangeState(STATE_LOGO_ENTER);
        break;
    case STATE_EXIT:
        if (g_game.stateFrame < 30) {
            float a = g_game.globalColorA + dt * 2.0f;
            if (a > 1.0f) a = 1.0f;
            Render_SetGlobalColor(0, 0, 0, a);
        }
        if (g_game.stateFrame >= 30)
            PostQuitMessage(0);
        break;
    default:
        break;
    }
    memcpy(g_game.input.prevKeys, g_game.input.keys, sizeof(g_game.input.keys));
}

static void Render_StateInfo(void) {
    static uint32_t lastFpsTime = 0;
    static int fpsCount = 0;
    static float currentFps = 0;
    char buf[256];
    int y = 8;

    fpsCount++;
    uint32_t now = timeGetTime();
    if (now - lastFpsTime >= 1000) {
        currentFps = fpsCount / ((now - lastFpsTime) / 1000.0f);
        fpsCount = 0;
        lastFpsTime = now;
    }

    // FPS no canto inferior direito
    snprintf(buf, sizeof(buf), "FPS: %.1f", currentFps);
    int fw = (int)strlen(buf) * 8;
    Font_DrawString(g_game.screenWidth - fw - 8, g_game.screenHeight - 24,
                    buf, 1.0f, 1.0f, 0.0f, 1.0f);

    snprintf(buf, sizeof(buf), "State: %s  Frame: %u/%u",
             State_ToString(g_game.state), g_game.stateFrame, g_game.frameCounter);
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
    
    if (g_game.bgm.buffer || g_game.bgm.useMCI) {
        snprintf(buf, sizeof(buf), "BGM: %s | mode=%s | playing=%d",
                 g_game.bgm.name[0] ? g_game.bgm.name : "(unnamed)",
                 g_game.bgm.useMCI ? "MCI" : "DSound",
                 g_game.bgm.playing);
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
    if (g_game.state != STATE_WARNING_INIT && g_game.state != STATE_WARNING_ANIM && 
        g_game.state != STATE_WARNING_END &&
        g_game.state != STATE_LOGO_ENTER && g_game.state != STATE_LOGO_UPDATE &&
        g_game.state != STATE_LOGO_SKIP) {
        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(640, 0);
        glVertex2f(640, 480);
        glVertex2f(0, 480);
        glEnd();
        glColor4f(1, 1, 1, 1);
    }

    if (g_game.bgaPicCount > 0 &&
        g_game.state != STATE_LOGO_SKIP &&
        g_game.state != STATE_GAMEOPTION_ENTER &&
        g_game.state != STATE_GAMEOPTION_ANIM &&
        g_game.state != STATE_GAMEOPTION &&
        g_game.state != STATE_GAMEOPTION_EXIT) {
        BGA_Render(0, g_game.bgaFrame);
    }

    switch (g_game.state) {
        case STATE_MENU_ENTER:
        case STATE_MENU_INPUT:
        case STATE_EXIT:
            Gamestate_RenderMenu(0, g_game.bgaFrame);
            break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B:
        Gamestate_RenderSongSelect();
        break;
    case STATE_LOADING_PNZ:
    case STATE_LOADING_PNZ_B:
        Loading_Render();
        break;
    case STATE_GAMEPLAY:
        Gameplay_Render();
        break;
    case STATE_GAMEOPTION_ENTER:
    case STATE_GAMEOPTION_ANIM:
    case STATE_GAMEOPTION:
    case STATE_GAMEOPTION_EXIT:
        Gamestate_RenderGameOption();
        break;
    default:
        break;
    }

    if (g_game.showDebug) Render_StateInfo();
    Render_EndScene();
}

void Game_MainLoop(void) {
    bool running = true;
    uint32_t lastTick = (timeGetTime() * 240) / 1000;

    while (running) {
        if (!Window_ProcessMessages()) {
            running = false;
            break;
        }

        uint32_t tick = (timeGetTime() * 240) / 1000;

        if (3 < tick - lastTick) {
            lastTick = tick;
            g_game.frameCounter++;
            Game_Update(1.0f / 60.0f);
            Game_Render();
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
