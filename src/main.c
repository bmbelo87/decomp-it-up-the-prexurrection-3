#include "pumpy.h"
#include "vsl.h"

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
    case STATE_GAMEOVER:         bgaName = "84"; break;
    case STATE_SONG_SELECT:
    case STATE_SONG_SELECT_B: bgaName = "099"; break;
    case STATE_SONG_TITLE:
    case STATE_SONG_TITLE_OUT: bgaName = ""; break;
    case STATE_STAFF_ENTER:
    case STATE_STAFF:
    case STATE_STAFF_END:        bgaName = ""; break;
    case STATE_STAGE_TRANSITION:
        if (g_game.isBonusSong)
            bgaName = "lt03";
        else
            bgaName = "lt01";
        break;
    case STATE_GAMEOVER_ENTER:   bgaName = "84"; break;
    case STATE_STAGE_BREAK:           bgaName = "083"; break; /* 083.DAT + 7-1.WAV pré-GameOver */
    case STATE_DANCE_GRADE_ENTER:
    case STATE_DANCE_GRADE_DISPLAY: bgaName = "83"; break;
    case STATE_HOWTOPLAY: bgaName = ""; break; /* limpa BGA do menu imediatamente; 03.DAT carrega no stateFrame==1 */
    case STATE_SERVICE_MENU: bgaName = ""; break; /* SETUP MENU desenha sobre fundo preto */
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
        if (g_game.stageCount == 3)
            SongSelect_Reset();
        else
            SongSelect_ResetIntro();
    }
    
    g_game.bgaLoop = (state == STATE_MENU_ENTER || state == STATE_MENU_INPUT ||
                      state == STATE_EXIT);
    if (g_game.bgaLoop) {
        g_game.bgaLoopStart = findBGALoopStart();
        g_game.bgaLoopEnd = findBGALoopEnd();
        g_game.bgaFrame = g_game.bgaLoopStart;
        Log_Print("BGA: loop range %d -> %d (maxFrame=%d)\n",
                  g_game.bgaLoopStart, g_game.bgaLoopEnd,
                  g_game.bgaMaxFrame);
    } else {
        g_game.bgaLoop = false;
        if (state == STATE_SONG_SELECT || state == STATE_SONG_SELECT_B) {
            g_game.bgaFrame = 0;
            if (g_game.bgaMaxFrame > 0) {
                g_game.bgaLoopStart = 0;
                g_game.bgaLoopEnd = 600;
                g_game.bgaLoop = true;
            }
        }
    }
}

/* Reseta todos os cheats de todos os players (ESC e Game Over). */
void Game_ResetAllCheats(void) {
    for (int _p = 0; _p < 2; _p++) {
        g_game.cmdSpeedMult[_p]      = 1;
        g_game.cmdSpeedRV[_p]        = false;
        g_game.cmdMirror[_p]         = false;
        g_game.cmdRandomStep[_p]     = false;
        g_game.cmdRandomVelocity[_p] = false;
        g_game.cmdEarthworm[_p]      = false;
        g_game.cmdFreedom[_p]        = false;
        g_game.cmdVanish[_p]         = false;
        g_game.cmdNonStep[_p]        = false;
    }
    Log_Print("CHEATS: reset global (ESC/GameOver)\n");
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
    /* g_game.stageBreak = 1; */ /* DISABLED: controlado por optionToggle1 no GameOption */
    g_game.showHelp = 0;
    g_game.cmdSpeedMult[0] = 1;    /* Command P1: velocidade padrão x1 */
    g_game.cmdSpeedMult[1] = 1;    /* Command P2: velocidade padrão x1 */
    g_game.cmdSpeedRV[0]   = false;
    g_game.cmdSpeedRV[1]   = false;
    g_game.activePlayerMask = 0x1; /* P1 ativo por padrão */
    g_game.isBattleMode = false;   /* BATTLE só ativo quando selecionado no song_select */
    Render_SetGlobalColor(0, 0, 0, 0);
    GetCurrentDirectoryA(MAX_PATH, g_game.currentDirectory);
    GameOption_Load(); /* lê PUMPY.INI — antes de qualquer sistema, para que o restante já veja os valores corretos */
    InitSystems();
    Audio_Init();
    Font_Init();
    Texture_Init();
    Audio_LoadAllWaves();
    Ranking_RegisterDefaults();  /* Game_InitState faz isso em 0x0040517c */

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
    Audio_Shutdown();
    BGM_Shutdown();
    Font_Shutdown();
    ShutdownSystems();
}



void Game_Update(float dt) {
    Input_Update();
    BGM_Update();   /* reinicia a faixa quando ela está em loop (ver audio.c) */
    if (Input_IsKeyHit(VK_F11)) g_game.showDebug = !g_game.showDebug;

    /* Crase abre/fecha o console de debug (0x29 no original) */
    if (Input_IsKeyHit(VK_OEM_3)) Debug_ConsoleToggle();

    /* Com o console aberto o jogo não enxerga teclado nem pad — as teclas são
     * entregues ao console pelo WndProc. O resto do update segue rodando para
     * as animações não congelarem. */
    if (Debug_ConsoleIsActive()) {
        memset(g_game.input.keys, 0, sizeof(g_game.input.keys));
        memset(g_game.input.padState, 0, sizeof(g_game.input.padState));
    }

    /* Alt+F4: encerra o jogo imediatamente de qualquer tela */
    if (Input_IsKeyHit(VK_F4) && (GetKeyState(VK_MENU) & 0x8000))
        PostQuitMessage(0);

    /* F1 abre o SETUP MENU de qualquer tela. Dentro do menu o F1 passa a ser o
     * TEST BUTTON (percorre a lista) e o F2 o SERVICE BUTTON (confirma) —
     * tratados em service_menu.c. */
    /* Botoeira do gabinete, na ordem que o I/O TEST do original lista:
     *   F1 TEST | F2 SERVICE | F3 CLEAR | F4 COIN1 | F5 COIN2
     * Fora do SETUP MENU o SERVICE dá crédito de cortesia (type 3, o mesmo que
     * incrementa g_nServiceTotal no original); dentro dele vira SELECT e o
     * I/O TEST lê as teclas por conta própria. */
    if (g_game.state != STATE_SERVICE_MENU) {
        if (Input_IsKeyHit(VK_F4)) Arcade_ProcessCoin(1);  /* COIN1   */
        if (Input_IsKeyHit(VK_F5)) Arcade_ProcessCoin(2);  /* COIN2   */
        if (Input_IsKeyHit(VK_F2)) Arcade_ProcessCoin(3);  /* SERVICE */
    }

    if (Input_IsKeyHit(VK_F1) && g_game.state != STATE_SERVICE_MENU) {
        ServiceMenu_Enter();
        /* Consome a borda do F1 antes de sair: este return pula o
         * memcpy(prevKeys, keys) do fim de Game_Update, e sem isso o mesmo
         * press seria lido de novo no frame seguinte, já como MOVE. */
        memcpy(g_game.input.prevKeys, g_game.input.keys, sizeof(g_game.input.keys));
        return;
    }

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

        /* ESC no SETUP MENU sai como a opção EXIT (página 9 → estado 4) */
        if (s == STATE_SERVICE_MENU) {
            ServiceMenu_Exit();
            return;
        }

        /* ESC de qualquer tela de jogo reseta os ponteiros de música por modo */
        SongSelect_ResetCreditIndices();

        if (s == STATE_GAMEPLAY || s == STATE_GAME_INIT) {
            Game_ResetAllCheats(); /* ESC durante gameplay: zera todos os cheats */
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
                          g_game.state == STATE_DANCE_GRADE_DISPLAY ||
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
    } else if (g_game.isVSL && g_vsl.active && g_game.bgaFrame < g_vsl.frameCount - 1) {
        g_game.bgaTimer += dt;
        if (g_game.bgaTimer >= 1.0f / 60.0f) {
            g_game.bgaTimer -= 1.0f / 60.0f;
            g_game.bgaFrame++;
            static int lastFrameAdvLog = -1;
            if (abs(g_game.bgaFrame - lastFrameAdvLog) >= 30) {
                Log_Print("VSL: bgaFrame advanced to %d (timer=%.4f, dt=%.4f)\n", g_game.bgaFrame, g_game.bgaTimer, dt);
                lastFrameAdvLog = g_game.bgaFrame;
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
    case STATE_SONG_TITLE:
    case STATE_SONG_TITLE_OUT:
        Loading_Update(dt);
        break;
    case STATE_GAMEPLAY:
        Gameplay_Update(dt);
        break;
    case STATE_DANCE_GRADE_ENTER:
        if (g_game.stateFrame == 1)
            Result_Enter();
        Result_Update(dt);
        break;
    case STATE_DANCE_GRADE_DISPLAY:
        Result_Update(dt);
        break;
    case STATE_STAGE_TRANSITION:
        if (g_game.stateFrame == 1) {
            Log_Print("STAGE: transition count=%d bonus=%d\n", g_game.stageCount, g_game.bonusStage);
        }
        if (g_game.stateFrame >= 60) {
            if (g_game.bonusStage && g_game.stageCount == 0 && !g_game.isBonusSong) {
                // Vai pro bonus stage
                Game_ChangeState(STATE_SONG_SELECT);
            } else if (g_game.isBonusSong) {
                // Bonus terminou, game over
                Game_ChangeState(STATE_GAMEOVER_ENTER);
            } else if (g_game.stageCount == 0 && !g_game.bonusStage) {
                // Sem bonus, game over
                Game_ChangeState(STATE_GAMEOVER_ENTER);
            } else {
                // Proximo stage
                Game_ChangeState(STATE_SONG_SELECT);
            }
        }
        break;
    case STATE_STAGE_BREAK:
        /* 083.DAT aparece enquanto 7-1.WAV toca; quando termina → GameOver */
        if (g_game.stateFrame == 1) {
            Game_ResetAllCheats();
            Audio_Play(g_waveSoundIds[SND_7_1], false);
        }
        if (g_game.stateFrame > 2 && !Audio_IsPlaying(g_waveSoundIds[SND_7_1])) {
            Game_ChangeState(STATE_GAMEOVER_ENTER);
        }
        break;
    case STATE_GAMEOVER_ENTER:
        if (g_game.stateFrame == 1) {
            BGM_Stop();
            Game_ResetAllCheats(); /* Game Over: zera todos os cheats (centralizado) */
            Render_SetGlobalColor(0, 0, 0, 0);
        }
        if (g_game.stateFrame >= 120) {
            Render_SetGlobalColor(0, 0, 0, 1);
        }
        if (g_game.stateFrame >= 150) {
            Resource_ClearBGA();
            Menu_ResetState();
            Game_ChangeState(STATE_MENU_ENTER);
        }
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
    case STATE_SERVICE_MENU:
        /* Só captura o input aqui; o desenho e a aplicação ficam em
         * ServiceMenu_UpdateRender, no Game_Render. A captura precisa ser
         * nesta fase porque Game_Update termina zerando as bordas do teclado
         * com memcpy(prevKeys, keys). */
        ServiceMenu_Update();
        break;
    case STATE_HOWTOPLAY:
        /* Frame 1: carrega 03.DAT e inicia 003.AUD (igual ao padrão GAMEOPTION_ENTER) */
        if (g_game.stateFrame == 1) {
            Resource_SwitchBGA("03");
            g_game.bgaFrame = 0;
            g_game.bgaLoop  = false;
            {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s\\AUDIO\\003.AUD", g_game.currentDirectory);
                BGM_Stop();
                if (BGM_LoadAUDDirect(path)) BGM_Play(true); /* loop=true: igual demais AUDs do jogo */
            }
        }
        /* Sai para SongSelect: CN do player ativo OU BGA terminou */
        if (g_game.stateFrame > 30) {
            bool skipCN = false;
            if (g_game.activePlayerMask & 0x1) skipCN |= Input_IsPadHit(0, PAD_C);
            if (g_game.activePlayerMask & 0x2) skipCN |= Input_IsPadHit(1, PAD_C);
            bool bgaEnded = (g_game.bgaMaxFrame > 0 && g_game.bgaFrame >= g_game.bgaMaxFrame);
            if (skipCN || bgaEnded) {
                BGM_Stop();
                Game_ChangeState(STATE_SONG_SELECT);
            }
        }
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
    int y = -4;

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
    Font_DrawString(g_game.screenWidth - fw - 8, 20,
                    buf, 1.0f, 1.0f, 0.0f, 1.0f);

    snprintf(buf, sizeof(buf), "State: %s  Frame: %u/%u",
             State_ToString(g_game.state), g_game.stateFrame, g_game.frameCounter);
    Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
    y += 16;

    if (g_game.isVSL && g_vsl.active) {
        int songId = 0;
        if (g_game.selectedModeIndex >= 0 && g_game.selectedModeIndex < g_game.songDB.modeCount &&
            g_game.selectedSongIndex >= 0) {
            SongMode* mode = &g_game.songDB.modes[g_game.selectedModeIndex];
            if (g_game.selectedSongIndex < mode->songCount)
                songId = mode->songIds[g_game.selectedSongIndex];
        }
        snprintf(buf, sizeof(buf), "BGA: %d.DAT - VSL | frame=%d/%d | meshs=%d",
                 songId, g_game.bgaFrame, g_vsl.frameCount, g_vsl.meshTableCount);
        Font_DrawString(8, y, buf, 1.0f, 1.0f, 0.0f, 1.0f);
        y += 16;
    } else if (g_game.bgaPicCount > 0) {
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

    if (g_game.isVSL && g_vsl.active) {
        VSL_Render(g_game.bgaFrame);
    } else if (g_game.bgaPicCount > 0 &&
        g_game.state != STATE_LOGO_SKIP &&
        g_game.state != STATE_DANCE_GRADE_DISPLAY &&
        g_game.state != STATE_GAMEOPTION_ENTER &&
        g_game.state != STATE_GAMEOPTION_ANIM &&
        g_game.state != STATE_GAMEOPTION &&
        g_game.state != STATE_GAMEOPTION_EXIT &&
        g_game.state != STATE_SONG_SELECT &&
        g_game.state != STATE_SONG_SELECT_B) {
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
    case STATE_SONG_TITLE:
    case STATE_SONG_TITLE_OUT:
        Loading_Render();
        break;
    case STATE_GAMEPLAY:
        Gameplay_Render();
        break;
    case STATE_DANCE_GRADE_DISPLAY:
        Result_Render();
        break;
    case STATE_STAGE_TRANSITION:
        if (g_game.isBonusSong) {
            Font_DrawStringCentered(320, 240, "BONUS STAGE", 1, 1, 0, 1);
        } else if (g_game.stageCount > 0) {
            Font_DrawStringCentered(320, 240, "NEXT STAGE", 1, 1, 1, 1);
        } else {
            Font_DrawStringCentered(320, 240, "FINAL STAGE", 1, 0, 0, 1);
        }
        break;
    case STATE_GAMEOVER_ENTER:
        Font_DrawStringCentered(320, 240, "GAME OVER", 1, 0, 0, 1);
        break;
    case STATE_GAMEOPTION_ENTER:
    case STATE_GAMEOPTION_ANIM:
    case STATE_GAMEOPTION:
    case STATE_GAMEOPTION_EXIT:
        Gamestate_RenderGameOption();
        break;
    case STATE_SERVICE_MENU:
        ServiceMenu_UpdateRender();
        break;
    default:
        break;
    }

    /* Console por último: é overlay, desenha sobre tudo */
    Debug_ConsoleRender();

    if (g_game.showDebug) Render_StateInfo();
    static int renderLogCount = 0;
    if (g_game.isVSL && renderLogCount < 30) {
        Log_Print("RENDER: state=%d isVSL=%d active=%d globalA=%.2f frame=%d fc=%d\n", g_game.state, g_game.isVSL, g_vsl.active, g_game.globalColorA, g_game.bgaFrame, g_vsl.frameCount);
        renderLogCount++;
    }
    if (renderLogCount < 5) {
        Log_Print("RENDER: state=%d isVSL=%d globalA=%.2f frame=%d\n", g_game.state, g_game.isVSL, g_game.globalColorA, g_game.bgaFrame);
        renderLogCount++;
    }
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
