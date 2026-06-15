#include "pumpy.h"

void Render_Clear(uint8_t r, uint8_t g, uint8_t b) {
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Render_SetOrtho(int width, int height) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, (GLdouble)width, 0, (GLdouble)height, 1.0, 0.0);
    glMatrixMode(GL_MODELVIEW);
}

void Render_Rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float yUp = 480.0f - y - h;
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    glBegin(GL_QUADS);
    glVertex2f(x, yUp);
    glVertex2f(x + w, yUp);
    glVertex2f(x + w, yUp + h);
    glVertex2f(x, yUp + h);
    glEnd();
}

void Render_RectOutline(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float yUp = 480.0f - y - h;
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, yUp);
    glVertex2f(x + w, yUp);
    glVertex2f(x + w, yUp + h);
    glVertex2f(x, yUp + h);
    glEnd();
}

void Render_Line(float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
}

void Render_BeginScene(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
}

void Render_EndScene(void) {
    if (g_game.globalColorA > 0.0f) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glColor4f(g_game.globalColorR, g_game.globalColorG, g_game.globalColorB, g_game.globalColorA);
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(640, 0);
        glVertex2f(640, 480);
        glVertex2f(0, 480);
        glEnd();
        glColor4f(1, 1, 1, 1);
    }
    Window_SwapBuffers();
}

void Render_SetGlobalColor(float r, float g, float b, float a) {
    g_game.globalColorR = r;
    g_game.globalColorG = g;
    g_game.globalColorB = b;
    g_game.globalColorA = a;
}

int Math_ROUND(float x) {
    return (int)(x + 0.5f);
}
