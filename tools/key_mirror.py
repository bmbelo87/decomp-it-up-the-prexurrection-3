"""
key_mirror.py - espelha o teclado fisico para a janela do PUMPY.EXE original.

Uso tipico: jogar no PUMPYTESTE.EXE (em foco) e o original recebe as mesmas
teclas, para comparar os dois lado a lado.

Evidencia (PUMPY.EXE):
  - Nao importa GetAsyncKeyState/GetKeyState/DirectInput: teclado so via WndProc.
  - WndProc 0x419ce0: WM_KEYDOWN/WM_KEYUP usam so o wParam (VK); lParam ignorado.
  - PUMPPAD.DLL faz o mesmo: SendMessageA(hWnd, WM_KEYDOWN/UP, VK, 0).
  - WM_ACTIVATE com a janela minimizada posta WM_CLOSE -> NAO minimize o original.

Somente biblioteca padrao (ctypes). Windows apenas.

    python tools/key_mirror.py                  # alvo PUMPY.EXE, teclas de jogo
    python tools/key_mirror.py --exe PUMPY.EXE --all
    python tools/key_mirror.py --list           # lista janelas visiveis e sai
"""

import argparse
import ctypes
import os
import sys
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

WH_KEYBOARD_LL = 13
WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP = 0x100, 0x101, 0x104, 0x105
LLKHF_INJECTED = 0x10
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

ULONG_PTR = ctypes.c_size_t
LRESULT = ctypes.c_ssize_t


class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("vkCode", wintypes.DWORD), ("scanCode", wintypes.DWORD),
                ("flags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


HOOKPROC = ctypes.WINFUNCTYPE(LRESULT, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM)
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, wintypes.HINSTANCE, wintypes.DWORD]
user32.SetWindowsHookExW.restype = wintypes.HHOOK
user32.CallNextHookEx.argtypes = [wintypes.HHOOK, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM]
user32.CallNextHookEx.restype = LRESULT
user32.UnhookWindowsHookEx.argtypes = [wintypes.HHOOK]
user32.PostMessageA.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsWindow.argtypes = [wintypes.HWND]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetForegroundWindow.restype = wintypes.HWND
user32.GetMessageW.argtypes = [ctypes.POINTER(wintypes.MSG), wintypes.HWND, wintypes.UINT, wintypes.UINT]
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HMODULE

# Teclas de jogo (mesmos VKs do PUMPPAD.DLL e do port) + navegacao/servico.
GAME_VKS = {
    0x51, 0x45, 0x53, 0x5A, 0x43,          # P1: Q E S Z C
    0x24, 0x21, 0x0C, 0x23, 0x22,          # P2: HOME PRIOR CLEAR END NEXT
    0x1B, 0x0D, 0x20,                      # ESC ENTER SPACE
    0x25, 0x26, 0x27, 0x28,                # setas
    0xC0,                                  # VK_OEM_3 (console)
} | set(range(0x70, 0x7C))                 # F1..F12

# Teclado numerico com NumLock ligado gera VK_NUMPADx; o original espera os VKs
# de navegacao (o port faz a mesma traducao em input.c: KP_7 -> VK_HOME etc.).
NUMPAD_TO_NAV = {0x67: 0x24, 0x69: 0x21, 0x65: 0x0C, 0x61: 0x23, 0x63: 0x22}


def process_image(hwnd):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
    if not h:
        return pid.value, ""
    buf = ctypes.create_unicode_buffer(1024)
    size = wintypes.DWORD(len(buf))
    ok = kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size))
    kernel32.CloseHandle(h)
    return pid.value, (buf.value if ok else "")


def window_title(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(hwnd, buf, 256)
    return buf.value


def enum_windows():
    out = []

    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            pid, img = process_image(hwnd)
            out.append((hwnd, pid, img, window_title(hwnd)))
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return out


def find_target(exe_name):
    exe_name = exe_name.lower()
    hits = [w for w in enum_windows() if os.path.basename(w[2]).lower() == exe_name]
    return hits


def main():
    ap = argparse.ArgumentParser(description="Espelha o teclado para a janela do PUMPY.EXE original.")
    ap.add_argument("--exe", default="PUMPY.EXE", help="nome do executavel alvo (padrao: PUMPY.EXE)")
    ap.add_argument("--all", action="store_true", help="espelha todas as teclas, nao so as de jogo")
    ap.add_argument("--quiet", action="store_true", help="nao imprime cada tecla")
    ap.add_argument("--list", action="store_true", help="lista as janelas visiveis e sai")
    args = ap.parse_args()

    try:  # console cp1252 nao representa alguns titulos de janela
        sys.stdout.reconfigure(errors="replace")
    except AttributeError:
        pass

    if sys.platform != "win32":
        print("Somente Windows.")
        return 1

    if args.list:
        for hwnd, pid, img, title in enum_windows():
            print(f"hwnd=0x{hwnd or 0:08x} pid={pid:<6} {os.path.basename(img):<24} '{title}'")
        return 0

    hits = find_target(args.exe)
    if not hits:
        print(f"Nenhuma janela visivel de '{args.exe}'. Abra o original primeiro (use --list para conferir).")
        return 1
    if len(hits) > 1:
        print(f"Aviso: {len(hits)} janelas de '{args.exe}'; usando a primeira.")
    target = hits[0][0]
    print(f"Alvo: hwnd=0x{target:08x} pid={hits[0][1]} '{hits[0][3]}'")
    print("Espelhando", "todas as teclas" if args.all else "teclas de jogo", "- Ctrl+C para sair.")
    print("NAO minimize o original: ele fecha ao ser minimizado (WM_ACTIVATE -> WM_CLOSE).")

    state = {"hook": None, "count": 0}

    def hook_proc(nCode, wParam, lParam):
        if nCode == 0:
            kb = ctypes.cast(lParam, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
            if not (kb.flags & LLKHF_INJECTED) and wParam in (WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP):
                vk = NUMPAD_TO_NAV.get(kb.vkCode, kb.vkCode)
                # Nao duplica quando o proprio original esta em foco.
                if user32.GetForegroundWindow() != target and (args.all or vk in GAME_VKS):
                    msg = WM_KEYDOWN if wParam in (WM_KEYDOWN, WM_SYSKEYDOWN) else WM_KEYUP
                    user32.PostMessageA(target, msg, vk, 0)
                    state["count"] += 1
                    if not args.quiet:
                        print(f"{'DOWN' if msg == WM_KEYDOWN else 'UP  '} vk=0x{vk:02X}"
                              + (f" (de 0x{kb.vkCode:02X})" if vk != kb.vkCode else ""))
        return user32.CallNextHookEx(state["hook"], nCode, wParam, lParam)

    proc = HOOKPROC(hook_proc)  # manter referencia viva
    state["hook"] = user32.SetWindowsHookExW(WH_KEYBOARD_LL, proc, kernel32.GetModuleHandleW(None), 0)
    if not state["hook"]:
        print(f"SetWindowsHookExW falhou (erro {ctypes.get_last_error()}).")
        return 1

    msg = wintypes.MSG()
    try:
        while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) != 0:
            if not user32.IsWindow(target):
                print("A janela alvo fechou.")
                break
    except KeyboardInterrupt:
        pass
    finally:
        user32.UnhookWindowsHookEx(state["hook"])
        print(f"Encerrado. {state['count']} mensagens espelhadas.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
