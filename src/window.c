#include "pumpy.h"

static const char WINDOW_CLASS[] = "PUMP IT UP";
static const char WINDOW_TITLE[] = "PUMP IT UP - Pumpy Reconstructed";

/* Resolução lógica do jogo. Todo o desenho (SPR, BGA, fontes, HUD) assume
 * 640x480 — inclusive g_game.screenWidth/screenHeight, que várias telas usam
 * para centralizar. Portanto o tamanho real da janela NUNCA pode vazar para
 * esses campos: o que muda ao entrar em fullscreen é só o viewport. */
#define LOGICAL_W 640
#define LOGICAL_H 480

/* Mantém o 4:3 do gabinete: em monitor widescreen sobram barras pretas nas
 * laterais em vez de esticar a imagem. Para esticar, bastaria usar o client
 * rect inteiro no glViewport. */
static void Window_UpdateViewport(void) {
    RECT rc;
    /* WM_SIZE chega durante o CreateWindowEx, antes do contexto GL existir. */
    if (!g_game.hWnd || !g_game.hRC) return;
    if (!GetClientRect(g_game.hWnd, &rc)) return;

    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;

    int vw = cw;
    int vh = (cw * LOGICAL_H) / LOGICAL_W;
    if (vh > ch) {
        vh = ch;
        vw = (ch * LOGICAL_W) / LOGICAL_H;
    }
    glViewport((cw - vw) / 2, (ch - vh) / 2, vw, vh);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, (GLdouble)LOGICAL_W, 0, (GLdouble)LOGICAL_H, 1.0, 0.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        /* Só o viewport muda. Antes isto gravava o tamanho real da janela em
         * g_game.screenWidth/Height, e como todo o layout é 640x480 fixo, ao
         * redimensionar (ou entrar em fullscreen) tudo que centraliza por esses
         * campos saía do lugar — sem contar que a projeção continuava 640x480. */
        Window_UpdateViewport();
        return 0;
    case WM_ACTIVATEAPP:
        break;
    case WM_SYSCOMMAND:
        /* SC_KEYMENU: sem isto, soltar ALT abre o menu de sistema e o beep do
         * Windows toca a cada Alt+Enter. */
        if (wParam == SC_SCREENSAVE || wParam == SC_MONITORPOWER ||
            (wParam & 0xFFF0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_CHAR:
        /* Caracteres imprimíveis vão para o console quando ele está aberto.
         * A crase é a tecla que abre/fecha (0x29 no original), então nunca é
         * digitada. */
        if (Debug_ConsoleIsActive() && wParam != '`')
            Debug_ConsoleKeyHandler((int)wParam, 0);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        /* Alt+Enter alterna fullscreen. Com ALT pressionado o Enter chega como
         * WM_SYSKEYDOWN: bit 29 do lParam é o context code (ALT), bit 30 é o
         * estado anterior da tecla — testado para o autorepeat não ficar
         * alternando enquanto a tecla estiver segurada. */
        if (msg == WM_SYSKEYDOWN && wParam == VK_RETURN &&
            (lParam & (1 << 29)) && !(lParam & (1 << 30))) {
            Window_ToggleFullscreen();
            /* A janela some e reaparece no meio da combinação; sem limpar, o
             * jogo pode nunca ver o KEYUP e ficar com as teclas presas. */
            g_game.input.keys[VK_RETURN] = false;
            g_game.input.keys[VK_MENU]   = false;
            return 0;
        }
        g_game.input.keys[wParam & 0xFF] = true;
        /* Teclas de controle (setas, Home/End, Backspace, Insert...) chegam
         * como VK e não geram WM_CHAR. */
        if (Debug_ConsoleIsActive())
            Debug_ConsoleKeyHandler(0, (int)wParam);
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        g_game.input.keys[wParam & 0xFF] = false;
        return 0;
}
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static void SetupPixelFormat(HDC hDC) {
    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA,
        32,
        0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0,
        16,
        0, 0,
        PFD_MAIN_PLANE,
        0, 0, 0, 0
    };
    int pf = ChoosePixelFormat(hDC, &pfd);
    SetPixelFormat(hDC, pf, &pfd);
}

bool Window_Create(HINSTANCE hInstance, int width, int height, bool fullscreen) {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIconSm = LoadIconA(NULL, IDI_APPLICATION);

    ATOM atom = RegisterClassExA(&wc);
    if (!atom) {
        Log_Print("Window: RegisterClassEx failed (%lu)\n", GetLastError());
        return false;
    }

    g_game.screenWidth = width;
    g_game.screenHeight = height;
    g_game.isFullscreen = fullscreen;
    g_game.hInstance = hInstance;

    DWORD style = WS_POPUP | WS_VISIBLE;
    DWORD exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;
    int x = 0, y = 0;

    if (!fullscreen) {
        style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;
        RECT r = {0, 0, width, height};
        AdjustWindowRectEx(&r, style, FALSE, exStyle);
        width = r.right - r.left;
        height = r.bottom - r.top;
        x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    }

    g_game.hWnd = CreateWindowExA(
        exStyle, WINDOW_CLASS, WINDOW_TITLE, style,
        x, y, width, height,
        NULL, NULL, hInstance, NULL);

    if (!g_game.hWnd) {
        Log_Print("Window: CreateWindowEx failed (%lu)\n", GetLastError());
        return false;
    }

    g_game.hDC = GetDC(g_game.hWnd);
    SetupPixelFormat(g_game.hDC);
    g_game.hRC = wglCreateContext(g_game.hDC);
    if (!g_game.hRC) {
        Log_Print("Window: wglCreateContext failed (%lu)\n", GetLastError());
        return false;
    }
    wglMakeCurrent(g_game.hDC, g_game.hRC);

    if (fullscreen) {
        SetWindowPos(g_game.hWnd, HWND_TOPMOST, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_SHOWWINDOW);
    }

    /* Em fullscreen o client rect é o da tela, não 640x480 — por isso o viewport
     * sai daqui e não dos campos lógicos. */
    Window_UpdateViewport();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0f);

    ShowWindow(g_game.hWnd, SW_SHOW);
    SetForegroundWindow(g_game.hWnd);
    SetFocus(g_game.hWnd);

    Log_Print("Window: created (%dx%d, %s)\n",
              g_game.screenWidth, g_game.screenHeight,
              fullscreen ? "fullscreen" : "windowed");
    return true;
}

void Window_Destroy(void) {
    if (g_game.hRC) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_game.hRC);
        g_game.hRC = NULL;
    }
    if (g_game.hDC) {
        ReleaseDC(g_game.hWnd, g_game.hDC);
        g_game.hDC = NULL;
    }
    if (g_game.hWnd) {
        DestroyWindow(g_game.hWnd);
        g_game.hWnd = NULL;
    }
    UnregisterClassA(WINDOW_CLASS, g_game.hInstance);
    Log_Print("Window: destroyed\n");
}

void Window_SwapBuffers(void) {
    SwapBuffers(g_game.hDC);
}

void Window_ToggleFullscreen(void) {
    if (!g_game.hWnd) return;
    g_game.isFullscreen = !g_game.isFullscreen;
    if (g_game.isFullscreen) {
        SetWindowLongA(g_game.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongA(g_game.hWnd, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_APPWINDOW);
        SetWindowPos(g_game.hWnd, HWND_TOPMOST, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    } else {
        RECT r = {0, 0, LOGICAL_W, LOGICAL_H};
        SetWindowLongA(g_game.hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowLongA(g_game.hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
        AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        SetWindowPos(g_game.hWnd, HWND_NOTOPMOST,
                     (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                     (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
                     w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    }
    /* O WM_SIZE do SetWindowPos já chama isto, mas SWP_FRAMECHANGED nem sempre
     * gera WM_SIZE quando o tamanho não muda — refazer é barato. */
    Window_UpdateViewport();
    Log_Print("Window: %s\n", g_game.isFullscreen ? "fullscreen" : "windowed");
}

bool Window_ProcessMessages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return true;
}
