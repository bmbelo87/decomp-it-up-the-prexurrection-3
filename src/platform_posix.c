/*
 * platform_posix.c — native implementations of the small Win32 helpers the game
 * uses, plus the SDL-based runtime bits shared by window.c / input.c / audio.c
 * (SDL_Init, event pump entry points) so the rest of the code stays portable.
 *
 * Only compiled on non-Windows (POSIX) targets; on Windows windows.h provides these.
 */
#include "pumpy.h"
#include <time.h>
#include <sys/time.h>

/* ---------------------------------------------------------------- timer --- */

static uint64_t clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint32_t timeGetTime(void) {
    return (uint32_t)(clock_ms() & 0xFFFFFFFFu);
}

void timeBeginPeriod(unsigned period) {
    (void)period;
}

void timeEndPeriod(unsigned period) {
    (void)period;
}

void Sleep(unsigned ms) {
    struct timespec req, rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &rem) == -1)
        req = rem;
}

/* ---------------------------------------------------------- path shims --- */

void GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer) {
    if (!lpBuffer || nBufferLength == 0) return;
    if (getcwd(lpBuffer, nBufferLength) == NULL)
        lpBuffer[0] = '\0';
}

void GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer) {
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    size_t n = strlen(tmp);
    if (n > 0 && tmp[n - 1] == '/') {
        snprintf(lpBuffer, nBufferLength, "%s", tmp);
    } else {
        snprintf(lpBuffer, nBufferLength, "%s/", tmp);
    }
}

BOOL DeleteFileA(LPCSTR lpFileName) {
    return remove(lpFileName) == 0 ? 1 : 0;
}

DWORD GetFileAttributesA(LPCSTR lpFileName) {
    return access(lpFileName, F_OK) == 0 ? 0x0 : INVALID_FILE_ATTRIBUTES;
}