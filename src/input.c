#include "pumpy.h"

#define PUMPPAD_DLL "PUMPPAD.DLL"

typedef int (__stdcall *PadInitFunc)(void);
typedef int (__stdcall *PadGetStateFunc)(int player);
typedef int (__stdcall *PadCloseFunc)(void);

static HMODULE g_padDLL = NULL;
static PadInitFunc Pad_Init = NULL;
static PadGetStateFunc Pad_GetState = NULL;
static PadCloseFunc Pad_Close = NULL;

// PIU keyboard mapping: UL, UR, C, DL, DR
static const uint32_t KEY_PAD_MAP[2][PAD_BUTTONS_PER_PLAYER] = {
    { 'Q', 'E', 'S', 'Z', 'C' },
    { VK_HOME, VK_PRIOR, VK_CLEAR, VK_END, VK_NEXT }
};

bool Input_LoadPumpPad(void) {
    g_padDLL = LoadLibraryA(PUMPPAD_DLL);
    if (!g_padDLL) {
        Log_Print("Input: PUMPPAD.DLL not found, using keyboard stub\n");
        return false;
    }
    Pad_Init = (PadInitFunc)GetProcAddress(g_padDLL, "PadInit");
    Pad_GetState = (PadGetStateFunc)GetProcAddress(g_padDLL, "PadGetState");
    Pad_Close = (PadCloseFunc)GetProcAddress(g_padDLL, "PadClose");
    if (!Pad_Init || !Pad_GetState || !Pad_Close) {
        Log_Print("Input: PUMPPAD.DLL missing exports, using keyboard stub\n");
        FreeLibrary(g_padDLL);
        g_padDLL = NULL;
        return false;
    }
    if (Pad_Init() != 0) {
        Log_Print("Input: PadInit failed, using keyboard stub\n");
        FreeLibrary(g_padDLL);
        g_padDLL = NULL;
        return false;
    }
    Log_Print("Input: PUMPPAD.DLL loaded\n");
    return true;
}

void Input_Update(void) {
    int p;

    for (p = 0; p < 2; p++) {
        g_game.input.padPrevState[p] = g_game.input.padState[p];
        g_game.input.padState[p] = 0;
    }

    if (g_padDLL && Pad_GetState) {
        for (p = 0; p < 2; p++) {
            int state = Pad_GetState(p);
            if (state >= 0) {
                g_game.input.padConnected[p] = true;
                g_game.input.padState[p] = (uint32_t)state;
            }
        }
    }

    for (p = 0; p < 2; p++) {
        int b;
        for (b = 0; b < PAD_BUTTONS_PER_PLAYER; b++) {
            if (GetAsyncKeyState(KEY_PAD_MAP[p][b]) & 0x8000) {
                g_game.input.padState[p] |= (1 << b);
            }
        }
    }

}

bool Input_IsPadHit(int player, PadButton button) {
    uint32_t mask = 1 << button;
    return (g_game.input.padState[player] & mask) &&
           !(g_game.input.padPrevState[player] & mask);
}

bool Input_IsPadDown(int player, PadButton button) {
    return (g_game.input.padState[player] & (1 << button)) != 0;
}

bool Input_IsKeyHit(int key) {
    return g_game.input.keys[key] && !g_game.input.prevKeys[key];
}

bool Input_IsKeyDown(int key) {
    return g_game.input.keys[key];
}

void Input_Shutdown(void) {
    if (g_padDLL) {
        if (Pad_Close) Pad_Close();
        FreeLibrary(g_padDLL);
        g_padDLL = NULL;
    }
    Log_Print("Input: shutdown\n");
}
