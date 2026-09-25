/*
 * platform_linux.h — Linux compatibility layer for the Windows-centric codebase.
 *
 * Provides the handful of Win32 types / macros / functions the game code relies
 * on, implemented natively on POSIX systems (Linux/macOS/BSD). Only included when
 * not building for Windows, where windows.h supplies the real ones.
 */
#ifndef PLATFORM_LINUX_H
#define PLATFORM_LINUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>

/* ---- base windows-ish scalar types ---- */
typedef int              BOOL;
typedef unsigned char    BYTE;
typedef unsigned short   WORD;
typedef unsigned long    DWORD;
typedef long             LONG;
typedef unsigned int     UINT;
typedef unsigned long    ULONG;
typedef int              INT;
typedef unsigned short   WCHAR;
typedef void*            HANDLE;
typedef const char*      LPCSTR;
typedef char*            LPSTR;

/* ---- opaque window/instance/device handles (only used as pointers) ---- */
typedef struct HWND__*   HWND;
typedef struct HDC__*    HDC;
typedef struct HGLRC__*  HGLRC;
typedef struct HINSTANCE__* HINSTANCE;
typedef struct HBRUSH__* HBRUSH;
typedef struct HFONT__*  HFONT;
typedef struct HMODULE__*    HMODULE;
typedef struct HRSRC__*  HRSRC;
typedef struct HGLOBAL__*    HGLOBAL;
typedef void*            ATOM;
typedef long             LRESULT;
typedef uintptr_t        WPARAM;
typedef long             LPARAM;

#define MAX_PATH 260

#define CALLBACK
#define WINAPI
#define __stdcall

/* ---- Virtual-key codes used by the engine ---- */
#define VK_BACK    0x08
#define VK_TAB     0x09
#define VK_RETURN  0x0D
#define VK_ESCAPE  0x1B
#define VK_SPACE   0x20
#define VK_PRIOR   0x21  /* PgUp */
#define VK_NEXT    0x22  /* PgDn */
#define VK_END     0x23
#define VK_HOME    0x24
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28
#define VK_INSERT  0x2D
#define VK_DELETE  0x2E
#define VK_CLEAR   0x0C
#define VK_MENU    0x12  /* Alt */
#define VK_OEM_3   0xC0  /* backquote */
#define VK_F1      0x70
#define VK_F2      0x71
#define VK_F3      0x72
#define VK_F4      0x73
#define VK_F5      0x74
#define VK_F6      0x75
#define VK_F7      0x76
#define VK_F8      0x77
#define VK_F9      0x78
#define VK_F10     0x79
#define VK_F11     0x7A
#define VK_F12     0x7B
#define VK_0 0x30
#define VK_1 0x31
#define VK_2 0x32
#define VK_3 0x33
#define VK_4 0x34
#define VK_5 0x35
#define VK_6 0x36
#define VK_7 0x37
#define VK_8 0x38
#define VK_9 0x39
#define VK_A 0x41
#define VK_B 0x42
#define VK_C 0x43
#define VK_D 0x44
#define VK_E 0x45
#define VK_F 0x46
#define VK_G 0x47
#define VK_H 0x48
#define VK_I 0x49
#define VK_J 0x4A
#define VK_K 0x4B
#define VK_L 0x4C
#define VK_M 0x4D
#define VK_N 0x4E
#define VK_O 0x4F
#define VK_P 0x50
#define VK_Q 0x51
#define VK_R 0x52
#define VK_S 0x53
#define VK_T 0x54
#define VK_U 0x55
#define VK_V 0x56
#define VK_W 0x57
#define VK_X 0x58
#define VK_Y 0x59
#define VK_Z 0x5A

/* ---- case-insensitive compare (MSVC compat) ---- */
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp

/* ---- timer / scheduler helpers (platform_posix.c) ---- */
extern uint32_t timeGetTime(void);
void timeBeginPeriod(unsigned period);
void timeEndPeriod(unsigned period);
void Sleep(unsigned ms);

/* ---- cross-platform path helpers ---- */
void GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
void GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
BOOL DeleteFileA(LPCSTR lpFileName);
DWORD GetFileAttributesA(LPCSTR lpFileName); /* 0xFFFFFFFF = not found */
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF

/* ---- microsoft debugger sink: no-op on Linux (pumpy.log carries the log) ---- */
#define OutputDebugStringA(s) ((void)0)

#endif /* PLATFORM_LINUX_H */