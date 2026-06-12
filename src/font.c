#include "pumpy.h"

#define FONT_LIST_BASE 1000

static bool g_fontInit = false;

bool Font_Init(void) {
    if (g_fontInit) return true;
    HDC hDC = wglGetCurrentDC();
    if (!hDC) return false;

    HFONT hFont = CreateFontA(
        -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");

    if (!hFont)
        hFont = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);

    SelectObject(hDC, hFont);
    BOOL fontOk = wglUseFontBitmapsA(hDC, 0, 256, FONT_LIST_BASE);
    DeleteObject(hFont);

    if (!fontOk) {
        Log_Print("Font: wglUseFontBitmapsA FAILED (%lu)\n", GetLastError());
        Log_Flush();
        return false;
    }

    g_fontInit = true;
    Log_Print("Font: initialized (GDI bitmaps)\n");
    return true;
}

void Font_DrawChar(int x, int y, unsigned char c, float r, float g, float b, float a) {
    if (!g_fontInit) return;
    glColor4f(r, g, b, a);
    glRasterPos2i(x, y + 16);
    glCallList(FONT_LIST_BASE + c);
}

void Font_DrawString(int x, int y, const char* str, float r, float g, float b, float a) {
    if (!str || !g_fontInit) return;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    int ox = x;
    while (*str) {
        if (*str == '\n') { y += 16; x = ox; }
        else {
            glRasterPos2i(x, y + 16);
            glCallList(FONT_LIST_BASE + (unsigned char)*str);
            x += 8;
        }
        str++;
    }
    glPopAttrib();
}

void Font_DrawStringCentered(int x, int y, const char* str, float r, float g, float b, float a) {
    int len = (int)strlen(str);
    x -= (len * 8) / 2;
    Font_DrawString(x, y, str, r, g, b, a);
}

void Font_Shutdown(void) {
    if (g_fontInit) {
        glDeleteLists(FONT_LIST_BASE, 256);
        g_fontInit = false;
    }
}
