#ifndef PUMPY_H
#define PUMPY_H

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "song.h"
#include "step.h"

#define GAME_VERSION "0.3"
#define GAME_BUILD_DATE "Jun 12 2026"
#define GAME_BUILD_TIME "15:13:38"
#define TARGET_FPS 60
#define FRAME_TIME_MS (1000 / TARGET_FPS)

#define MAX_TEXTURES 2048
#define MAX_SOUNDS 64
#define MAX_BGA_PICS 50
#define MAX_BGA_EVENTS 6000
#define MAX_SPR_TILES 4096

#define PAD_BUTTONS_PER_PLAYER 5

typedef enum {
    // Original state values (from reverse engineering)
    STATE_BOOT            = 0x00,
    STATE_WARNING_INIT    = 0x01,
    STATE_WARNING_ANIM    = 0x02,
    STATE_WARNING_END     = 0x03,
    STATE_LOGO_ENTER      = 0x04,
    STATE_LOGO_UPDATE     = 0x05,
    STATE_RESET_FLOW      = 0x06,
    STATE_MENU_FADE_IN    = 0x07,
    STATE_MENU_IDLE       = 0x08,
    STATE_MENU_INPUT_WAIT = 0x09,
    STATE_MENU_ENTER      = 0x0A,
    STATE_MENU_INPUT      = 0x0B,
    STATE_RESET_WARNING   = 0x0C,
    STATE_UNKNOWN_D       = 0x0D,
    STATE_GAME_INIT       = 0x0E,
    STATE_GAMEPLAY        = 0x0F,
    STATE_GAMEOVER        = 0x10,
    STATE_RESULT          = 0x11,
    STATE_STAFF_ROLL      = 0x12,
    STATE_AUTO_EXIT       = 0x13,
    STATE_RANKING         = 0x14,
    STATE_RANKING_IN      = 0x15,
    STATE_RANKING_OUT     = 0x16,
    STATE_COURSE          = 0x17,
    STATE_COURSE_2        = 0x18,
    STATE_COURSE_3        = 0x19,
    STATE_BATTLE          = 0x1A,
    STATE_BATTLE_2        = 0x1B,
    STATE_BATTLE_3        = 0x1C,
    STATE_BATTLE_4        = 0x1D,
    STATE_BATTLE_5        = 0x1E,
    STATE_GAMEOPTION_ENTER = 0x1F,
    STATE_GAMEOPTION_ANIM  = 0x20,
    STATE_GAMEOPTION       = 0x21,
    STATE_GAMEOPTION_EXIT  = 0x22,
    STATE_STAFF_ENTER     = 0x23,
    STATE_STAFF           = 0x24,
    STATE_STAFF_END       = 0x25,

    // Our custom additions (high range to avoid conflicts)
    STATE_LOGO_SKIP       = 0x80,
    STATE_SONG_SELECT     = 0x81,
    STATE_SONG_SELECT_B   = 0x82,
    STATE_EXIT            = 0xFF,
} GameState;

typedef enum {
    PAD_UL    = 0,
    PAD_UR    = 1,
    PAD_C     = 2,
    PAD_DL    = 3,
    PAD_DR    = 4,
} PadButton;

typedef struct {
    GLuint id;
    int width;
    int height;
    int format;
    bool inUse;
    uint32_t lastFrame;
    char name[64];
} Texture;

typedef struct {
    LPDIRECTSOUNDBUFFER buffer;
    bool inUse;
    char name[64];
} Sound;

typedef struct {
    LPDIRECTSOUNDBUFFER buffer;
    bool playing;
    bool looping;
    char name[MAX_PATH];
    DWORD dataSize;
} BGM;

#pragma pack(push, 1)
typedef struct {
    int32_t frame;    // 0-3
    int32_t picIndex; // 4-7
    int32_t unk8;     // 8-11
    float x;          // 12-15
    float y;          // 16-19
    float w;          // 20-23
    float h;          // 24-27
    float sx;         // 28-31
    float sy;         // 32-35
    float u1;         // 36-39
    float v1;         // 40-43
    float u2;         // 44-47
    float v2;         // 48-51
    uint32_t pad[3];  // 52-63
} BGA2Event; // 64 bytes
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    int16_t frame;
    int16_t picIndex;
    int16_t flags;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t pad0;
    float sx;
    float sy;
    uint8_t pad1[12];
    float alpha;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t pad2;
} BGA1Event; // 0x2C bytes
#pragma pack(pop)

typedef struct {
    float x;
    float y;
    float hotx;
    float hoty;
    float scaleX;
    float scaleY;
    float rotation;
    float r;
    float g;
    float b;
    float a;
    int frame;
    int type;
    int z_order;
} BGAKeyframe;

typedef struct {
    char name[16];
    char texture[64];
    int srcX, srcY, srcW, srcH;
    int u1, v1, u2, v2;
    int texId;
    bool flipH, flipV;
} SPRTileDef;

#define MAX_BGA_LAYERS 64
#define MAX_BGA_KEYFRAMES 256

typedef struct {
    char filename[64];
    int isSPR;
    int kfCount;
    BGAKeyframe keyframes[MAX_BGA_KEYFRAMES];
    int sprTileStart;
    int sprTileCount;
    int texId;
    int aniFrameCount;
} BGALayer;

typedef struct {
    char name[64];
    int version;
    int layerCount;
    BGALayer layers[MAX_BGA_LAYERS];
    int maxFrame;
} BGAPicture;

typedef struct {
    bool keys[256];
    bool prevKeys[256];
    bool padConnected[2];
    uint32_t padState[2];
    uint32_t padPrevState[2];
    bool autoplay;
} InputState;

typedef struct {
    uint32_t score[2];
    uint32_t combo[2];
    int32_t life[2];
    uint32_t maxCombo[2];
    int perfectCount[2];
    int greatCount[2];
    int goodCount[2];
    int badCount[2];
    int missCount[2];
} GameplayStats;

typedef struct {
    HWND hWnd;
    HDC hDC;
    HGLRC hRC;
    HINSTANCE hInstance;
    
    LPDIRECTSOUND pDS;
    LPDIRECTSOUNDBUFFER pPrimaryBuffer;
    Sound sounds[MAX_SOUNDS];
    int soundCount;
    BGM bgm;
    
    Texture textures[MAX_TEXTURES];
    int textureCount;
    
    BGAPicture bgaPics[MAX_BGA_PICS];
    int bgaPicCount;
    int currentBGA;
    int bgaFrame;
    int bgaMaxFrame;
    float bgaTimer;
    bool bgaLoop;
    int bgaLoopStart;
    int bgaLoopEnd;
    int bgaStateIndices[16];
    
    SPRTileDef sprTiles[MAX_SPR_TILES];
    int sprTileCount;
    
    GameState state;
    GameState nextState;
    GameState fadeTarget;
    uint32_t frameCounter;
    uint32_t stateFrame;
    uint32_t flags;
    
    int screenWidth;
    int screenHeight;
    bool isFullscreen;
    bool vsync;
    
    InputState input;
    GameplayStats stats;
    
    SongDB songDB;
    StepSong currentSong;
    int selectedSongIndex;
    int selectedModeIndex;
    int selectedDifficulty;
    int songSelectScroll;
    int songSelectHighlighted;
    int previewSongId;
    
    float globalScaleX;
    float globalScaleY;
    float globalAlpha;
    float globalColorR;
    float globalColorG;
    float globalColorB;
    float globalColorA;
    float fadeAlpha;
    
    bool confirmActive;
    uint32_t confirmTimer;

    int optionDifficulty; // 0=Easy, 1=Normal, 2=Hard
    int optionToggle1;    // opcao toggle 1
    int optionToggle2;    // opcao toggle 2
    int optionCurrentItem; // 0-4
    int stageBreak;       // Stage Break global (0=OFF, 1=ON)
    int showHelp;         // Show Help global (0=OFF, 1=ON)
    
    bool showDebug;
    uint32_t timerId;
    uint32_t lastTime;
    float deltaTime;
    
    char currentDirectory[MAX_PATH];
    char logBuffer[4096];
    int logPos;
} GameContext;

extern GameContext g_game;

void Game_Init(HINSTANCE hInstance);
void Game_Shutdown(void);
void Game_MainLoop(void);
void Game_ChangeState(GameState newState);
void Game_Update(float dt);
void Game_Render(void);

void Log_Print(const char* fmt, ...);
void Log_Flush(void);

float Math_Lerp(float a, float b, float t);
float Math_Clamp(float v, float min, float max);

uint32_t Timer_GetTime(void);
float Timer_GetDelta(void);
void Timer_Wait(uint32_t ms);
const char* State_ToString(GameState state);

bool Window_Create(HINSTANCE hInstance, int width, int height, bool fullscreen);
void Window_Destroy(void);
void Window_SwapBuffers(void);
bool Window_ProcessMessages(void);
void Window_ToggleFullscreen(void);

bool Font_Init(void);
void Font_DrawChar(int x, int y, unsigned char c, float r, float g, float b, float a);
void Font_DrawString(int x, int y, const char* str, float r, float g, float b, float a);
void Font_DrawStringCentered(int x, int y, const char* str, float r, float g, float b, float a);
void Font_Shutdown(void);

void Texture_Init(void);
int Texture_Load(const char* name);
int Texture_LoadFromMemory(const uint8_t* buf, uint32_t bufSize, const char* debugName);
void Texture_Unload(int id);
void Texture_Bind(int id);
int Texture_GetWidth(int id);
int Texture_GetHeight(int id);
void Texture_Draw(int id, float x, float y, float scaleX, float scaleY, float alpha);
void Texture_DrawUV(int id, float x, float y, float w, float h, float u1, float v1, float u2, float v2, float r, float g, float b, float alpha);
void Texture_Shutdown(void);

bool Audio_Init(void);
int Audio_LoadWAV(const char* name, const uint8_t* data, DWORD size);
int Audio_LoadFromResource(const char* name, int resId);
void Audio_Play(int id, bool loop);
void Audio_Stop(int id);
void Audio_StopAll(void);
void Audio_SetVolume(int id, long volume);
void Audio_Shutdown(void);

bool BGM_Load(const char* path);
bool BGM_LoadWAV(const char* path);
bool BGM_LoadMP3(const char* path);
bool BGM_LoadAUD(int songId, bool preview);
void BGM_Play(bool loop);
void BGM_Stop(void);
void BGM_SetVolume(long volume);
void BGM_Shutdown(void);

bool Input_LoadPumpPad(void);
void Input_Update(void);
bool Input_IsPadHit(int player, PadButton button);
bool Input_IsPadDown(int player, PadButton button);
bool Input_IsKeyHit(int key);
bool Input_IsKeyDown(int key);
void Input_Shutdown(void);

bool BGA_Load(const char* path);
bool BGA_LoadFromMemory(const uint8_t* data, uint32_t size, bool isBGA2);
void BGA_Render(int bgaIndex, int frame);
void BGA_SetEventFrame(int bgaIndex, int frame);
void BGA_SetEventLayer(int bgaIndex, int frame, int layerIdx);
void BGA_Reset(void);
void BGA_Shutdown(void);

extern int g_menuSelection;

void Gamestate_UpdateWarning(float dt);
void Gamestate_UpdateLogo(float dt);
void Gamestate_UpdateMenu(float dt);
void Gamestate_UpdateSongSelect(float dt);
void Gamestate_RenderMenu(int bgaIndex, int frame);
void Gamestate_RenderSongSelect(void);

void Gameplay_Enter(void);
void Gameplay_Exit(void);
void Gameplay_Update(float dt);
void Gameplay_Render(void);

void Gamestate_UpdateGameOption(float dt);
void Gamestate_RenderGameOption(void);
void Gamestate_InitGameOption(void);

void Render_Clear(uint8_t r, uint8_t g, uint8_t b);
void Render_SetOrtho(int width, int height);
void Render_Rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void Render_RectOutline(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void Render_Line(float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void Render_BeginScene(void);
void Render_SetGlobalColor(float r, float g, float b, float a);

int Sprite_FindTile(const char* name);
void Sprite_DrawTile(int tileIdx, float x, float y, float scaleX, float scaleY, float alpha);
void Sprite_DrawTileUV(int tileIdx, float x, float y, float w, float h, float alpha);
int Sprite_GetTileW(int tileIdx);
int Sprite_GetTileH(int tileIdx);
void Render_EndScene(void);
int Math_ROUND(float x);

bool RES_Open(const char* path);
int RES_GetCount(void);
int RES_Find(const char* name);
const char* RES_GetName(int index);
uint32_t RES_GetSize(int index);
bool RES_Read(int index, uint8_t* buffer);
uint8_t* RES_ReadAlloc(int index);
void RES_Close(void);
bool RES_IsOpen(void);

void* Resource_Load(int resId, const char* type, DWORD* outSize);
int Resource_LoadWave(int resId, const char* name);
int Resource_LoadTexture(int resId, const char* name);
bool Resource_LoadBGA(int resId);
bool Resource_LoadBGAByName(const char* datName);
bool Resource_LoadBGADirect(const char* datPath);
bool Resource_LoadStage(const char* path);
void Resource_ClearBGA(void);
int Resource_LoadStateBGA(const char* stateName);
int Resource_GetStateBGAIndex(const char* stateName);
int Resource_SwitchBGA(const char* datName);

#endif