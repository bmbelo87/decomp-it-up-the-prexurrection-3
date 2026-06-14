#include "pumpy.h"
#include <stdarg.h>
#include <inttypes.h>

GameContext g_game = {0};

static FILE* g_logFile = NULL;

void Log_Print(const char* fmt, ...) {
    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    int written = vsnprintf(g_game.logBuffer + g_game.logPos, 
                            sizeof(g_game.logBuffer) - g_game.logPos, fmt, args);
    va_end(args);
    if (written > 0) {
        g_game.logPos += written;
        if (g_game.logPos >= (int)sizeof(g_game.logBuffer) - 1) {
            g_game.logPos = sizeof(g_game.logBuffer) - 1;
        }
    }
    OutputDebugStringA(g_game.logBuffer + g_game.logPos - written);
    if (!g_logFile) {
        g_logFile = fopen("pumpy.log", "w");
    }
    if (g_logFile) {
        vfprintf(g_logFile, fmt, args2);
        fflush(g_logFile);
    }
    va_end(args2);
}

void Log_Flush(void) {
    g_game.logPos = 0;
    g_game.logBuffer[0] = '\0';
}

float Math_Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float Math_Clamp(float v, float min, float max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

uint32_t Timer_GetTime(void) {
    return timeGetTime();
}

float Timer_GetDelta(void) {
    uint32_t now = Timer_GetTime();
    float dt = (now - g_game.lastTime) * 0.001f;
    g_game.lastTime = now;
    return dt;
}

void Timer_Wait(uint32_t ms) {
    Sleep(ms);
}

const char* State_ToString(GameState state) {
    switch (state) {
        case STATE_BOOT: return "BOOT";
        case STATE_WARNING_INIT: return "WARNING_INIT";
        case STATE_WARNING_ANIM: return "WARNING_ANIM";
        case STATE_WARNING_END: return "WARNING_END";
        case STATE_LOGO_ENTER: return "LOGO_ENTER";
        case STATE_LOGO_UPDATE: return "LOGO_UPDATE";
        case STATE_RESET_FLOW: return "RESET_FLOW";
        case STATE_MENU_FADE_IN: return "MENU_FADE_IN";
        case STATE_MENU_IDLE: return "MENU_IDLE";
        case STATE_MENU_INPUT_WAIT: return "MENU_INPUT_WAIT";
        case STATE_MENU_ENTER: return "MENU_ENTER";
        case STATE_MENU_INPUT: return "MENU_INPUT";
        case STATE_RESET_WARNING: return "RESET_WARNING";
        case STATE_GAME_INIT: return "GAME_INIT";
        case STATE_GAMEPLAY: return "GAMEPLAY";
        case STATE_GAMEOVER: return "GAMEOVER";
        case STATE_RESULT: return "RESULT";
        case STATE_GAMEOPTION_ENTER: return "GAMEOPTION_ENTER";
        case STATE_GAMEOPTION_ANIM: return "GAMEOPTION_ANIM";
        case STATE_GAMEOPTION: return "GAMEOPTION";
        case STATE_GAMEOPTION_EXIT: return "GAMEOPTION_EXIT";
        case STATE_SONG_SELECT: return "SONG_SELECT";
        case STATE_SONG_SELECT_B: return "SONG_SELECT_B";
        case STATE_LOGO_SKIP: return "LOGO_SKIP";
        case STATE_EXIT: return "EXIT";
        default: return "UNKNOWN";
    }
}

int Sprite_FindTile(const char* name)
{
    for (int i = 0; i < g_game.sprTileCount; i++)
    {
        if (_stricmp(g_game.sprTiles[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int Sprite_GetTexture(int tileIdx)
{
    if (tileIdx < 0 || tileIdx >= g_game.sprTileCount) return -1;
    return g_game.sprTiles[tileIdx].texId;
}

void Sprite_DrawTile(int tileIdx, float x, float y, float scaleX, float scaleY, float alpha)
{
    if (tileIdx < 0 || tileIdx >= g_game.sprTileCount) return;
    SPRTileDef* t = &g_game.sprTiles[tileIdx];
    if (t->texId < 0) return;

    // Aplicar espelhamento nas coordenadas UV
    float u1 = t->flipH ? (float)t->u2 : (float)t->u1;
    float v1 = t->flipV ? (float)t->v2 : (float)t->v1;
    float u2 = t->flipH ? (float)t->u1 : (float)t->u2;
    float v2 = t->flipV ? (float)t->v1 : (float)t->v2;

    if (t->u1 == 0 && t->v1 == 0 && t->u2 == 256 && t->v2 == 256 && !t->flipH && !t->flipV)
    {
        float w = (float)t->srcW * scaleX;
        float h = (float)t->srcH * scaleY;
        Texture_Draw(t->texId, x - w/2, y - h/2, w / Texture_GetWidth(t->texId),
                    h / Texture_GetHeight(t->texId), alpha);
    }
    else
    {
        float w = (float)t->srcW * scaleX;
        float h = (float)t->srcH * scaleY;
        Texture_DrawUV(t->texId, x - w/2, y - h/2, w, h,
                      u1, v1, u2, v2, 1.0f, 1.0f, 1.0f, alpha);
    }
}

void Sprite_DrawTileUV(int tileIdx, float x, float y, float w, float h, float alpha)
{
    if (tileIdx < 0 || tileIdx >= g_game.sprTileCount) return;
    SPRTileDef* t = &g_game.sprTiles[tileIdx];
    if (t->texId < 0) return;

    // Aplicar espelhamento nas coordenadas UV
    float u1 = t->flipH ? (float)t->u2 : (float)t->u1;
    float v1 = t->flipV ? (float)t->v2 : (float)t->v1;
    float u2 = t->flipH ? (float)t->u1 : (float)t->u2;
    float v2 = t->flipV ? (float)t->v1 : (float)t->v2;

    Texture_DrawUV(t->texId, x - w/2, y - h/2, w, h,
                  u1, v1, u2, v2, 1.0f, 1.0f, 1.0f, alpha);
}