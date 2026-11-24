// khd4k.dll 4K display, stage A (uhd=1): the engine keeps drawing its 800x600 frame; we make its window big (a scaled
// window, or borderless over the whole monitor without changing the display mode), draw that frame scaled into it,
// and give the engine back 800x600 coordinates for the mouse and its window size.
#include "common.h"
#include <windowsx.h>

static const char kMainClass[] = "OCGameApplicationWindow";
static HWND mainWnd;
static int clientW, clientH;              // our client size
static int imgX, imgY, imgW, imgH;        // where the 800x600 frame goes in the client
static void Publish();
RECT uhdImageRect;                        // the same, for battle.cpp (Present goes there)
extern volatile LONG battleDrew;
static WNDPROC engineProc;
BYTE* volatile frameBits;               // the engine's 800x600 frame DIB (bottom-up), seen at the last full present
int frameStride, frameH;

// the largest 4:3 rectangle whose height is a multiple of 120 (whole output pixels per 5-pixel block), centered
static void Layout(int cw, int ch)
{
    clientW = cw;
    clientH = ch;
    int h = ch < cw * 3 / 4 ? ch : cw * 3 / 4;
    int p = h / 120;
    if (p < 1) p = 1;
    if (cfg.text) p = UhdSetOutput(p * 120);
    imgH = 120 * p;
    imgW = 160 * p;
    imgX = (cw - imgW) / 2;
    imgY = (ch - imgH) / 2;
    SetRect(&uhdImageRect, imgX, imgY, imgX + imgW, imgY + imgH);
    Publish();
}

// tells the copy's helpers (the auto-advance driver and the subtitle window in KhdAutoBattle/KhdSubtitles) where the
// 800x600 picture is in our client: window properties "khd4k.pos" = MAKELONG(x, y), "khd4k.size" = MAKELONG(w, h)
static void Publish()
{
    if (!mainWnd || !imgW) return;
    SetPropA(mainWnd, "khd4k.pos", (HANDLE)(UINT_PTR)MAKELONG(imgX, imgY));
    SetPropA(mainWnd, "khd4k.size", (HANDLE)(UINT_PTR)MAKELONG(imgW, imgH));
}

HWND UhdMainWindow() { return mainWnd; }

static LPARAM MapIn(LPARAM lp)            // client (ours) -> client (800x600)
{
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    int fx = imgW ? (x - imgX) * 800 / imgW : x;
    int fy = imgH ? (y - imgY) * 600 / imgH : y;
    fx = fx < 0 ? 0 : (fx > 799 ? 799 : fx);
    fy = fy < 0 ? 0 : (fy > 599 ? 599 : fy);
    return MAKELPARAM(fx, fy);
}

static void PaintBars(HDC dc)
{
    if (!imgW || (imgX == 0 && imgY == 0 && imgW == clientW && imgH == clientH)) return;
    HBRUSH b = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT r;
    if (imgX > 0)
    {
        SetRect(&r, 0, 0, imgX, clientH); FillRect(dc, &r, b);
        SetRect(&r, imgX + imgW, 0, clientW, clientH); FillRect(dc, &r, b);
    }
    if (imgY > 0)
    {
        SetRect(&r, 0, 0, clientW, imgY); FillRect(dc, &r, b);
        SetRect(&r, 0, imgY + imgH, clientW, clientH); FillRect(dc, &r, b);
    }
}

// ---------------------------------------------------------------- the window
static LRESULT CALLBACK OurProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    {
        LPARAM in = lp;
        lp = MapIn(lp);
        static LONG seen;
        if (cfg.trace && m != WM_MOUSEMOVE && InterlockedIncrement(&seen) <= 40)
            Log("mouse %04x at (%d,%d) -> (%d,%d)", m, GET_X_LPARAM(in), GET_Y_LPARAM(in), GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        break;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && LOWORD(lp) && HIWORD(lp))
        {
            Layout(LOWORD(lp), HIWORD(lp));
            lp = MAKELPARAM(800, 600);
        }
        break;
    case WM_ERASEBKGND:
        PaintBars((HDC)wp);
        return 1;
    case WM_PAINT:
        if (cfg.text) UhdRepaint();
        break;
    }
    return CallWindowProcA(engineProc, h, m, wp, lp);
}

// battle.cpp: at the end of a fight the battle module puts the host's window procedure back (demonbane+0x1050,
// SetWindowLongA with the engine's own procedure), which would drop ours - the mouse would then reach the engine in our
// big window's pixels (user 10-16: after a FINISHERS fight clicks did nothing). Ours goes back in front of it.
WNDPROC UhdFrontProc(WNDPROC requested)
{
    if (!requested || requested == OurProc) return requested;
    engineProc = requested;
    return OurProc;
}

typedef ATOM(WINAPI* RegisterClassExA_t)(const WNDCLASSEXA*);
typedef HWND(WINAPI* CreateWindowExA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static RegisterClassExA_t realRegisterClassExA;
static CreateWindowExA_t realCreateWindowExA;

static ATOM WINAPI HookRegisterClassExA(const WNDCLASSEXA* wc)
{
    if (wc && wc->lpszClassName && HIWORD((DWORD)(UINT_PTR)wc->lpszClassName) && strcmp(wc->lpszClassName, kMainClass) == 0)
    {
        WNDCLASSEXA c = *wc;
        engineProc = c.lpfnWndProc;
        c.lpfnWndProc = OurProc;
        c.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        Log("main window class: our window procedure in front of the engine's");
        return realRegisterClassExA(&c);
    }
    return realRegisterClassExA(wc);
}

// uhd_offscreen (background tests, see below): dialogs - the quit question, ... - activate themselves inside user32, so a
// CBT hook on the game's window thread keeps every activation off (the tests click with window messages, which need none)
static HHOOK cbt;
static LRESULT CALLBACK QuietCbt(int code, WPARAM w, LPARAM l)
{
    if (code == HCBT_ACTIVATE)
    {
        static LONG said;
        if (said++ < 8)
        {
            char c[64] = "";
            GetClassNameA((HWND)w, c, sizeof c);
            Log("off-screen test: activation of a %s window (%p) kept off", c, (void*)w);
        }
        return 1;
    }
    return CallNextHookEx(cbt, code, w, l);
}

static HWND WINAPI HookCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                                       HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    bool isMain = !mainWnd && !parent && cls && HIWORD((DWORD)(UINT_PTR)cls) && strcmp(cls, kMainClass) == 0;
    if (!isMain) return realCreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    Log("main window asked: (%d,%d) %dx%d style %08lx", x, y, w, h, style);
    POINT pt = { x + w / 2, y + h / 2 };
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    if (cfg.full)
    {
        style = (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
        x = mi.rcMonitor.left;
        y = mi.rcMonitor.top;
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        Layout(w, h);
    }
    else
    {
        int cw = 800 * cfg.scale, ch = 600 * cfg.scale;
        RECT r = { 0, 0, cw, ch };
        AdjustWindowRectEx(&r, style, FALSE, ex);
        w = r.right - r.left;
        h = r.bottom - r.top;
        RECT wa = mi.rcWork;
        if (x + w > wa.right) x = wa.right - w;
        if (y + h > wa.bottom) y = wa.bottom - h;
        if (x < wa.left) x = wa.left;
        if (y < wa.top) y = wa.top;
        Layout(cw, ch);
    }
    if (cfg.offscreen) { x = 20000; y = 0; }           // right of every monitor: the left one is 150% and would scale us
    bool showLater = false;
    if (cfg.offscreen)                                 // background tests: made hidden, then shown without activation
    {
        if (!cbt) cbt = SetWindowsHookExW(WH_CBT, QuietCbt, NULL, GetCurrentThreadId());
        Log("off-screen test: activation hook on the window thread %s", cbt ? "set" : "NOT set");
        ex |= WS_EX_NOACTIVATE;
        showLater = (style & WS_VISIBLE) != 0;
        style &= ~WS_VISIBLE;
    }
    HWND hw = realCreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    mainWnd = hw;
    if (hw && showLater) ShowWindow(hw, SW_SHOWNA);
    Publish();
    Log("main window %p made: (%d,%d) %dx%d, frame at (%d,%d) %dx%d in a %dx%d client", hw, x, y, w, h, imgX, imgY, imgW, imgH, clientW, clientH);
    return hw;
}

// ---------------------------------------------------------------- geometry the engine asks about
typedef BOOL(WINAPI* GetClientRect_t)(HWND, LPRECT);
typedef BOOL(WINAPI* ClientToScreen_t)(HWND, LPPOINT);
static GetClientRect_t realGetClientRect;
static ClientToScreen_t realClientToScreen;

static BOOL WINAPI HookGetClientRect(HWND h, LPRECT r)
{
    BOOL main = h && (h == mainWnd || (!mainWnd && GetWindowLongPtrA(h, GWLP_WNDPROC) == (LONG_PTR)OurProc));
    if (cfg.trace) Log("GetClientRect caller %08lx hwnd %p main %d", Rva(_ReturnAddress()), h, main);
    if (main && r) { SetRect(r, 0, 0, 800, 600); return TRUE; }
    return realGetClientRect(h, r);
}

static BOOL WINAPI HookClientToScreen(HWND h, LPPOINT p)       // 800x600 client point -> the real screen point
{
    if (h && h == mainWnd && p && imgW)
    {
        p->x = imgX + p->x * imgW / 800;
        p->y = imgY + p->y * imgH / 600;
    }
    return realClientToScreen(h, p);
}

// ---------------------------------------------------------------- the picture
typedef BOOL(WINAPI* BitBlt_t)(HDC, int, int, int, int, HDC, int, int, DWORD);
static BitBlt_t nextBitBlt;

static BOOL WINAPI HookBitBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, DWORD rop)
{
    if (mainWnd && s && imgW && WindowFromDC(d) == mainWnd)
    {
        if (InterlockedExchange(&battleDrew, 0))              // a fight drew over the window: everything again
        {
            if (cfg.text) UhdRepaint();
            PaintBars(d);
        }
        if (x == 0 && y == 0 && w >= 800 && h >= 600)
        {
            DIBSECTION ds = {};
            HGDIOBJ bmp = GetCurrentObject(s, OBJ_BITMAP);
            if (bmp && GetObjectA(bmp, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits)
            {
                frameBits = (BYTE*)ds.dsBm.bmBits;
                frameStride = ds.dsBm.bmWidthBytes;
                frameH = ds.dsBm.bmHeight;
            }
        }
        if (cfg.text && x == 0 && y == 0 && w >= 800 && h >= 600 && UhdPresent(d, imgX, imgY))
        {
            PaintBars(d);
            return TRUE;
        }
        int dx = imgX + x * imgW / 800, dy = imgY + y * imgH / 600;
        int dw = imgX + (x + w) * imgW / 800 - dx, dh = imgY + (y + h) * imgH / 600 - dy;
        SetStretchBltMode(d, HALFTONE);
        SetBrushOrgEx(d, 0, 0, NULL);
        BOOL r = StretchBlt(d, dx, dy, dw, dh, s, sx, sy, w, h, rop);
        if (x == 0 && y == 0 && w >= 800 && h >= 600) PaintBars(d);
        return r;
    }
    return nextBitBlt(d, x, y, w, h, s, sx, sy, rop);
}

// ---------------------------------------------------------------- background tests (uhd_offscreen): never activate
// The engine shows its window, brings it forward (SetForegroundWindow) and places it (SetWindowPos) as any game does.
// Started while the user had been idle for a while (the foreground lock lapses), the off-screen window could take the
// foreground and the user's typing went into it (user 10-16: 「백그라운드에서 소리 끄고 실험해줘 자꾸 화면 가리면 내가
// 작업이 힘들어」). Child windows are left alone.
static BOOL WINAPI QuietSetForegroundWindow(HWND)
{
    static LONG said;
    if (!said++) Log("off-screen test: the engine's SetForegroundWindow ignored (the user keeps the foreground)");
    return TRUE;
}

static BOOL WINAPI QuietShowWindow(HWND h, int cmd)
{
    if (h && !(GetWindowLongW(h, GWL_STYLE) & WS_CHILD))
    {
        if (cmd == SW_SHOW) cmd = SW_SHOWNA;
        else if (cmd == SW_SHOWNORMAL || cmd == SW_RESTORE || cmd == SW_SHOWDEFAULT) cmd = SW_SHOWNOACTIVATE;
    }
    return ShowWindow(h, cmd);
}

static BOOL WINAPI QuietSetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    if (h && !(GetWindowLongW(h, GWL_STYLE) & WS_CHILD)) flags |= SWP_NOACTIVATE;
    return SetWindowPos(h, after, x, y, cx, cy, flags);
}

void UhdQuietWindows(HMODULE m)
{
    int n = (int)HookModuleImportIfAny(m, "USER32.dll", "SetForegroundWindow", (void*)QuietSetForegroundWindow)
          + (int)HookModuleImportIfAny(m, "USER32.dll", "ShowWindow", (void*)QuietShowWindow)
          + (int)HookModuleImportIfAny(m, "USER32.dll", "SetWindowPos", (void*)QuietSetWindowPos);
    Log("off-screen test: %d window call(s) of module %p kept from activating the game", n, m);
}

void InstallDisplay()
{
    if (cfg.offscreen)
    {
        UhdQuietWindows(sys);
        UhdQuietWindows(GetModuleHandleW(NULL));
    }
    HookImport("USER32.dll", "RegisterClassExA", (void*)HookRegisterClassExA, (void**)&realRegisterClassExA);
    HookImport("USER32.dll", "CreateWindowExA", (void*)HookCreateWindowExA, (void**)&realCreateWindowExA);
    HookImport("USER32.dll", "GetClientRect", (void*)HookGetClientRect, (void**)&realGetClientRect);
    HookImport("USER32.dll", "ClientToScreen", (void*)HookClientToScreen, (void**)&realClientToScreen);
    HookImport("GDI32.dll", "BitBlt", (void*)HookBitBlt, (void**)&nextBitBlt);
    InstallBattle();
    InstallMovie();
    Log("4K display (stage A) installed: %s", cfg.full ? "borderless full screen" : "window");
}
