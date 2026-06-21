#include "pumpy.h"

#define FONT_LIST_BASE 1000

static bool g_fontInit = false;

bool Font_Init(void) {
    if (g_fontInit) return true;
    HDC hDC = wglGetCurrentDC();
    if (!hDC) return false;

    HFONT hFont = CreateFontA(
        -16, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE,
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

void Font_DrawStringScaled(int x, int y, const char* str, float r, float g, float b, float a, float scale) {
    if (!str || !g_fontInit) return;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_TRANSFORM_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef((float)x, (float)y + 16.0f, 0.0f);
    glScalef(scale, scale, 1.0f);

    int ox = 0;
    while (*str) {
        if (*str == '\n') { /* skip */ }
        else {
            glRasterPos2i(ox, 0);
            glCallList(FONT_LIST_BASE + (unsigned char)*str);
            ox += 8;
        }
        str++;
    }
    glPopMatrix();
    glPopAttrib();
}

void Font_DrawStringCenteredScaled(int x, int y, const char* str, float r, float g, float b, float a, float scale) {
    int len = (int)strlen(str);
    x -= (int)((len * 8 * scale) / 2);
    Font_DrawStringScaled(x, y, str, r, g, b, a, scale);
}

int g_fontTexId = -1;

int Font_LoadTexture(void)
{
    if (g_fontTexId >= 0) return g_fontTexId;

    // Method 1: load from 00.DAT via RES system
    char datPath[MAX_PATH];
    snprintf(datPath, sizeof(datPath), "%s/BGA/00.DAT", g_game.currentDirectory);
    Log_Print("Font: trying RES load from '%s'\n", datPath);

    if (RES_Open(datPath)) {
        g_fontTexId = loadTextureFromRES("font.png");
        RES_Close();
        if (g_fontTexId >= 0) {
            Log_Print("Font: loaded via RES, texId=%d\n", g_fontTexId);
            return g_fontTexId;
        }
        Log_Print("Font: loadTextureFromRES failed\n");
    } else {
        Log_Print("Font: RES_Open failed for '%s'\n", datPath);
    }

    // Method 2: load directly from extracted file
    char extPath[MAX_PATH];
    snprintf(extPath, sizeof(extPath), "%s/BGA_extracted/00/FONT.PNG", g_game.currentDirectory);
    Log_Print("Font: trying direct load from '%s'\n", extPath);
    g_fontTexId = Texture_Load(extPath);
    if (g_fontTexId >= 0) {
        Log_Print("Font: loaded via direct file, texId=%d\n", g_fontTexId);
        return g_fontTexId;
    }

    Log_Print("Font: ALL loading methods failed for font texture\n");
    return -1;
}

void Font_DrawDigit(int texId, int digit, int x, int y, float scale)
{
    if (texId < 0 || digit < 0 || digit > 9) return;

    int col = digit % 8;
    int row = digit / 8;

    int u1 = col * 32;
    int v1 = row * 31 + 73;
    int u2 = u1 + 32;
    int v2 = v1 + 31;

    int w = (int)(36 * scale);
    int h = (int)(39 * scale);

    Texture_DrawUV(texId, (float)x, (float)y, (float)w, (float)h,
                   (float)u1, (float)v1, (float)u2, (float)v2,
                   1.0f, 1.0f, 1.0f, 1.0f);
}

void Font_DrawNumber(int texId, int x, int y, int number, int digits, float scale)
{
    int spacing = (int)(22 * scale);
    int totalW = digits * spacing;
    int startX = x - totalW / 2 + spacing;

    for (int i = digits - 1; i >= 0; i--)
    {
        int d = number % 10;
        number /= 10;
        Font_DrawDigit(texId, d, startX + i * spacing, y, scale);
    }
}

void Font_Shutdown(void) {
    if (g_fontInit) {
        glDeleteLists(FONT_LIST_BASE, 256);
        g_fontInit = false;
    }
}

// dec00.tga digit — igual FUN_0040c780. (x,y) em screen Y-DOWN
void Font_DrawDecDigit(int texId, float x, float y, int digit, float alpha)
{
    if (texId < 0 || digit < 0 || digit > 9) return;
    int col = digit % 5;
    int row = digit / 5;
    float yUp = 480.0f - y - 49.5f;
    float u0 = (float)col * 0.171875f;
    float u1 = u0 + 0.171875f;
    float vTga = (float)row * 0.17578125f + 0.65625f;  // TGA top do digito
    float vEnd = vTga + 0.17578125f;                     // TGA bottom (45/256 - igual Ghidra)
    Texture_Bind(texId);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, alpha);
    glBegin(GL_QUADS);
    // Ghidra: topo quad recebe TGA_top, base recebe TGA_bottom
    // Nosso loader flipa → base: 1-TGA_bottom, topo: 1-TGA_top
    glTexCoord2f(u0, 1.0f - vEnd); glVertex2f(x, yUp);
    glTexCoord2f(u1, 1.0f - vEnd); glVertex2f(x + 48.4f, yUp);
    glTexCoord2f(u1, 1.0f - vTga); glVertex2f(x + 48.4f, yUp + 49.5f);
    glTexCoord2f(u0, 1.0f - vTga); glVertex2f(x, yUp + 49.5f);
    glEnd();
}

// Combo number — igual FUN_0040c840. (centerX, centerY) em screen Y-DOWN
void Font_DrawDecNumber(int texId, float centerX, float centerY, int value, float alpha)
{
    if (texId < 0) return;
    if (value < 0) value = 0;
    if (value > 999) value = 999;
    int d1 = value / 100;
    int d2 = (value / 10) % 10;
    int d3 = value % 10;
    glPushMatrix();
    glTranslatef(centerX, 480.0f - centerY, 0.0f);  // screen → OpenGL Y-UP
    glTranslatef(24.0f, 0.0f, 0.0f);
    glScalef(1.1f, 1.1f, 1.0f);
    Font_DrawDecDigit(texId, 0.0f, 0.0f, d1, alpha);
    glTranslatef(-10.0f, 0.0f, 0.0f);
    Font_DrawDecDigit(texId, 0.0f, 0.0f, d2, alpha);
    glTranslatef(-10.0f, 0.0f, 0.0f);
    Font_DrawDecDigit(texId, 0.0f, 0.0f, d3, alpha);
    glPopMatrix();
}
