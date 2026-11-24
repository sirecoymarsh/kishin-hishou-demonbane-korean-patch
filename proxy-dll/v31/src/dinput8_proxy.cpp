// dinput8.dll proxy for the Win11 test copy of 機神飛翔デモンベイン (32-bit) - auto battle built into the game folder.
//
// The battle module (demonbane.dll) reads the keyboard with DirectInput8 GetDeviceState (+0xE44A, 256 bytes, background
// cooperative level), system.dll (story, menus) has a keyboard device of its own. This proxy:
//   * forwards DirectInput8Create to the system dinput8.dll and hooks CreateDevice / GetDeviceState in their vtables;
//   * while auto battle is on (agent=1 or the 자동 전투 check box on the battle settings page, 10-17), the fight's
//     keyboard reads give the keys the agent holds (shared memory Local\KhdAutoBattle_<pid>) and nothing of the person's
//     keyboard, pad or mouse; ESC alone asks whether to take that fight over - only for the reads made from
//     demonbane.dll's code, so the story and the menus never see the agent, and nothing goes to other windows (no
//     keybd_event / SendInput anywhere);
//   * while the agent runs and the game window is not in front, the person's typing elsewhere is kept out of the fight;
//   * starts the agent (autobattle\KhdAutoBattle.exe <pid> <map name>) and the battle subtitle window
//     (autobattle\KhdSubtitles.exe <pid>, subtitles=1) once, in a job object that ends them with the game;
//   * mutes this process's sound when autobattle.ini says mute=1 (unmutes once when mute=0);
//   * keeps the fight going behind other windows (background=1), shows the cursor again after every fight, and keeps
//     the display on while the game runs (a screen saver mid-fight made the game quit).
//   * keeps this copy's settings in a registry key of its own (own_settings=1), shows the game's hidden third difficulty
//     어려움 and swaps in the Hard pack (Normal's numbers, more skills) when it is chosen (hard=1).
//   * the settings window (10-17/18): Korean where the game left Japanese, 화면 크기, 자동 전투, and the 마도서 look
//     (settings_skin=1, pictures in autobattle\ui) with per-character voice levels on the sound page ([voice_volume],
//     applied to the story voices' sound buffers through voice_groups.tsv);
//   * the defeat screen's voice-only lines get subtitles (story_subs.tsv, shown by the subtitle window).
// If the agent is not running (agent=0, exe missing, or it stopped beating) the keyboard reads are left as they were.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <commctrl.h>
#include <dinput.h>
#include <mmsystem.h>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT(WINAPI* Plain_t)(void);
typedef HRESULT(WINAPI* GetClassObject_t)(REFCLSID, REFIID, LPVOID*);
typedef LPCDIDATAFORMAT(WINAPI* GetdfJoy_t)(void);
typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(void* self, REFGUID guid, void** dev, LPUNKNOWN outer);
typedef HRESULT(STDMETHODCALLTYPE* GetDeviceState_t)(void* self, DWORD cb, LPVOID data);

// shared with the agent; keep in step with khdbot_host.cs
struct Shared
{
    BYTE keys[256];            // agent: DIK codes, 0x80 = held
    volatile LONG agentBeat;   // +256 agent: changes every loop while it runs (0 = stopped)
    volatile LONG lastPoll;    // +260 proxy: GetTickCount of the battle module's last keyboard read
    volatile LONG polls;       // +264 proxy: how many of those reads
    volatile LONG foreground;  // +268 proxy: the game window was in front at that read
    volatile LONG version;     // +272 proxy: layout version
    volatile LONG runs;        // +276 proxy: battle module runs - a read after 2 s without one starts a new run (a fight loading)
};

// ---------------------------------------------------------------- auto battle on and off in the game (10-17)
// user 10-17: 「자동 전투는 옵션에서 선택 가능하게 했으면 해. 그리고 자동 전투가 돌아갈 동안 내 입력이 먹지 않도록.
// ESC를 눌렀을때만 자동 전투 중단이 뜨게.」 The battle settings page (system.dll dialog 184: difficulty 쉬움/보통/어려움
// and its check boxes) gets a check box 자동 전투 that writes agent= to autobattle.ini at once and starts the agent if it
// is not running yet. While the agent plays a fight, the fight reads its keys only - the person's keyboard, pad and
// mouse stay out - and ESC alone asks whether to take this fight over (yes: the person plays it, the next fight is
// automatic again).
static volatile LONG autoBattle;   // the setting: autobattle.ini agent=1, or the check box
static volatile LONG stoppedRun;   // the battle-module run (Shared::runs) the person took over; 0 = none
// 11-03 user 「자동전투 해제/실행을 듀얼 모드, 프리 파이트 모드, 전투 중에도 설정하게」→「모드마다 따로. 설정 옵션에서 모드별 컨트롤
// 가능하게 또 분리」: each mode keeps its own switch - the story agent=, FREE FIGHT free_auto=, DUEL duel_auto= - on the battle
// settings page (three boxes), on the FREE FIGHT and DUEL screens, and in the pause menu during a fight (that fight's mode).
static volatile LONG freeAuto;     // FREE FIGHT: autobattle.ini free_auto (without the line: as agent)
static volatile LONG duelAutoOn;   // DUEL: autobattle.ini duel_auto (the AI-vs-AI copies, and who gets PLAYER1 at the start)
static int fightKind;              // this fight (BeforeFight): 0 the story, 1 FREE FIGHT, 2 DUEL
static int hoMode = -1;            // duel_handover.h: 1 the AI plays PLAYER1 of this DUEL fight, 0 the person, -1 no hand-over
// 11-05 user 「과거 스토리 전투에서 노말 모드에서 로드 비야키 이기게 만들었던 AI 있어?」→「로드 비야키전만」: in DUEL, Ambrosius vs
// Lord Byakhee (manual scene 303) with 자동 전투 on stays the person's scene and the story agent's BATTLE-06 AI plays PLAYER1
// with keys, as in the story (autobattleattles.tsv row HMHV / MMDV -> B06). autobattle.ini hm_agent_mm=0: the AI-vs-AI copy again.
static bool duelAgentScene;        // this DUEL fight is that one (kept for the agent)
static bool duelAgentFight;        // ... and the agent plays it now (the pause menu's 자동 전투 row switches it)
static bool AgentMode() { return fightKind == 1 ? freeAuto != 0 : fightKind == 0 ? autoBattle != 0 : fightKind == 2 ? duelAgentFight : false; }
static int background = 1;         // autobattle.ini background=1: with auto battle the fight goes on behind other windows
static const int kAutoCheckId = 0x5A01;
static const int kBalanceCheckId = 0x5A02;   // DUEL 균형 모드 (10-19, the block "DUEL balance mode" further down)
static const int kFreeAutoCheckId = 0x5A04, kDuelAutoCheckId = 0x5A05;   // 11-03 자동 전투 per mode
static const int kDifficultyNoteId = 0x5A08;   // 11-09 what each difficulty changes (AddDifficultyNotes; 0x5A03 is DUEL's)
static volatile LONG duelBalance;            // the setting: autobattle.ini duel_balance=1, or the check box
static void SetDuelBalance(bool on);
static volatile LONG duelDifficulty = 1;
// 11-02 user 「균형 모드의 AI를 이지 노말 하드에서 일괄적으로 먼저 같게 할까?」→「일괄 적용」: the Normal-tuned AIs (hero five,
// Aion native, Kraken observe, native command adapter) run at every DUEL AI difficulty for now. Per-difficulty AI comes later:
// return duelDifficulty here then (the tier-1 routes and checks stay as written).
static inline LONG DuelAiTier() { return 1; }
static const DWORD kDuelMenuMask = 0x400007E2; // KHD_DUEL_SETTINGS_V1: native menu low bits preserved
static void UpdateDuelControls(HWND w);
static void DrainDuelMessages();
static void LoadDuelSettings(const wchar_t* ini);
static bool DuelMenuContext();
struct DuelAiRoute { int tier, source, loaded, destination, control; };
static DuelAiRoute aiRoutes[4096];
static int aiRouteCount;
static void LoadDuelAiRoutes();


static HINSTANCE selfInst;
static HMODULE real;
static DI8Create_t realCreate;
static CRITICAL_SECTION patchLock;
static Shared* shared;
static wchar_t gameDir[MAX_PATH], abDir[MAX_PATH], logPath[MAX_PATH];
static wchar_t battleModule[64] = L"demonbane.dll";
static HANDLE agentJob;

struct Patched { void** vt; void* orig; };
static Patched cdPatched[8];
static volatile LONG cdN;
static Patched gsPatched[16];
static volatile LONG gsN;

static void Log(const char* fmt, ...)
{
    if (!logPath[0]) return;
    char line[512];
    SYSTEMTIME t; GetLocalTime(&t);
    int n = _snprintf_s(line, sizeof line, _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d pid %lu  ", t.wYear, t.wMonth, t.wDay,
                        t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    va_list a; va_start(a, fmt);
    if (n > 0) _vsnprintf_s(line + n, sizeof line - n - 2, _TRUNCATE, fmt, a);   // 11-03: room for "\r\n" - a full line stopped the game
    va_end(a);
    strcat_s(line, "\r\n");
    HANDLE f = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, line, (DWORD)strlen(line), &w, NULL);
    CloseHandle(f);
}

static bool LoadReal()
{
    if (realCreate) return true;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);   // System32 - a 32-bit process is sent on to SysWOW64
    if (!n || n > MAX_PATH - 20) return false;
    wcscat_s(path, L"\\dinput8.dll");
    real = LoadLibraryW(path);
    if (!real) { Log("could not load the system dinput8.dll (error %lu)", GetLastError()); return false; }
    realCreate = (DI8Create_t)GetProcAddress(real, "DirectInput8Create");
    return realCreate != NULL;
}

static void* FindOrig(Patched* arr, LONG n, void** vt)
{
    for (LONG i = 0; i < n; i++)
        if (arr[i].vt == vt) return arr[i].orig;
    return NULL;
}

static bool PatchSlot(void** vt, int idx, void* hook, Patched* arr, volatile LONG* n, int cap)
{
    bool ok = false;
    EnterCriticalSection(&patchLock);
    if (FindOrig(arr, *n, vt)) ok = true;
    else if (vt[idx] == hook) ok = true;
    else if (*n < cap)
    {
        DWORD old;
        if (VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
        {
            arr[*n].vt = vt; arr[*n].orig = vt[idx];
            MemoryBarrier();
            InterlockedIncrement(n);          // the original is findable before the slot points at the hook
            InterlockedExchangePointer(&vt[idx], hook);
            VirtualProtect(&vt[idx], sizeof(void*), old, &old);
            ok = true;
        }
        else Log("VirtualProtect failed on a vtable (error %lu)", GetLastError());
    }
    LeaveCriticalSection(&patchLock);
    return ok;
}

// the read comes from the battle module's code (its return address lies inside demonbane.dll)
static bool FromBattle(void* ret)
{
    HMODULE m = GetModuleHandleW(battleModule);
    if (!m) return false;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)m;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)m + dos->e_lfanew);
    BYTE* lo = (BYTE*)m;
    BYTE* hi = lo + nt->OptionalHeader.SizeOfImage;
    return (BYTE*)ret >= lo && (BYTE*)ret < hi;
}

static bool GameInFront()
{
    HWND f = GetForegroundWindow();
    DWORD pid = 0;
    if (f) GetWindowThreadProcessId(f, &pid);
    return pid == GetCurrentProcessId();
}

static LONG beatSeen, beatTick;
static bool AgentAlive(LONG now)
{
    LONG b = shared->agentBeat;
    if (b != beatSeen) { beatSeen = b; beatTick = now; }
    return b != 0 && now - beatTick < 1500;   // a stopped or hung agent leaves no key held
}

// The battle pauses whenever the game window is not the active one: at the start of a fight the battle module makes
// its message handler (RVA 0x12C0) the window procedure of the game window (SetWindowLongA, RVA 0x20E0); the handler
// sets the byte [0x10106788] from WM_ACTIVATE (WA_INACTIVE -> 0) and every frame is skipped while it is 0 (RVA
// 0x2956C). With the agent playing, keep the fight going behind other windows: once that handler is in place, a
// window procedure of ours goes in front of it and hands WM_ACTIVATE on as WA_ACTIVE (the minimised bit kept -
// minimised still pauses). The battle module puts the host's procedure back at the end of the fight, which drops
// ours with it. No code or memory protection of the game is touched.
static WNDPROC volatile battleProc;    // (only while auto battle is on - autoBattle && background)
static LRESULT CALLBACK KeepActiveProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) wp = MAKEWPARAM(WA_ACTIVE, HIWORD(wp));
    return CallWindowProcA(battleProc, h, msg, wp, lp);
}

static HWND foundWnd;
static BOOL CALLBACK OnGameWindow(HWND h, LPARAM)
{
    DWORD pid = 0;
    wchar_t cls[64];
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && GetClassNameW(h, cls, 64) && wcscmp(cls, L"OCGameApplicationWindow") == 0) { foundWnd = h; return FALSE; }
    return TRUE;
}

// The battle module hides the cursor from its window procedure (RVA 0x1470 -> 0x3A40, on the window's thread) and shows
// it again when the fight ends right after putting the host's procedure back (RVA 0x216A -> 0x3A10) - from the fight's
// own thread. Once a dialog has split the two threads' input (input fix v4), that "show" counts on the wrong thread and
// the cursor stays hidden over the game and its dialogs (user 10-13: 「게임 화면 위에서 마우스가 안뜨네」「실행은되는데 그냥
// 보이지 않아」). At the end of each fight the window thread's count is
// brought back to 0 - on that thread itself: a message hook of ours there (WH_GETMESSAGE, this process) runs when a nudge (WM_NULL) reaches it. Until 10-18 the watcher joined the
// window thread's input for a moment instead (10-13); the user still lost the cursor after free fights (10-18 「자유
// 전투를 하고 메뉴로 오면 마우스 커서를 돌려받지 못하는 현상이 아직 있어」), so for 20 s after a fight a cursor hidden
// over the game in front is fixed again, and an empty cursor shape gets the arrow.
// 11-04 user 「전투가 끝난후 마우스 반환 안하는 버그 다시 생긴거 같은데」: after the story's first fight a movie began 10 s
// later; its video window (DirectShow, owned by this window) hides the cursor on purpose, the 20 s ran out while it played
// (6 fixes, all over the movie) and nothing fixed the cursor after it. Now a cursor hidden over the game window itself -
// not over a child such as the movie's video window - is fixed whenever no fight is on, at most once a second; the log
// keeps the first 10 fixes after each fight and then every 50th.
static HHOOK cursorHook;
static volatile LONG cursorFixPending;
static const char* cursorFixWhy = "";       // NULL: fix without a log line
static LRESULT CALLBACK CursorFixHook(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && InterlockedExchange(&cursorFixPending, 0))
    {
        int n = ShowCursor(TRUE), steps = 1;
        while (n < 0 && steps++ < 50) n = ShowCursor(TRUE);
        while (n > 0) n = ShowCursor(FALSE);
        bool shape = GetCursor() == NULL;
        if (shape) SetCursor(LoadCursorW(NULL, IDC_ARROW));
        if (cursorFixWhy)
            Log("%s: the cursor is shown on the window's own thread (count %d%s)", cursorFixWhy, n, shape ? ", arrow put back" : "");
    }
    return CallNextHookEx(NULL, code, wp, lp);
}
static void ShowCursorOn(HWND w, const char* why)
{
    DWORD tid = GetWindowThreadProcessId(w, NULL);
    if (!tid) return;
    if (!cursorHook) cursorHook = SetWindowsHookExW(WH_GETMESSAGE, CursorFixHook, NULL, tid);
    if (!cursorHook) { Log("%s: no message hook on the window's thread (error %lu)", why ? why : "cursor fix", GetLastError()); return; }
    cursorFixWhy = why;
    InterlockedExchange(&cursorFixPending, 1);
    PostMessageW(w, WM_NULL, 0, 0);
}
// hidden while the game is in front and the mouse is over its picture - over the game window itself, not a child
static bool CursorHiddenOver(HWND w)
{
    CURSORINFO ci = { sizeof ci };
    if (!GetCursorInfo(&ci) || GetForegroundWindow() != w) return false;
    RECT rc;
    GetClientRect(w, &rc);
    MapWindowPoints(w, NULL, (POINT*)&rc, 2);
    return PtInRect(&rc, ci.ptScreenPos) && WindowFromPoint(ci.ptScreenPos) == w
           && (!(ci.flags & CURSOR_SHOWING) || !ci.hCursor);
}

// ---------------------------------------------------------------- ESC on the title and 감상실 screens: the previous menu
// 10-18 user 「메인 화면에서 ESC를 게임 종료가 아니라 이전 메뉴로 고쳐줘. 종료는 ALT F4」. The engine binds ESC in its
// accelerator table (system.dll resource 103: VK_ESCAPE -> 0x9C5B, which 0x1003F840 turns into the menu's Close 0x9C52 =
// 「Do you want to exit the game?」). On the title and the 감상실 side - system-menu mask 0x7E2 at the window object
// (GWL_USERDATA) +0x18E2C; only dh_menu.nps sets it, the story sets #000/#002 - ESC now presses the screen's own EXIT
// button instead (692,524, 60x60 on every one of those screens: dh_extra, freebattle, dh_ex_cg, dh_ex_sound and ours),
// which takes it to the menu before. The title has nothing there, so ESC does nothing; Alt+F4 (WM_CLOSE -> the game's
// own question) quits. The story keeps ESC as it was. Fights are left alone (the battle module's window procedure is
// in), and the game's dialogs (load, settings) run their own message loops.
typedef int (WINAPI* TranslateAcceleratorA_t)(HWND, HACCEL, LPMSG);
static TranslateAcceleratorA_t realTranslateAcceleratorA;
static int escBack = 1;
static volatile LONG escBackBusy;
static DWORD lastEscMask = 1;

static bool InFight(HWND w)
{
    HMODULE m = GetModuleHandleW(L"demonbane.dll");
    WNDPROC handler = m ? (WNDPROC)((BYTE*)m + 0x12C0) : NULL;
    WNDPROC now = (WNDPROC)GetWindowLongPtrA(w, GWLP_WNDPROC);
    return handler && (now == handler || now == KeepActiveProc);
}

static bool Readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    return p && VirtualQuery(p, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
           && (const BYTE*)mi.BaseAddress + mi.RegionSize >= (const BYTE*)p + n;
}

// The application object: the main window procedure (0x10046B90) takes it from the global 0x100EE680 (the window has
// nothing in GWL_USERDATA - first try 10-18 read 0 there); its +4 is the window, +0x1A15C the accelerator table.
static DWORD MenuMask(HWND w)
{
    HMODULE sys = GetModuleHandleW(L"system.dll");
    BYTE** global = sys ? (BYTE**)((BYTE*)sys + 0xEE680) : NULL;
    BYTE* obj = Readable(global, 4) ? *global : NULL;
    if (!Readable(obj + 4, 4) || *(HWND*)(obj + 4) != w || !Readable(obj + 0x18E2C, 4)) return 0xFFFFFFFF;
    return *(DWORD*)(obj + 0x18E2C);
}

// the battle's pause menu is open: [[demonbane+0x11E3B0]+0x78]+0x144 (what the agent reads too)
static bool PauseMenuOpen()
{
    BYTE* m = (BYTE*)GetModuleHandleW(L"demonbane.dll");
    BYTE** at = m ? (BYTE**)(m + 0x11E3B0) : NULL;
    BYTE* manager = Readable(at, 4) ? *at : NULL;
    BYTE* menu = manager && Readable(manager + 0x78, 4) ? *(BYTE**)(manager + 0x78) : NULL;
    return menu && Readable(menu + 0x144, 4) && *(int*)(menu + 0x144) != 0;
}

// 11-03: the pause menu reads Enter / Space as 고르기 and ESC as 취소 from the keyboard, Z and C only through PLAYER1's key
// device (a pad built from the key settings) - in an automatic DUEL fight PLAYER1 has none, and Z did not choose (09:02 test).
// While the menu is open, Z also presses Enter and C also ESC.
static void PauseMenuKeys(BYTE* s)
{
    if (s[DIK_Z] & 0x80) s[DIK_RETURN] |= 0x80;
    if (s[DIK_C] & 0x80) s[DIK_ESCAPE] |= 0x80;
}

// the system-menu mask without the window check (fight start): dh_menu.nps #7E2, the story #000/#002, DUEL #400007E2,
// and 11-03 freebattle.nps #200007E2 (its copy in 0replay.pak) - so a fight started from there is a FREE FIGHT one
static DWORD SystemMenuMask()
{
    HMODULE sys = GetModuleHandleW(L"system.dll");
    BYTE** global = sys ? (BYTE**)((BYTE*)sys + 0xEE680) : NULL;
    BYTE* obj = Readable(global, 4) ? *global : NULL;
    return obj && Readable(obj + 0x18E2C, 4) ? *(DWORD*)(obj + 0x18E2C) : 0;
}

static DWORD WINAPI EscBackThread(LPVOID p)
{
    HWND w = (HWND)p;
    RECT rc;
    GetClientRect(w, &rc);
    int cw = rc.right, ch = rc.bottom, iw, ih;              // the 800x600 picture in the client (4K: letterboxed)
    if (cw * 3 >= ch * 4) { ih = ch; iw = ch * 4 / 3; } else { iw = cw; ih = cw * 3 / 4; }
    int x = (cw - iw) / 2 + 722 * iw / 800, y = (ch - ih) / 2 + 554 * ih / 600;
    Log("ESC on the title/감상실 side: the screen's EXIT spot is pressed (previous menu) at %d,%d of %dx%d", x, y, cw, ch);
    PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));      // over it first, as a hand would
    Sleep(150);
    PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    Sleep(80);
    PostMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    Sleep(150);
    POINT pt;                                                // the game's idea of the mouse back where the mouse is
    if (GetCursorPos(&pt) && ScreenToClient(w, &pt)) PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
    Sleep(300);
    InterlockedExchange(&escBackBusy, 0);
    return 0;
}

static int WINAPI HookTranslateAcceleratorA(HWND w, HACCEL acc, LPMSG m)
{
    // 10-19 user 「한번 선택하면 취소가 불가능하네」 (DUEL select screens): on the same screens a right click is the cancel
    // too - it presses the screen's EXIT like ESC (one step back). The click itself never reaches the game there.
    if (escBack && w && m && (m->message == WM_RBUTTONDOWN || m->message == WM_RBUTTONUP || m->message == WM_RBUTTONDBLCLK)
        && !InFight(w) && (MenuMask(w) & ~0x40000000UL) == 0x7E2)
    {
        if (m->message == WM_RBUTTONUP && !InterlockedExchange(&escBackBusy, 1))
        {
            HANDLE t = CreateThread(NULL, 0, EscBackThread, w, 0, NULL);
            if (t) CloseHandle(t);
            else InterlockedExchange(&escBackBusy, 0);
        }
        return 1;
    }
    if (escBack && w && m && m->message == WM_KEYDOWN && m->wParam == VK_ESCAPE && !(GetKeyState(VK_CONTROL) & 0x8000)
        && !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && !InFight(w))
    {
        DWORD mask = MenuMask(w) & ~0x40000000UL;
        if (mask != lastEscMask)
        {
            lastEscMask = mask;
            Log("ESC: system-menu mask %#lx -> %s", mask, mask == 0x7E2 ? "title/감상실 side, previous menu" : "the game's own ESC");
        }
        if (mask == 0x7E2)
        {
            if (!(m->lParam & (1 << 30)) && !InterlockedExchange(&escBackBusy, 1))   // not the key's auto-repeat
            {
                HANDLE t = CreateThread(NULL, 0, EscBackThread, w, 0, NULL);
                if (t) CloseHandle(t);
                else InterlockedExchange(&escBackBusy, 0);
            }
            return 1;                                          // not the game's Close question
        }
    }
    return realTranslateAcceleratorA(w, acc, m);
}

static DWORD WINAPI WatchBattleWindow(LPVOID)
{
    bool fighting = false, classLogged = false;
    DWORD lastHeal = 0;
    int heals = 0;
    for (;;)
    {
        Sleep(100);
        DrainDuelMessages();
        foundWnd = NULL;
        EnumWindows(OnGameWindow, 0);
        HWND w = foundWnd;
        UpdateDuelControls(w);
        if (!w) continue;
        HMODULE m = GetModuleHandleW(L"demonbane.dll");
        WNDPROC handler = m ? (WNDPROC)((BYTE*)m + 0x12C0) : NULL;
        WNDPROC now = (WNDPROC)GetWindowLongPtrA(w, GWLP_WNDPROC);
        bool inFight = handler && (now == handler || now == KeepActiveProc);
        DWORD t = GetTickCount();
        if (!classLogged)
        {
            classLogged = true;
            Log("game window class cursor %p (cursor fixes outside fights: over the window itself, once a second)",
                (void*)GetClassLongPtrW(w, GCLP_HCURSOR));
        }
        if (fighting && !inFight) { ShowCursorOn(w, "fight over"); heals = 0; }
        else if (!inFight && t - lastHeal > 1000 && CursorHiddenOver(w))
        {
            lastHeal = t;
            heals++;
            ShowCursorOn(w, heals <= 10 || heals % 50 == 0 ? "outside a fight the cursor was hidden over the game" : NULL);
        }
        fighting = inFight;
        if (!((autoBattle || freeAuto || duelAutoOn) && background) || !handler || now != handler) continue;
        battleProc = handler;
        WNDPROC was = (WNDPROC)SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)KeepActiveProc);
        if (was != handler)
        {
            SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)was);   // the fight ended in between: leave it as it was
            continue;
        }
        Log("battle window procedure %p: the fight keeps running while the window is not active", handler);
    }
}

// tests only (autobattle.ini, 0 = off): test_hold=<DIK code> makes the fight's keyboard reads look as if the person held
// that key, test_esc_ms=<ms> as if ESC went down that long into each fight (from its first read), test_answer=1|2 answers
// the take-over question yes|no without showing it. The log says whether the held key reached the fight.
// test_keys=<ms>:<DIK hex>[+<DIK hex>...]:<hold ms>,... (10-18, enemy robots played by hand): scripted presses counted
// from each fight's first keyboard read, put in every read of the fight whatever else happens to it (no agent needed)
struct TestKey { LONG at, hold; BYTE dik[4]; int n; };
static TestKey testKeys[256];
static int testHold, testEscMs, testAnswer, testKeyN;
// v29 (11-18, user 「Yes, fix them」 to 「게임 창이 뒤에 있을 때 들어오는 키를 기본으로 막는다」): while the game window is not in
// front, every keyboard/mouse read of the game is empty. Fights read DirectInput in background mode, so what the person typed
// in other windows reached a game left behind them (ㅑ = I paused a fight, Enter left it; 11-16 tests closed four times).
// The agent's and scripted keys are put in after this, so auto battle still plays; a pad is not touched.
// autobattle.ini isolate_input=0 turns it off. test_isolate_input=1 (the 11-16 test switch) also logs which keys were kept.
static int isolateInput, testIsolate;
static void LoadTestHooks(const wchar_t* ini)
{
    testHold = GetPrivateProfileIntW(L"autobattle", L"test_hold", 0, ini);
    testEscMs = GetPrivateProfileIntW(L"autobattle", L"test_esc_ms", 0, ini);
    testAnswer = GetPrivateProfileIntW(L"autobattle", L"test_answer", 0, ini);
    testIsolate = GetPrivateProfileIntW(L"autobattle", L"test_isolate_input", 0, ini);
    isolateInput = GetPrivateProfileIntW(L"autobattle", L"isolate_input", 1, ini) || testIsolate;
    Log("keys and mouse while the game window is behind others: %ls", isolateInput ? L"kept out (isolate_input=1)" : L"read (isolate_input=0)");
    static wchar_t keys[8192];
    GetPrivateProfileStringW(L"autobattle", L"test_keys", L"", keys, 8192, ini);
    testKeyN = 0;
    for (wchar_t* p = keys; *p && testKeyN < 256;)
    {
        TestKey k = {};
        k.at = wcstol(p, &p, 10);
        if (*p++ != L':') break;
        do { if (k.n < 4) k.dik[k.n++] = (BYTE)wcstol(p, &p, 16); } while (*p == L'+' && p++);
        if (*p++ != L':') break;
        k.hold = wcstol(p, &p, 10);
        testKeys[testKeyN++] = k;
        while (*p == L',' || *p == L' ') p++;
    }
    if (testHold || testEscMs || testAnswer || testKeyN)
        Log("TEST hooks: hold key %d, ESC %d ms into each fight, answer %d, %d scripted presses", testHold, testEscMs, testAnswer, testKeyN);
}

static BOOL CALLBACK FindGameWnd(HWND h, LPARAM out)
{
    DWORD pid = 0;
    wchar_t cls[64];
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && GetClassNameW(h, cls, 64) && wcscmp(cls, L"OCGameApplicationWindow") == 0) { *(HWND*)out = h; return FALSE; }
    return TRUE;
}

// a pad read while the agent plays: sticks centred (their range asked of the device), nothing pressed, hats released
static void NeutralPad(void* dev, BYTE* data, DWORD cb)
{
    typedef HRESULT(STDMETHODCALLTYPE* GetProperty_t)(void*, REFGUID, LPDIPROPHEADER);
    DIPROPRANGE pr = {};
    pr.diph.dwSize = sizeof pr;
    pr.diph.dwHeaderSize = sizeof pr.diph;
    pr.diph.dwObj = DIJOFS_X;
    pr.diph.dwHow = DIPH_BYOFFSET;
    LONG mid = 32767;                                                  // DirectInput's default range 0..65535
    if (SUCCEEDED(((GetProperty_t)(*(void***)dev)[5])(dev, DIPROP_RANGE, &pr.diph))) mid = pr.lMin + (pr.lMax - pr.lMin) / 2;
    DIJOYSTATE* j = (DIJOYSTATE*)data;
    j->lX = j->lY = j->lZ = j->lRx = j->lRy = j->lRz = mid;
    j->rglSlider[0] = j->rglSlider[1] = mid;
    for (int i = 0; i < 4; i++) j->rgdwPOV[i] = 0xFFFFFFFF;
    memset(j->rgbButtons, 0, sizeof j->rgbButtons);
    if (cb > sizeof(DIJOYSTATE)) memset(data + sizeof(DIJOYSTATE), 0, cb - sizeof(DIJOYSTATE));   // DIJOYSTATE2: more buttons, rates, forces
}

static LONG firstBattleRead, runStartTick, testReads;
static bool duelMenuExit;                            // DUEL: the pause menu's EXIT leaves the fight (DuelMenuExit, below)
static bool neroSideCamera;                          // DUEL: the 10-23 side view when piloting Nero (nero_player_camera.h);
                                                     // 11-02 user 「디폴트로 돌려줘」 - off unless nero_side_camera=1
static void DuelMenuExit();
// 10-19: this fight is a DUEL AI-vs-AI copy (set by DuelAutoScene before each fight). Both robots are on the game's AI, so
// the fight needs nothing of the person's - but with the window behind others (background=1 keeps the fight going) an
// ESC and an Enter typed into another window still reached it, paused it and left through the pause menu's EXIT (twice in
// the 10-19 tests). While the game window is not in front its keyboard and pad reads are blanked (scripted test keys
// stay); in front nothing changes, so ESC and the pause menu work as before.
static volatile LONG duelAutoFight;
static LONG duelAutoBlankLogged;

static void ApplyTestKeys(BYTE* s, LONG t)
{
    for (int i = 0; i < testKeyN; i++)
        if (t >= testKeys[i].at && t < testKeys[i].at + testKeys[i].hold)
            for (int k = 0; k < testKeys[i].n; k++) s[testKeys[i].dik[k]] |= 0x80;
}
static void KcTick();                    // kokuin_camera.h: SetCamera calls ours only while a robot plays DUEL_666
static HRESULT STDMETHODCALLTYPE HookGetDeviceState(void* self, DWORD cb, LPVOID data)
{
    void* ret = _ReturnAddress();
    GetDeviceState_t orig = (GetDeviceState_t)FindOrig(gsPatched, gsN, *(void***)self);
    if (!orig) return DIERR_GENERIC;
    HRESULT hr = orig(self, cb, data);
    if (isolateInput && data && (cb == 256 || cb == 16 || cb == 20) && !GameInFront())   // v29: see isolateInput
    {
        if (testIsolate && SUCCEEDED(hr) && cb == 256)   // tests only: which keys of the person's would have reached it (first 60)
        {
            static LONG isoKeys;
            static BYTE wasDown[256];
            BYTE* k = (BYTE*)data;
            for (int i = 1; i < 256; i++)
            {
                bool down = (k[i] & 0x80) != 0;
                if (down && !wasDown[i] && InterlockedIncrement(&isoKeys) <= 60)
                    Log("TEST isolate input: the person's key DIK %02X kept out (%ls read)", i, FromBattle(ret) ? L"fight" : L"story");
                wasDown[i] = down;
            }
        }
        memset(data, 0, cb);
        hr = DI_OK;
        static LONG isoLogged;
        if (!InterlockedExchange(&isoLogged, 1)) Log("the game window is behind others: its keyboard/mouse reads are empty until it is in front");
    }
    if (cb == 256 && FromBattle(ret)) KcTick();     // 11-05 18:4x: khdcam needs the call at 0x13e77 untouched while a fight loads
    if (!data || !shared) return hr;
    bool pad = cb == sizeof(DIJOYSTATE) || cb == sizeof(DIJOYSTATE2);
    if (cb != 256 && cb != 16 && cb != 20 && !pad) return hr;
    LONG now = (LONG)GetTickCount();
    bool fromBattle = FromBattle(ret);
    bool battle = cb == 256 && fromBattle;
    if (battle)
    {
        LONG prev = shared->lastPoll;
        if (prev == 0 || now - prev >= 2000) { InterlockedIncrement(&shared->runs); runStartTick = now; }
        shared->lastPoll = now ? now : 1;
        InterlockedIncrement(&shared->polls);
        if (!InterlockedExchange(&firstBattleRead, 1)) Log("first keyboard read from %ls (result %08lX)", battleModule, (unsigned long)hr);
    }
    if (battle && testKeyN) ApplyTestKeys((BYTE*)data, now - runStartTick);
    if (battle && duelMenuExit) DuelMenuExit();
    // 10-19 낮: 키보드만 비운다(패드는 손대지 않음 — 패드 범위를 묻는 GetProperty 를 매 프레임 부르지 않게). 사용자가 게임을 켜 둔 채
    // 채팅창에 한글을 치면 ㅑ(I = 일시정지)와 Enter(EXIT)가 자동 전투를 끝냈다(놀이 사본 12:34·12:36·12:40·12:42)
    if (fightKind == 2 && (hoMode >= 0 ? hoMode == 1 : duelAutoFight != 0) && battle && !GameInFront())   // DUEL AI vs AI behind other windows (above)
    {
        memset(data, 0, cb);
        if (testKeyN) ApplyTestKeys((BYTE*)data, now - runStartTick);
        if (PauseMenuOpen()) PauseMenuKeys((BYTE*)data);
        if (!InterlockedExchange(&duelAutoBlankLogged, 1))
            Log("DUEL auto battle: the game window is not in front - keys typed elsewhere stay out of this fight");
        return DI_OK;
    }
    if (battle && PauseMenuOpen()) PauseMenuKeys((BYTE*)data);
    if (!AgentAlive(now)) return hr;                 // no agent (off, missing or stopped beating): the reads stay as they were
    // the game may run behind other windows: what the person types or moves there stays out of it
    bool front = GameInFront();
    if (battle) shared->foreground = front ? 1 : 0;
    if (!pad && (FAILED(hr) || !front)) { memset(data, 0, cb); hr = DI_OK; }
    if (!fromBattle) return hr;
    BYTE* s = (BYTE*)data;
    bool esc = false;
    if (battle)
    {
        if (testHold > 0 && testHold < 256) s[testHold] |= 0x80;
        bool testEsc = testEscMs > 0 && now - runStartTick >= testEscMs && now - runStartTick < testEscMs + 400;
        esc = (front && (s[DIK_ESCAPE] & 0x80)) || testEsc;
    }
    // 11-03: ESC no longer asks (MessageBox) - it reaches the fight and opens the pause menu, whose 자동 전투 row switches
    // this fight's mode off or on; while that menu is open the person's keys drive it (the agent lets go of a paused fight)
    bool playing = AgentMode() && stoppedRun != shared->runs;    // the agent plays this fight
    bool menu = battle && PauseMenuOpen();
    if (playing && !menu)
    {
        if (battle) { memcpy(s, shared->keys, 256); if (esc) s[DIK_ESCAPE] |= 0x80; }   // the agent's keys, and the person's ESC
        else if (pad) NeutralPad(self, s, cb);
        else memset(s, 0, cb);                                   // mouse
    }
    if (battle && testHold > 0 && testHold < 256 && (++testReads % 300) == 1)
        Log("TEST: held key %d %s the fight (auto battle %s)", testHold, (s[testHold] & 0x80) ? "reaches" : "is kept out of",
            playing ? "playing" : "not playing");
    if (battle && testKeyN && (!playing || menu)) ApplyTestKeys(s, now - runStartTick);   // after the background blanking above
    if (menu) PauseMenuKeys(s);
    else if (battle && testKeyN)                                 // the agent plays: a scripted ESC is the person's (pause menu)
    {
        BYTE t[256] = {};
        ApplyTestKeys(t, now - runStartTick);
        if (t[DIK_ESCAPE] & 0x80) s[DIK_ESCAPE] |= 0x80;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreateDevice(void* self, REFGUID guid, void** dev, LPUNKNOWN outer)
{
    CreateDevice_t orig = (CreateDevice_t)FindOrig(cdPatched, cdN, *(void***)self);
    if (!orig) return DIERR_GENERIC;
    HRESULT hr = orig(self, guid, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev)
        PatchSlot(*(void***)(*dev), 9, (void*)HookGetDeviceState, gsPatched, &gsN, 16);   // IDirectInputDevice8::GetDeviceState
    return hr;
}

// ---------------------------------------------------------------- sound
static void SetMuteAll(BOOL mute)
{
    IMMDeviceEnumerator* en = NULL;
    IMMDeviceCollection* list = NULL;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) return;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &list)))
    {
        UINT count = 0;
        list->GetCount(&count);
        for (UINT i = 0; i < count; i++)
        {
            IMMDevice* dev = NULL;
            IAudioSessionManager* mgr = NULL;
            ISimpleAudioVolume* vol = NULL;
            if (SUCCEEDED(list->Item(i, &dev)) && SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, NULL, (void**)&mgr))
                && SUCCEEDED(mgr->GetSimpleAudioVolume(NULL, FALSE, &vol)))   // this process's default session on that device
            {
                BOOL cur = !mute;
                if (SUCCEEDED(vol->GetMute(&cur)) && cur != mute) vol->SetMute(mute, NULL);
            }
            if (vol) vol->Release();
            if (mgr) mgr->Release();
            if (dev) dev->Release();
        }
        list->Release();
    }
    en->Release();
}

static DWORD WINAPI SoundThread(LPVOID p)
{
    BOOL mute = (BOOL)(INT_PTR)p;
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 0;
    if (!mute)
    {
        SetMuteAll(FALSE);
        CoUninitialize();
        return 0;
    }
    for (int i = 0;; i++)   // the battle module makes its own sound device: keep every session of ours muted
    {
        SetMuteAll(TRUE);
        Sleep(i < 20 ? 500 : 2000);
    }
}

// ---------------------------------------------------------------- 3D diagnosis (10-13, autobattle.ini d3dprobe=1)
// The first fight crashed at demonbane.dll RVA 0x39992 since 03:39: inside the game, CheckDeviceType(HAL) answered
// D3DERR_NOTAVAILABLE, the battle module fell back to the reference rasteriser, CreateDevice failed and the device
// stayed NULL - while the same call from a test program works. Ask the same question from inside the game: at the
// title, and the moment the battle module is loaded. Read only - a Direct3D 8 object made and released again.
typedef void* (WINAPI* Create8_t)(UINT);
struct DisplayMode8 { UINT Width, Height, RefreshRate; DWORD Format; };
static void ProbeD3D(const char* when)
{
    HMODULE d = LoadLibraryW(L"d3d8.dll");
    Create8_t create = d ? (Create8_t)GetProcAddress(d, "Direct3DCreate8") : NULL;
    void* d3d = create ? create(220) : NULL;
    if (!d3d) { Log("3D probe (%s): no Direct3D 8 object", when); return; }
    void** vt = *(void***)d3d;
    UINT n = ((UINT(WINAPI*)(void*))vt[4])(d3d);
    for (UINT a = 0; a < n && a < 4; a++)
    {
        DisplayMode8 m = {};
        HRESULT hm = ((HRESULT(WINAPI*)(void*, UINT, DisplayMode8*))vt[8])(d3d, a, &m);
        HRESULT hw = ((HRESULT(WINAPI*)(void*, UINT, DWORD, DWORD, DWORD, BOOL))vt[9])(d3d, a, 1, m.Format, m.Format, TRUE);
        HRESULT hf = ((HRESULT(WINAPI*)(void*, UINT, DWORD, DWORD, DWORD, BOOL))vt[9])(d3d, a, 1, m.Format, m.Format, FALSE);
        Log("3D probe (%s): adapter %u mode %08lX %ux%u fmt %lu - HAL windowed %08lX, full screen %08lX", when, a, (unsigned long)hm,
            m.Width, m.Height, m.Format, (unsigned long)hw, (unsigned long)hf);
    }
    ((ULONG(WINAPI*)(void*))vt[2])(d3d);
}

static DWORD WINAPI ProbeThread(LPVOID)
{
    Sleep(5000);
    ProbeD3D("title");
    HMODULE seen = NULL;
    for (;;)
    {
        HMODULE m = GetModuleHandleW(L"demonbane.dll");
        if (m && m != seen) { seen = m; ProbeD3D("battle module just loaded"); }
        if (!m) seen = NULL;
        Sleep(20);
    }
}

#include "clean_helpers.h"

// ---------------------------------------------------------------- agent and subtitle window
// one helper process from autobattle\ in the job that ends it with the game (the job handle closes with us)
static void StartChild(const wchar_t* exeName, const wchar_t* args, const char* what)
{
    wchar_t exe[MAX_PATH], cmd[MAX_PATH * 2];
    swprintf_s(exe, L"%s\\%s", abDir, exeName);
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) { Log("no %s at %ls", what, exe); return; }
    swprintf_s(cmd, L"\"%s\" %s", exe, args);
    if (!agentJob)
    {
        agentJob = CreateJobObjectW(NULL, NULL);
        if (agentJob)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim = {};
            lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(agentJob, JobObjectExtendedLimitInformation, &lim, sizeof lim);
        }
    }
    // Locale Emulator cannot initialize these CLR helpers after mscoree loads kernel32.
    // Ask the local process provider to start only these two helpers outside the game's LE inheritance.
    if (_wcsicmp(exeName, L"KhdSubtitles.exe") == 0 || _wcsicmp(exeName, L"KhdAutoBattle.exe") == 0)
    {
        DWORD childPid = 0, providerStatus = 0;
        const HRESULT result = CleanStartManagedHelper(cmd, abDir, &childPid, &providerStatus);
        if (FAILED(result))
        {
            Log("clean %s start failed: HRESULT %08lX provider %lu", what, (unsigned long)result, providerStatus);
            return;
        }
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, childPid);
        const bool inJob = child && agentJob && AssignProcessToJobObject(agentJob, child);
        if (child) CloseHandle(child);
        Log("%s started clean: pid %lu%s", what, childPid, inJob ? " (ends with the game)" : " (not in a job; it watches the game itself)");
        return;
    }
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, abDir, &si, &pi))
    {
        Log("could not start the %s (error %lu)", what, GetLastError());
        return;
    }
    bool inJob = agentJob && AssignProcessToJobObject(agentJob, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log("%s started: pid %lu%s", what, pi.dwProcessId, inJob ? " (ends with the game)" : " (not in a job; it watches the game itself)");
}

// the shared memory, once per game and from the start (11-03: also with every 자동 전투 off - the fight's reads, the test keys
// and a later 켜기 in the pause menu all need it; with no agent beating it changes nothing)
static wchar_t sharedName[64];
static void EnsureShared()
{
    static volatile LONG made = 0;
    if (InterlockedExchange(&made, 1)) return;
    swprintf_s(sharedName, L"Local\\KhdAutoBattle_%lu", GetCurrentProcessId());
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, sharedName);
    if (!map) { Log("CreateFileMapping failed (error %lu)", GetLastError()); return; }
    Shared* s = (Shared*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
    if (!s) { Log("MapViewOfFile failed (error %lu)", GetLastError()); return; }
    memset(s, 0, sizeof(Shared));
    s->version = 1;
    MemoryBarrier();
    shared = s;   // map and view stay open for the life of the game
}

// the agent, once per game: at start with the story or FREE FIGHT switch on, or when the person turns one on later
static void EnsureAgent()
{
    static volatile LONG started = 0;
    EnsureShared();
    if (!shared || InterlockedExchange(&started, 1)) return;
    wchar_t args[96];
    swprintf_s(args, L"%lu %s", GetCurrentProcessId(), sharedName);
    StartChild(L"KhdAutoBattle.exe", args, "agent");
}

// the 자동 전투 check box: kept in autobattle.ini (agent=) and in force at once
static void SetAutoBattle(bool on)
{
    autoBattle = on ? 1 : 0;
    stoppedRun = 0;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    BOOL saved = WritePrivateProfileStringW(L"autobattle", L"agent", on ? L"1" : L"0", ini);
    if (on) EnsureAgent();
    Log("auto battle turned %s in the settings (autobattle.ini agent=%d%s)", on ? "on" : "off", on ? 1 : 0, saved ? "" : " - could not save");
}

// 11-03 the FREE FIGHT and DUEL switches (the story one is SetAutoBattle): kept in autobattle.ini and in force at once
static void SetFreeAuto(bool on)
{
    freeAuto = on ? 1 : 0;
    stoppedRun = 0;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    BOOL saved = WritePrivateProfileStringW(L"autobattle", L"free_auto", on ? L"1" : L"0", ini);
    if (on) EnsureAgent();
    Log("FREE FIGHT auto battle turned %s (autobattle.ini free_auto=%d%s)", on ? "on" : "off", on ? 1 : 0, saved ? "" : " - could not save");
}

static void SetDuelAuto(bool on)
{
    duelAutoOn = on ? 1 : 0;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    BOOL saved = WritePrivateProfileStringW(L"autobattle", L"duel_auto", on ? L"1" : L"0", ini);
    Log("DUEL auto battle turned %s (autobattle.ini duel_auto=%d%s)", on ? "on" : "off", on ? 1 : 0, saved ? "" : " - could not save");
}

// DUEL-only settings: independent of the story settings dialog.
static void SetDuelBalance(bool on)
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    BOOL saved = WritePrivateProfileStringW(L"duel", L"balance", on ? L"1" : L"0", ini);
    if (saved) InterlockedExchange(&duelBalance, on ? 1 : 0);
    Log("DUEL selection: balance=%ld saved=%d (story settings untouched)", duelBalance, saved);
}
#include "duel_settings_ui.h"

static void StartOnce()
{
    static volatile LONG started = 0;
    if (InterlockedExchange(&started, 1)) return;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    int agent = GetPrivateProfileIntW(L"autobattle", L"agent", 1, ini);
    int mute = GetPrivateProfileIntW(L"autobattle", L"mute", 1, ini);
    int subtitles = GetPrivateProfileIntW(L"autobattle", L"subtitles", 1, ini);
    int bg = GetPrivateProfileIntW(L"autobattle", L"background", 1, ini);
    wchar_t env[64];
    if (GetEnvironmentVariableW(L"KHD_AUTOBATTLE_MODULE", env, 64) && env[0]) wcscpy_s(battleModule, env);   // tests only
    Log("proxy loaded in %ls; agent=%d mute=%d subtitles=%d background=%d battle module %ls", gameDir, agent, mute, subtitles, bg,
        battleModule);
    LoadTestHooks(ini);
    if (GetPrivateProfileIntW(L"autobattle", L"d3dprobe", 0, ini))
    {
        HANDLE pt = CreateThread(NULL, 0, ProbeThread, NULL, 0, NULL);
        if (pt) CloseHandle(pt);
    }
    background = bg;
    autoBattle = agent ? 1 : 0;
    freeAuto = GetPrivateProfileIntW(L"autobattle", L"free_auto", agent, ini) ? 1 : 0;   // without the line: as the story
    if (_wcsicmp(battleModule, L"demonbane.dll") == 0)   // the cursor after each fight; the subclass only with auto battle
    {
        HANDLE wt = CreateThread(NULL, 0, WatchBattleWindow, NULL, 0, NULL);
        if (wt) CloseHandle(wt);
    }
    // The screen saver (here Mystify after 180 s without input) taking the screen mid-fight lost the battle's Direct3D
    // device and the game saved and quit (10-13 06:23 and 06:39; story scenes, DirectDraw, lived through it; user
    // 「스크린 세이버 뜬거 아니야?」). Keep the display on while the game runs, as a video player does: a power request
    // that ends with this process - no setting is changed.
    REASON_CONTEXT why = {};
    why.Version = POWER_REQUEST_CONTEXT_VERSION; why.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    why.Reason.SimpleReasonString = (LPWSTR)L"Kishin Hishou copy: a fight quits when the screen saver starts";
    HANDLE awake = PowerCreateRequest(&why);
    if (awake != INVALID_HANDLE_VALUE && awake != NULL && PowerSetRequest(awake, PowerRequestDisplayRequired) && PowerSetRequest(awake, PowerRequestSystemRequired))
        Log("display kept on while the game runs (no screen saver mid-fight)");
    else Log("could not keep the display on (error %lu)", GetLastError());
    HANDLE t = CreateThread(NULL, 0, SoundThread, (LPVOID)(INT_PTR)(mute ? 1 : 0), 0, NULL);
    if (t) CloseHandle(t);

    wchar_t args[96];
    if (subtitles)
    {
        swprintf_s(args, L"%lu", GetCurrentProcessId());
        StartChild(L"KhdSubtitles.exe", args, "subtitle window");
    }
    EnsureShared();
    if (agent || freeAuto) EnsureAgent();
}

// ---------------------------------------------------------------- own settings key and the 어려움 difficulty (10-13)
// system.dll keeps its settings in HKCU\Software\nitroplus\KishinHisho ("%s%s" of "Software\nitroplus\" and its name),
// the same key the play install uses - picking a difficulty here changed it there too. own_settings=1: system.dll's
// registry calls for that key go to ...\KishinHisho_Win11Copy, filled once from the shared key (user 「설정 분리해」).
// hard=1: the settings dialog has a third difficulty button 어려움 (id 0x47C, value 2) that the game never shows - show
// it; and before the battle module loads, put autobattle\hard\dmpack900_hard.dat into the game folder when the
// difficulty is 2 (Normal's numbers, the special-heavy Tiberius) and take it out otherwise, so Easy and Normal stay the
// original game (user 「하드모드에서만」). Only system.dll's import slots are changed; no code bytes.
static const char kSharedKey[] = "Software\\nitroplus\\KishinHisho";
static const char kOwnKey[] = "Software\\nitroplus\\KishinHisho_Win11Copy";
static int ownSettings, hardMode;
typedef LSTATUS(WINAPI* RegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LSTATUS(WINAPI* RegCreateKeyExA_t)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
typedef HWND(WINAPI* CreateDialogParamA_t)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
typedef HMODULE(WINAPI* LoadLibraryA_t)(LPCSTR);
static RegOpenKeyExA_t realRegOpenKeyExA;
static RegCreateKeyExA_t realRegCreateKeyExA;
static CreateDialogParamA_t realCreateDialogParamA;
static LoadLibraryA_t realLoadLibraryA;

// the import slot of dll!func in mod (by name, through the import name table)
static void** ImportSlot(HMODULE mod, const char* dll, const char* func)
{
    BYTE* b = (BYTE*)mod;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!d.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(b + d.VirtualAddress); imp->Name; imp++)
    {
        if (_stricmp((char*)(b + imp->Name), dll) != 0 || !imp->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA* names = (IMAGE_THUNK_DATA*)(b + imp->OriginalFirstThunk);
        void** slots = (void**)(b + imp->FirstThunk);
        for (int i = 0; names[i].u1.AddressOfData; i++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))   // func may be an ordinal (MAKEINTRESOURCE-like)
            {
                if (IS_INTRESOURCE(func) && IMAGE_ORDINAL(names[i].u1.Ordinal) == LOWORD((UINT_PTR)func)) return &slots[i];
                continue;
            }
            if (IS_INTRESOURCE(func)) continue;
            IMAGE_IMPORT_BY_NAME* n = (IMAGE_IMPORT_BY_NAME*)(b + names[i].u1.AddressOfData);
            if (strcmp((char*)n->Name, func) == 0) return &slots[i];
        }
    }
    return NULL;
}

static bool HookImport(HMODULE mod, const char* dll, const char* func, void* hook, void** orig)
{
    void** slot = ImportSlot(mod, dll, func);
    if (!slot)
    {
        if (IS_INTRESOURCE(func)) Log("no import %s!#%u to hook", dll, (unsigned)LOWORD((UINT_PTR)func));
        else Log("no import %s!%s to hook", dll, func);
        return false;
    }
    if (*slot == hook) return true;
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { Log("VirtualProtect failed on an import slot (error %lu)", GetLastError()); return false; }
    *orig = *slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// "Software\nitroplus\KishinHisho[\...]" under HKCU -> the copy's own key; NULL for every other path
static const char* OwnPath(HKEY root, LPCSTR sub, char* buf, size_t n)
{
    if (!ownSettings || root != HKEY_CURRENT_USER || !sub) return NULL;
    size_t k = strlen(kSharedKey);
    if (_strnicmp(sub, kSharedKey, k) != 0 || (sub[k] != 0 && sub[k] != '\\')) return NULL;
    _snprintf_s(buf, n, _TRUNCATE, "%s%s", kOwnKey, sub + k);
    return buf;
}

static void SeedOwnKey()
{
    static volatile LONG done = 0;
    if (InterlockedExchange(&done, 1)) return;
    HKEY own; DWORD disp = 0;
    LSTATUS r = RegCreateKeyExA(HKEY_CURRENT_USER, kOwnKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &own, &disp);
    if (r != ERROR_SUCCESS) { Log("could not open the copy's own settings key (error %ld)", r); return; }
    if (disp == REG_CREATED_NEW_KEY)
    {
        HKEY from;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, kSharedKey, 0, KEY_READ, &from) == ERROR_SUCCESS)
        {
            r = RegCopyTreeA(from, NULL, own);
            RegCloseKey(from);
            Log("own settings key HKCU\\%s made from the shared one (copy %ld)", kOwnKey, r);
        }
        else Log("own settings key HKCU\\%s made (no shared key to copy)", kOwnKey);
    }
    else Log("settings go to the copy's own key HKCU\\%s", kOwnKey);
    RegCloseKey(own);
}

static LSTATUS WINAPI HookRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD opt, REGSAM sam, PHKEY out)
{
    char buf[512];
    const char* own = OwnPath(root, sub, buf, sizeof buf);
    if (own) { SeedOwnKey(); sub = own; }
    return realRegOpenKeyExA(root, sub, opt, sam, out);
}

static LSTATUS WINAPI HookRegCreateKeyExA(HKEY root, LPCSTR sub, DWORD res, LPSTR cls, DWORD opt, REGSAM sam, const LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disp)
{
    char buf[512];
    const char* own = OwnPath(root, sub, buf, sizeof buf);
    if (own) { SeedOwnKey(); sub = own; }
    return realRegCreateKeyExA(root, sub, res, cls, opt, sam, sa, out, disp);
}

// the battle settings page's own window procedure, with the 자동 전투 check boxes on it (11-03: one per mode)
static WNDPROC battlePageProc;
static LRESULT CALLBACK BattlePageProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED &&
        (LOWORD(wp) == kAutoCheckId || LOWORD(wp) == kFreeAutoCheckId || LOWORD(wp) == kDuelAutoCheckId))
    {
        bool on = SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (LOWORD(wp) == kAutoCheckId) SetAutoBattle(on);
        else if (LOWORD(wp) == kFreeAutoCheckId) SetFreeAuto(on);
        else SetDuelAuto(on);
        return 0;
    }
    return CallWindowProcA(battlePageProc, h, msg, wp, lp);
}

// 자동 전투 under the page's BGM / SE row, inside its frame ((3,3)-(250,169) in dialog units; that row ends at 148), in
// the page's own font: a label and one box per mode (11-03 user 「설정 옵션에서 모드별 컨트롤 가능하게 또 분리」)
static void AddAutoBattleBox(HWND page)
{
    if (GetDlgItem(page, kAutoCheckId)) return;
    HFONT font = (HFONT)SendMessageW(page, WM_GETFONT, 0, 0);
    struct { int id; const wchar_t* text; RECT r; LONG on; } boxes[] = {
        {0, L"자동 전투:", {30, 153, 76, 163}, 0},
        {kAutoCheckId, L"이야기", {78, 152, 122, 164}, autoBattle},
        {kFreeAutoCheckId, L"프리 파이트", {124, 152, 186, 164}, freeAuto},
        {kDuelAutoCheckId, L"DUEL", {188, 152, 240, 164}, duelAutoOn}};
    for (auto& b : boxes)
    {
        RECT r = b.r;
        MapDialogRect(page, &r);
        HWND box = CreateWindowExW(0, b.id ? L"BUTTON" : L"STATIC", b.text,
                                   b.id ? WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX : WS_CHILD | WS_VISIBLE,
                                   r.left, r.top, r.right - r.left, r.bottom - r.top, page, (HMENU)(INT_PTR)b.id, selfInst, NULL);
        if (!box) { Log("settings dialog: could not add %ls (error %lu)", b.text, GetLastError()); continue; }
        SendMessageW(box, WM_SETFONT, (WPARAM)font, FALSE);
        if (b.id) SendMessageW(box, BM_SETCHECK, b.on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    // The options difficulty belongs to story mode only. DUEL controls are on the DUEL screen.
    RECT rb = { 30, 124, 240, 134 };
    MapDialogRect(page, &rb);
    HWND note = CreateWindowExW(0, L"STATIC", L"난이도는 스토리 전투에만 적용됩니다.",
                               WS_CHILD | WS_VISIBLE, rb.left, rb.top, rb.right-rb.left, rb.bottom-rb.top,
                               page, NULL, selfInst, NULL);
    if (note) SendMessageW(note, WM_SETFONT, (WPARAM)SendMessageW(page, WM_GETFONT, 0, 0), FALSE);
    WNDPROC was = (WNDPROC)SetWindowLongPtrA(page, GWLP_WNDPROC, (LONG_PTR)BattlePageProc);
    if (was != BattlePageProc) battlePageProc = was;
    Log("settings dialog: 자동 전투 boxes added (이야기 %ld, 프리 파이트 %ld, DUEL %ld)", autoBattle, freeAuto, duelAutoOn);
}

// 11-09 user 「이지 노말 하드가 정확히 어떻게 바뀌는지 설명을 써주자. 하드는 적 에너지 재생량 상승, 적 필살기 강화 써주고」:
// what each difficulty changes, one line each under the difficulty buttons, inside their frame. The frame grows down to
// hold them; the 그림자 / 반사광 효과 row under it moves down only as far as it must (it had 15 dialog units of air).
// The numbers: every battle scene has game_rank_attack / game_rank_defence (damage we deal / take, columns easy, normal,
// hard); in the 32 story scenes Easy deals 2-2.5x and takes 0.6-0.75x of Normal (Y:\AI\claude-work\khd-session-57a04242\
// difficulty_ranks.py). The 어려움 pack (autobattle\hard\dmpack900_hard.dat, khd-runtime-fix\special_mode\build_hard_pack.py)
// sets Hard's column to Normal's in all 32 scenes, gives the enemies energy (recovery x3, full at each HP-phase cut-in)
// and their top skills (on the cut-ins, after a stagger hit and on a far player in the final phase - hard_ultimates.py).
static void AddDifficultyNotes(HWND page)
{
    if (GetDlgItem(page, kDifficultyNoteId)) return;
    HWND easy = GetDlgItem(page, 0x47B), normal = GetDlgItem(page, 0x478), hard = GetDlgItem(page, 0x47C);
    if (!easy || !normal) return;
    auto rectIn = [page](HWND c) { RECT r = {}; GetWindowRect(c, &r); MapWindowPoints(NULL, page, (POINT*)&r, 2); return r; };
    RECT er = rectIn(easy);
    // the difficulty frame: the smallest group box around the 쉬움 button (the page's own frame is around it too)
    HWND frame = NULL;
    RECT fr = {};
    for (HWND c = GetWindow(page, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    {
        wchar_t cls[16] = L"";
        GetClassNameW(c, cls, 16);
        if (_wcsicmp(cls, L"Button") != 0 || (GetWindowLongW(c, GWL_STYLE) & BS_TYPEMASK) != BS_GROUPBOX) continue;
        RECT r = rectIn(c);
        if (r.left > er.left || r.top > er.top || r.right < er.right || r.bottom < er.bottom) continue;
        if (!frame || (r.right - r.left) * (r.bottom - r.top) < (fr.right - fr.left) * (fr.bottom - fr.top)) { frame = c; fr = r; }
    }
    if (!frame) { Log("settings dialog: no difficulty frame found - no difficulty notes"); return; }
    bool hardShown = hard && (GetWindowLongW(hard, GWL_STYLE) & WS_VISIBLE);
    // v28 (user 11-09 「원작의 보통 난이도라고 하지말고 주는 피해 1배, 받는 피해 1배라고 제대로 써줄래?」): 보통 is the base the
    // other two lines are measured against
    const wchar_t* text = hardShown
        ? L"쉬움: 보통보다 주는 피해 2~2.5배, 받는 피해 0.6~0.75배\r\n"
          L"보통: 주는 피해 1배, 받는 피해 1배\r\n"
          L"어려움: 보통 수치 + 적 에너지 재생량 상승, 적 필살기 강화"
        : L"쉬움: 보통보다 주는 피해 2~2.5배, 받는 피해 0.6~0.75배\r\n"
          L"보통: 주는 피해 1배, 받는 피해 1배";
    // under the lowest of the buttons, 8 units in from the frame's sides
    LONG below = er.bottom;
    const HWND others[] = { normal, hard };
    for (HWND b : others)
        if (b) below = max(below, rectIn(b).bottom);
    RECT du = { 8, 1, 5, 5 };                                   // left inset, gap under the buttons, right inset, bottom air
    MapDialogRect(page, &du);
    RECT nr = { fr.left + du.left, below + du.top, fr.right - du.right, below + du.top };
    HFONT font = (HFONT)SendMessageW(page, WM_GETFONT, 0, 0);
    HDC dc = GetDC(page);
    HGDIOBJ old = SelectObject(dc, font ? (HGDIOBJ)font : GetStockObject(DEFAULT_GUI_FONT));
    RECT calc = { 0, 0, nr.right - nr.left, 0 };
    DrawTextW(dc, text, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(page, dc);
    nr.bottom = nr.top + calc.bottom;
    HWND notes = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, nr.left, nr.top,
                                 nr.right - nr.left, nr.bottom - nr.top, page, (HMENU)(INT_PTR)kDifficultyNoteId, selfInst, NULL);
    if (!notes) { Log("settings dialog: could not add the difficulty notes (error %lu)", GetLastError()); return; }
    SendMessageW(notes, WM_SETFONT, (WPARAM)font, FALSE);
    LONG frameBottom = max(fr.bottom, nr.bottom + du.bottom);
    SetWindowPos(frame, NULL, 0, 0, fr.right - fr.left, frameBottom - fr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    // the row under the frame: 그림자 0x474, 반사광 효과 0x47D - down to 3 units under the grown frame, if it was higher
    RECT gap = { 0, 3, 0, 0 };
    MapDialogRect(page, &gap);
    LONG shift = 0;
    const int rowIds[] = { 0x474, 0x47D };
    for (int id : rowIds)
        if (HWND c = GetDlgItem(page, id)) shift = max(shift, frameBottom + gap.top - rectIn(c).top);
    if (shift > 0)
        for (int id : rowIds)
            if (HWND c = GetDlgItem(page, id))
            {
                RECT r = rectIn(c);
                SetWindowPos(c, NULL, r.left, r.top + shift, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
    LONG nextRow = 0;                                           // 환경 반사 0x479: the row the moved one must stay above
    if (HWND c = GetDlgItem(page, 0x479)) nextRow = rectIn(c).top;
    HWND shadow = GetDlgItem(page, 0x474);
    LONG shadowBottom = shadow ? rectIn(shadow).bottom : 0;
    Log("settings dialog: difficulty notes (%s) at y %ld-%ld px; frame bottom %ld -> %ld; 그림자 row moved %ld px (its bottom %ld, 환경 반사 row top %ld%s)",
        hardShown ? "3 lines" : "2 lines", nr.top, nr.bottom, fr.bottom, frameBottom, shift, shadowBottom, nextRow,
        shadowBottom > nextRow && nextRow ? " - OVERLAP" : "");
}

// per-character voice levels (their block is further down, after the voice packs' handles)
static void AddVoiceLevels(HWND page);
static void VoiceLevelMoved(HWND page, int g);
static void VoiceGroupRead(int pak, DWORD at);
static const int kVoiceCtlBase = 0x5B00;         // + group * 4: 0 face, 1 name, 2 value, 3 slider
static int voiceBox[12];                         // the game's check box of each of its twelve groups (by its caption)

// ---------------------------------------------------------------- voice names on the sound settings page (10-18)
// user 10-18 「인물들 음성 대사 일본어로 된거 한국어로 고쳐도 괜찮나? 미국인들은 그거 고치면 함수가 일본어 음성 이름을
// 참조하는데 작동 안한다고 안 고쳤어」: the check boxes 0x41B.. (dialog 110, group 0x41A) take their names from game.ini
// [音声] (九郎=1, アル=1 ...) - the keys the engine matches the scripts' voices against, so that list stays Japanese. The
// page only shows each name (SetDlgItemTextA, system.dll RVA 0x71E33) and reads back the check state alone (BM_GETCHECK,
// 0x72B84), so the name shown can change and nothing else does. Names as the Korean text has them (speakers_ko.tsv).
static const struct { const wchar_t* jp; const wchar_t* ko; } kVoiceNames[] = {
    { L"九郎", L"쿠로" }, { L"アル", L"알" }, { L"九朔", L"쿠자쿠" }, { L"アナザーブラッド", L"어나더 블러드" },
    { L"アズラッド", L"아즈라드" }, { L"シュリュズベリイ", L"슈루즈베리" }, { L"ハヅキ", L"하즈키" }, { L"瑠璃", L"루리" },
    { L"ライカ", L"라이카" }, { L"ウェスト", L"닥터 웨스트" }, { L"エルザ", L"엘자" }, { L"その他", L"그 밖의 인물" },
};
// The game runs with the Japanese code page (932): its dialogs and their controls are ANSI windows, and Korean sent to them
// goes through their ANSI procedure, which makes ? of every Hangul (10-18 probe: the voice names, the window's title
// 「?? ??」; a Unicode procedure put in front changed nothing - the old one still got it converted). The window's text is
// kept as Unicode underneath: DefWindowProcW stores it so, past the control's procedure; then it is drawn again. The
// game's own text in the patch font (CP932 codes drawn as Hangul) is left alone.
// With the settings window on Common-Controls 6 (10-18) a control keeps its own copy of the text, so a control (not a
// dialog: the game's dialog procedures take ANSI) is made a Unicode window - its own procedure set again through the
// W call - and takes the text through its procedure (the shortcut row's static showed 「?? ? 「?? ??」 ??」 before).
static void SetKoreanText(HWND w, const wchar_t* text)
{
    if (!w) return;
    wchar_t cls[16] = L"";
    GetClassNameW(w, cls, 16);
    bool dialog = wcscmp(cls, L"#32770") == 0;
    if (!dialog && !IsWindowUnicode(w)) SetWindowLongPtrW(w, GWLP_WNDPROC, GetWindowLongPtrW(w, GWLP_WNDPROC));
    if (!dialog && IsWindowUnicode(w)) SetWindowTextW(w, text);
    else DefWindowProcW(w, WM_SETTEXT, 0, (LPARAM)text);
    DefWindowProcW(w, WM_SETTEXT, 0, (LPARAM)text);           // and the window's own text, which our drawing reads
    SetPropW(w, L"khd.ko", (HANDLE)1);
    RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}

// other Japanese the settings show (game.ini SupportName on the system information page)
static const struct { const wchar_t* jp; const wchar_t* ko; } kSettingsText[] = {
    { L"ユーザー登録ページへ", L"사용자 등록 페이지" },
};

typedef BOOL(WINAPI* SetDlgItemTextA_t)(HWND, int, LPCSTR);
static SetDlgItemTextA_t realSetDlgItemTextA;
static BOOL WINAPI HookSetDlgItemTextA(HWND dlg, int id, LPCSTR text)
{
    wchar_t w[128];
    if (text && MultiByteToWideChar(932, 0, text, -1, w, 128))
    {
        if (id >= 0x41B && id <= 0x429 && GetDlgItem(dlg, 0x41A))
        {
            for (int g = 0; g < 12; g++)
                if (wcscmp(w, kVoiceNames[g].jp) == 0) { voiceBox[g] = id; SetKoreanText(GetDlgItem(dlg, id), kVoiceNames[g].ko); return TRUE; }
            static volatile LONG said;
            if (InterlockedIncrement(&said) <= 20) Log("sound page: no Korean name for voice box %#x - shown as the game has it", id);
        }
        for (const auto& v : kSettingsText)
            if (wcscmp(w, v.jp) == 0) { SetKoreanText(GetDlgItem(dlg, id), v.ko); return TRUE; }
    }
    return realSetDlgItemTextA(dlg, id, text);
}

// the settings window (it holds the tab control 0x3ED): its title in Korean whenever the game sets it, and its one button:
// 閉じる(&C) in the original (close - nothing is undone), which the English text made Cancel and the Korean 「취소(C)」
static void FixSettingsTitle(HWND w)
{
    if (!w || !GetDlgItem(w, 0x3ED)) return;
    SetKoreanText(w, L"환경 설정");
    SetKoreanText(GetDlgItem(w, IDCANCEL), L"닫기(&C)");
}

// the shortcut list page (dialog 116, Ctrl+B and F in its key column): the last row 「전체 화면 F」 - F asks the engine's
// full-screen switch, which the 4K module keeps off (its 800x600 display mode, 10-16), so F does nothing; the row points
// at the 화면 크기 choices instead
static void FixShortcutPage(HWND page)
{
    HWND f = NULL;
    bool list = false;
    for (HWND c = GetWindow(page, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    {
        char a[16] = "";
        GetWindowTextA(c, a, sizeof a);
        if (strcmp(a, "F") == 0) f = c;
        if (strcmp(a, "Ctrl+B") == 0) list = true;
    }
    if (!f || !list) return;
    RECT r = { 96, 139, 187, 147 };                            // the key column out to the list's right edge (dialog units)
    MapDialogRect(page, &r);
    SetWindowPos(f, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    SetKoreanText(f, L"기본 쪽 「화면 크기」에서");
    Log("settings dialog: the shortcut row 전체 화면 F points at the 화면 크기 choices");
}

// the system information page (edit 0x42E, filled with SetWindowTextA at system.dll RVA 0x724DD whenever the page is
// shown): its first line names Windows from GetVersionEx, which tells a program without a manifest that it runs on
// Windows 8 (6.2, build 9200) - the game has no name for that and shows 「Professional  (Build 9200)」. The line gets
// the real version (RtlGetVersion) and edition instead - from the 64-bit registry view: the 32-bit one this game sees can
// keep an old EditionID (10-18 here: Enterprise there, Professional in the real one); false when the text has no such line.
static bool RealWindowsLine(const char* text, char* out, size_t n)
{
    const char* eol = text ? strstr(text, "\r\n") : NULL;
    const char* old = text ? strstr(text, "(Build 9200)") : NULL;
    if (!eol || !old || old > eol) return false;
    typedef LONG(WINAPI* RtlGetVersion_t)(OSVERSIONINFOW*);
    RtlGetVersion_t getVersion = (RtlGetVersion_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW v = {};
    v.dwOSVersionInfoSize = sizeof v;
    if (!getVersion || getVersion(&v) != 0) return false;
    char edition[64] = "", display[32] = "";
    DWORD cb = sizeof edition;
    RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "EditionID", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, edition, &cb);
    cb = sizeof display;
    RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, display, &cb);
    const char* ed = strcmp(edition, "Professional") == 0 ? "Pro" : strcmp(edition, "Core") == 0 ? "Home" : edition;
    const char* name = v.dwMajorVersion == 10 ? (v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10") : "Windows";
    sprintf_s(out, n, "%s %s%s%s  (Build %lu)%s", name, ed, display[0] ? " " : "", display, v.dwBuildNumber, eol);
    static volatile LONG said;
    if (!InterlockedExchange(&said, 1)) Log("settings dialog: system information names %s %s %s (Build %lu), not the Windows 8 the game is told",
                                            name, ed, display, v.dwBuildNumber);
    return true;
}
// v29 (11-18): the engine's own yes/no prompts (dialog procedure 0x10062da0: SetWindowTextA on its static 0x45C, then on the
// dialog for the title) were English in system.dll - T on a text screen showed 「Return to the title screen?」
static const struct { const char* en; const wchar_t* ko; } kPromptText[] = {
    { "Return to the title screen?", L"타이틀 화면으로 돌아갈까요?" },
    { "Return to choices?", L"선택지로 돌아갈까요?" },
    { "Do you want to exit the game?", L"게임을 끝낼까요?" },      // the same dialog when the window is closed
    { "Skip the movie?", L"영상을 건너뛸까요?" },
};
typedef BOOL(WINAPI* SetWindowTextA_t)(HWND, LPCSTR);
static SetWindowTextA_t realSetWindowTextA;
static BOOL WINAPI HookSetWindowTextA(HWND w, LPCSTR text)
{
    if (w && GetDlgItem(w, 0x3ED)) { FixSettingsTitle(w); return TRUE; }
    if (w && text && GetDlgCtrlID(w) == 0x45C)
        for (const auto& p : kPromptText)
            if (strcmp(text, p.en) == 0)
            {
                SetKoreanText(w, p.ko);
                static volatile LONG said;
                if (InterlockedIncrement(&said) <= 4) Log("prompt in Korean: %ls", p.ko);
                return TRUE;
            }
    if (w && text && GetDlgItem(w, 0x45C) && GetPropW(GetDlgItem(w, 0x45C), L"khd.ko"))   // the prompt's title: say what it is
    {
        wchar_t t[128] = L"";
        MultiByteToWideChar(932, 0, text, -1, t, 128);
        static volatile LONG said;
        if (InterlockedIncrement(&said) <= 2) Log("prompt title as the game has it: %ls", t);
    }
    char out[4096];
    if (w && GetDlgCtrlID(w) == 0x42E && text && strlen(text) < 3800 && RealWindowsLine(text, out, sizeof out))
        return realSetWindowTextA(w, out);
    return realSetWindowTextA(w, text);
}

#include "unlock_all.h"

// ---------------------------------------------------------------- screen size in the settings (10-17)
// user 10-17 「아 그리고 옵션에 고해상도 전환을 넣어줘」: the system settings page (dialog 108: the 창 모드 / 전체 화면 pair
// 0x3F9 / 0x3FA at the top right, hidden boxes below from y 94) gets a group 화면 크기 with four choices, kept in
// autobattle.ini for the next start - the 4K module (khd4k.dll) sets the window up as the game starts. 원래 크기 keeps the
// 4K module at 1x rather than off: it is what keeps the engine's full-screen switch off (10-16 an 800x600 full screen
// left the user without a screen).
// 11-09: 원래 크기 keeps uhd_text 1 too (was 0). The size changes at once now (ApplyScreenNow), but the 4K module reads
// uhd_text only at the start (its text stage is hooked in then or never) - a run started at 원래 크기 with 0 showed a bigger
// size only as the frame scaled up until the next start. At 1x the text stage draws at 5 px per 5-px block, the frame's own
// size (the live test's 1x shots before and after the switches look as before).
struct ScreenChoice { const wchar_t* label; const wchar_t* mode; const wchar_t* scale; const wchar_t* text; };
static const ScreenChoice kScreens[] = {
    { L"원래 크기 (800×600)", L"window", L"1", L"1" },
    { L"2배 창 (1600×1200)", L"window", L"2", L"1" },
    { L"3배 창 (2400×1800)", L"window", L"3", L"1" },
    { L"모니터 전체", L"full", L"3", L"1" },
};
static const int kScreenGroupId = 0x5A10, kScreenFirstId = 0x5A11;
static const int kUnlockId = 0x5A20;                        // v30 모든 콘텐츠 열기

static int CurrentScreen(const wchar_t* ini)
{
    wchar_t mode[32];
    GetPrivateProfileStringW(L"autobattle", L"uhd_mode", L"full", mode, 32, ini);
    int scale = GetPrivateProfileIntW(L"autobattle", L"uhd_scale", 3, ini);
    if (GetPrivateProfileIntW(L"autobattle", L"uhd", 0, ini) != 1) return 0;     // 4K off: the original size
    if (_wcsicmp(mode, L"window") != 0) return 3;
    return scale <= 1 ? 0 : scale == 2 ? 1 : scale == 3 ? 2 : -1;
}

// 11-09 user 「환경설정에서 화면 모드 설정 원본, 2배, 3배, 4배 전체화면이 적용 안되는거 같아」: a choice was only saved for the
// next start (the group said 「다시 켜면 바뀜」 - the 4K module sizes the window as it is made). The 4K module lays its picture
// out again on every WM_SIZE (display.cpp Layout -> UhdSetOutput, which drops and remakes its buffers at the next frame),
// a fight takes the picture's place as its device is made (battle.cpp FixParams) and a movie at every draw - so the window
// itself is changed now, as the 4K module would have made it at the start: 모니터 전체 = borderless over the monitor
// (its style without caption / frame / system menu / minimize), a window = the engine's own style (82CA0000 in its log:
// WS_POPUP | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX) at 800x600 times the scale, its centre kept and
// pushed into the monitor's work area the same way. Not without the 4K module (uhd off at the start): the engine's own
// 800x600 window has nothing that would follow a size change. uhd_text is read at the start only - started at 원래 크기
// (text 0), a bigger size now is the frame scaled up until the next start.
static void ApplyScreenNow(int i)
{
    if (!GetModuleHandleW(L"khd4k.dll")) { Log("screen size: no 4K module in this run - the choice waits for the next start"); return; }
    HWND main = NULL;
    for (HWND w = FindWindowExA(NULL, NULL, "OCGameApplicationWindow", NULL); w; w = FindWindowExA(NULL, w, "OCGameApplicationWindow", NULL))
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId()) { main = w; break; }
    }
    if (!main) { Log("screen size: the game window was not found - the choice waits for the next start"); return; }
    const ScreenChoice& c = kScreens[i];
    bool full = _wcsicmp(c.mode, L"full") == 0;
    int scale = _wtoi(c.scale);
    if (scale < 1 || scale > 4) scale = 3;
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTONEAREST), &mi);
    const DWORD frameBits = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    DWORD style = (DWORD)GetWindowLongW(main, GWL_STYLE), ex = (DWORD)GetWindowLongW(main, GWL_EXSTYLE);
    RECT was;
    GetWindowRect(main, &was);
    int x, y, w, h;
    if (full)
    {
        style = (style & ~frameBits) | WS_POPUP;
        x = mi.rcMonitor.left;
        y = mi.rcMonitor.top;
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    else
    {
        style = (style & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT r = { 0, 0, 800 * scale, 600 * scale };
        AdjustWindowRectEx(&r, style, FALSE, ex);
        w = r.right - r.left;
        h = r.bottom - r.top;
        x = (was.left + was.right) / 2 - w / 2;
        y = (was.top + was.bottom) / 2 - h / 2;
        RECT wa = mi.rcWork;
        if (x + w > wa.right) x = wa.right - w;
        if (y + h > wa.bottom) y = wa.bottom - h;
        if (x < wa.left) x = wa.left;
        if (y < wa.top) y = wa.top;
    }
    SetWindowLongW(main, GWL_STYLE, (LONG)style);
    SetWindowPos(main, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(main, NULL, TRUE);
    RECT cr = {}, now = {};
    GetWindowRect(main, &now);
    // the client as Windows has it (the engine's own GetClientRect is answered 800x600 by the 4K module)
    typedef BOOL(WINAPI* GetClientRect_t)(HWND, LPRECT);
    static GetClientRect_t realClient = (GetClientRect_t)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetClientRect");
    if (realClient) realClient(main, &cr);
    Log("screen size now: choice %d - window (%ld,%ld)-(%ld,%ld) -> (%ld,%ld)-(%ld,%ld), client %ldx%ld, style %08lx%s",
        i, was.left, was.top, was.right, was.bottom, now.left, now.top, now.right, now.bottom, cr.right, cr.bottom,
        (unsigned long)style, full ? " (borderless over the monitor)" : "");
}

static void SetScreen(int i)
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    const ScreenChoice& c = kScreens[i];
    BOOL ok = WritePrivateProfileStringW(L"autobattle", L"uhd", L"1", ini)
              && WritePrivateProfileStringW(L"autobattle", L"uhd_mode", c.mode, ini)
              && WritePrivateProfileStringW(L"autobattle", L"uhd_scale", c.scale, ini)
              && WritePrivateProfileStringW(L"autobattle", L"uhd_text", c.text, ini);
    Log("screen size for the next start: choice %d (uhd=1, %ls x%ls, text %ls)%s", i, c.mode, c.scale, c.text, ok ? "" : " - could not save");
    ApplyScreenNow(i);   // 11-09: and now
}

static WNDPROC systemPageProc;
static LRESULT CALLBACK SystemPageProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int id = LOWORD(wp);
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED && id >= kScreenFirstId && id < kScreenFirstId + 4)
    {
        SetScreen(id - kScreenFirstId);
        return 0;
    }
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED && id == kUnlockId)       // v30 모든 콘텐츠 열기
    {
        HWND box = GetDlgItem(h, kUnlockId);
        bool want = SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (want && MessageBoxW(GetAncestor(h, GA_ROOT),
                L"감상실의 CG·DUEL 기체·스토리 차트·극장·피니셔와 DUEL 정보 창의 잠긴 칸을 모두 엽니다.\n"
                L"이야기 뒷부분의 내용이 보일 수 있습니다.\n\n"
                L"세이브와 진행 기록은 바꾸지 않으므로, 이 칸을 끄면 원래대로 다시 잠깁니다.\n"
                L"모든 콘텐츠를 열까요?", L"모든 콘텐츠 열기", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        {
            SendMessageW(box, BM_SETCHECK, BST_UNCHECKED, 0);
            return 0;
        }
        UaSet(want);
        return 0;
    }
    return CallWindowProcA(systemPageProc, h, msg, wp, lp);
}

// the group under the page's 창 모드 / 전체 화면 group ((166,0) 87x69 and the button at (173,75) 70x16, dialog units)
static void AddScreenChoices(HWND page)
{
    if (GetDlgItem(page, kScreenGroupId)) return;
    HFONT font = (HFONT)SendMessageW(page, WM_GETFONT, 0, 0);
    RECT gr = { 166, 93, 253, 155 };
    MapDialogRect(page, &gr);
    HWND grp = CreateWindowExW(0, L"BUTTON", L"화면 크기", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, gr.left, gr.top,   // 11-09: changes at once now
                               gr.right - gr.left, gr.bottom - gr.top, page, (HMENU)(INT_PTR)kScreenGroupId, selfInst, NULL);
    if (!grp) { Log("settings dialog: could not add the 화면 크기 group (error %lu)", GetLastError()); return; }
    SendMessageW(grp, WM_SETFONT, (WPARAM)font, FALSE);
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    int cur = CurrentScreen(ini);
    for (int i = 0; i < 4; i++)
    {
        RECT r = { 171, 104 + 12 * i, 251, 114 + 12 * i };
        MapDialogRect(page, &r);
        HWND b = CreateWindowExW(0, L"BUTTON", kScreens[i].label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
                                 r.left, r.top, r.right - r.left, r.bottom - r.top, page, (HMENU)(INT_PTR)(kScreenFirstId + i), selfInst, NULL);
        if (!b) continue;
        SendMessageW(b, WM_SETFONT, (WPARAM)font, FALSE);
        if (i == cur) SendMessageW(b, BM_SETCHECK, BST_CHECKED, 0);
    }
    if (uaEvalOrig && !GetDlgItem(page, kUnlockId))         // v30: 모든 콘텐츠 열기
    {
        RECT ur = { 10, 142, 161, 152 };
        MapDialogRect(page, &ur);
        HWND ub = CreateWindowExW(0, L"BUTTON", L"모든 콘텐츠 열기 (감상실·DUEL·차트)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  ur.left, ur.top, ur.right - ur.left, ur.bottom - ur.top, page, (HMENU)(INT_PTR)kUnlockId, selfInst, NULL);
        if (ub)
        {
            SendMessageW(ub, WM_SETFONT, (WPARAM)font, FALSE);
            SendMessageW(ub, BM_SETCHECK, unlockAll ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
    WNDPROC was = (WNDPROC)SetWindowLongPtrA(page, GWLP_WNDPROC, (LONG_PTR)SystemPageProc);
    if (was != SystemPageProc) systemPageProc = was;
    // 전체 화면 (0x3F3, the first box of 기본 설정): the engine's 800x600 switch, kept off by the 4K module - so it did
    // nothing; the 화면 크기 group is the way now
    HWND full = GetDlgItem(page, 0x3F3);
    if (full)
    {
        SendMessageW(full, BM_SETCHECK, BST_UNCHECKED, 0);
        EnableWindow(full, FALSE);
        SetKoreanText(full, L"전체 화면 (→ 오른쪽 「화면 크기」)");
    }
    Log("settings dialog: 화면 크기 choices added (now %d); 전체 화면 box %s", cur, full ? "turned off" : "not found");
}

// ---------------------------------------------------------------- the settings window in the 마도서 look (10-18)
// user 10-17 「옵션 고칠수있어? 디자인도 게임에 어울리게 바꾸고」; 10-18 mockups (khd-runtime-fix\options_redesign) and
// the user's pick, B 마도서: faded parchment (autobattle\ui\parchment.bmp, tools\make_settings_skin.py), the library's
// white blade bands at the top and the bottom (cut from cg\system\ex_cg\cg_back.png), brown ink, crimson headings and
// marks. The controls stay the game's - their states are what it reads when the window closes - only their drawing
// changes:
//   * the window, every page and the tab strip paint one picture (the parchment with its bands, made at the window's
//     size); statics and sliders get it as a brush lined up with their place, edit boxes a lighter parchment;
//   * check boxes, radio buttons, push buttons and sliders draw through NM_CUSTOMDRAW (what they do stays theirs),
//     group boxes paint their frame and heading themselves;
//   * the tabs go to one row under the top band (in two rows they swapped places on every click) and the pages are
//     pinned under them;
//   * the title bar takes the ink colour (Windows 11).
// Text is drawn with the window's own font (KHD Korean): the patch's Korean is kanji codes that font draws as Hangul.
// settings_skin=0 in autobattle.ini leaves the window as the game draws it.
static int skinOn = 1;
static HWND skinDlg;
static HBITMAP skinBg;                       // the window's picture at its client size
static HBRUSH skinBgBrush, skinFillBrush;
static HFONT skinHeadFont, skinNoteFont;
static RECT skinPage;                        // where the pages go, in the window's client coordinates
static int skinW, skinH;
static const int kTopBand = 22;              // px of the top band shown above the tabs
static const COLORREF kInk = RGB(58, 40, 26), kDim = RGB(150, 130, 104), kHead = RGB(140, 30, 28),
                      kLine = RGB(150, 118, 78), kBoxEdge = RGB(110, 82, 52), kBoxFill = RGB(240, 230, 206),
                      kMark = RGB(150, 26, 24), kTabText = RGB(92, 70, 48), kTabOnFill = RGB(236, 224, 196),
                      kBtnFill = RGB(222, 206, 172), kBtnHot = RGB(232, 218, 186), kBtnDown = RGB(206, 188, 150),
                      kBtnEdge = RGB(120, 90, 58);

static HBITMAP SkinBmp(const wchar_t* name)
{
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\ui\\%s", abDir, name);
    return (HBITMAP)LoadImageW(NULL, p, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}

// part of the picture (sx, sy in the window's client) onto dc at x, y
static void SkinPaint(HDC dc, int x, int y, int w, int h, int sx, int sy)
{
    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(src, skinBg);
    BitBlt(dc, x, y, w, h, src, sx, sy, SRCCOPY);
    SelectObject(src, o);
    DeleteDC(src);
}

// where w's client starts in the settings window's client
static POINT SkinOrigin(HWND w)
{
    POINT p = { 0, 0 };
    MapWindowPoints(w, skinDlg, &p, 1);
    return p;
}

static void RoundBox(HDC dc, RECT r, COLORREF fill, COLORREF edge, int rad)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}

static void SkinLine(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int width)
{
    HPEN p = CreatePen(PS_SOLID, width, c);
    HGDIOBJ op = SelectObject(dc, p);
    MoveToEx(dc, x0, y0, NULL); LineTo(dc, x1, y1);
    SelectObject(dc, op); DeleteObject(p);
}

static bool SkinBuild(HWND dlg)
{
    RECT rc;
    GetClientRect(dlg, &rc);
    skinW = rc.right; skinH = rc.bottom;
    HBITMAP parch = SkinBmp(L"parchment.bmp"), top = SkinBmp(L"band_top.bmp"), bot = SkinBmp(L"band_bottom.bmp");
    if (!parch || !top || !bot || skinW <= 0 || skinH <= 0)
    {
        if (parch) DeleteObject(parch);
        if (top) DeleteObject(top);
        if (bot) DeleteObject(bot);
        Log("settings look: the pictures in autobattle\\ui are missing - the game's own look stays");
        return false;
    }
    HDC screen = GetDC(NULL);
    skinBg = CreateCompatibleBitmap(screen, skinW, skinH);
    HDC dc = CreateCompatibleDC(screen), src = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    HGDIOBJ od = SelectObject(dc, skinBg);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    BITMAP bm;
    HGDIOBJ os = SelectObject(src, parch);
    GetObjectW(parch, sizeof bm, &bm);
    StretchBlt(dc, 0, 0, skinW, skinH, src, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(src, top);                                     // the top band's lower edge, as tall as kTopBand
    GetObjectW(top, sizeof bm, &bm);
    int srcKeep = kTopBand * bm.bmWidth / skinW;
    StretchBlt(dc, 0, 0, skinW, kTopBand, src, 0, bm.bmHeight - srcKeep, bm.bmWidth, srcKeep, SRCCOPY);
    SelectObject(src, bot);                                     // the bottom band whole, at the window's width
    GetObjectW(bot, sizeof bm, &bm);
    int botH = bm.bmHeight * skinW / bm.bmWidth;
    StretchBlt(dc, 0, skinH - botH, skinW, botH, src, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SkinLine(dc, 0, kTopBand, skinW, kTopBand, kLine, 1);
    SkinLine(dc, 0, skinH - botH - 1, skinW, skinH - botH - 1, kLine, 1);
    SelectObject(src, os); SelectObject(dc, od);
    DeleteDC(src); DeleteDC(dc);
    DeleteObject(parch); DeleteObject(top); DeleteObject(bot);
    skinBgBrush = CreatePatternBrush(skinBg);
    skinFillBrush = CreateSolidBrush(kBoxFill);
    HFONT f = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);
    LOGFONTW lf;
    if (f && GetObjectW(f, sizeof lf, &lf))
    {
        LOGFONTW h = lf;
        h.lfWeight = FW_BOLD;
        h.lfHeight = lf.lfHeight * 115 / 100;
        skinHeadFont = CreateFontIndirectW(&h);
        LOGFONTW n = lf;
        n.lfHeight = lf.lfHeight * 85 / 100;
        skinNoteFont = CreateFontIndirectW(&n);
    }
    return true;
}

static void SkinFree()
{
    if (skinBgBrush) DeleteObject(skinBgBrush);
    if (skinFillBrush) DeleteObject(skinFillBrush);
    if (skinBg) DeleteObject(skinBg);
    if (skinHeadFont) DeleteObject(skinHeadFont);
    if (skinNoteFont) DeleteObject(skinNoteFont);
    skinBgBrush = skinFillBrush = NULL;
    skinBg = NULL;
    skinHeadFont = skinNoteFont = NULL;
    skinDlg = NULL;
    SetRectEmpty(&skinPage);
}

// one subclass per window (the window procedure it replaced is kept as the window's property), in the window's own
// character set: our Unicode controls subclassed the ANSI way lost their Hangul to ? (10-18 「?? ??」)
static void SkinSubclass(HWND h, WNDPROC proc)
{
    if (!h || GetPropW(h, L"khd.skin")) return;
    bool wide = IsWindowUnicode(h) != FALSE;
    WNDPROC prev = (WNDPROC)(wide ? SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)proc) : SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)proc));
    SetPropW(h, L"khd.skin", (HANDLE)prev);
    if (wide) SetPropW(h, L"khd.skin.w", (HANDLE)1);
}
static LRESULT SkinPrev(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    WNDPROC prev = (WNDPROC)GetPropW(h, L"khd.skin");
    bool wide = GetPropW(h, L"khd.skin.w") != NULL;
    if (m == WM_NCDESTROY) { RemovePropW(h, L"khd.skin"); RemovePropW(h, L"khd.skin.w"); RemovePropW(h, L"khd.ko"); }
    if (!prev) return wide ? DefWindowProcW(h, m, wp, lp) : DefWindowProcA(h, m, wp, lp);
    return wide ? CallWindowProcW(prev, h, m, wp, lp) : CallWindowProcA(prev, h, m, wp, lp);
}

// WM_CTLCOLOR*: edit boxes on light parchment, the rest on the picture as it lies under them
static LRESULT SkinColor(UINT m, HDC dc, HWND ctl)
{
    wchar_t cls[16] = L"";
    GetClassNameW(ctl, cls, 16);
    if (m == WM_CTLCOLOREDIT || _wcsicmp(cls, L"Edit") == 0)
    {
        SetTextColor(dc, IsWindowEnabled(ctl) ? kInk : kDim);
        SetBkColor(dc, kBoxFill);
        return (LRESULT)skinFillBrush;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, !IsWindowEnabled(ctl) ? kDim : GetDlgCtrlID(ctl) == 0x430 ? kHead : kInk);   // 0x430: the home page link
    // the brush lined up through the DC's own place on the screen: statics and buttons draw in their page's DC
    // (CS_PARENTDC), so lining it up with the control's place took the band's dark gears at a page's top (10-18)
    if (GetObjectType(dc) == OBJ_MEMDC)                         // Common-Controls 6 draws in a memory DC: the control's own
    {
        POINT c = SkinOrigin(ctl);
        SetBrushOrgEx(dc, -c.x, -c.y, NULL);
    }
    else
    {
        POINT org = { 0, 0 }, d = { 0, 0 };
        GetDCOrgEx(dc, &org);
        ClientToScreen(skinDlg, &d);
        SetBrushOrgEx(dc, d.x - org.x, d.y - org.y, NULL);
    }
    return (LRESULT)skinBgBrush;
}

static UINT SkinPrefix(HWND b)
{
    return (SendMessageW(b, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0;
}

static void SkinButton(NMCUSTOMDRAW* cd, LONG type)
{
    HWND b = cd->hdr.hwndFrom;
    HDC dc = cd->hdc;
    RECT r = cd->rc;
    POINT o = SkinOrigin(b);
    SkinPaint(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, o.x + r.left, o.y + r.top);
    bool disabled = !IsWindowEnabled(b) || (cd->uItemState & CDIS_DISABLED);
    bool hot = (cd->uItemState & CDIS_HOT) != 0, down = (cd->uItemState & CDIS_SELECTED) != 0;
    wchar_t text[256] = L"";
    InternalGetWindowText(b, text, 256);
    HFONT f = (HFONT)SendMessageW(b, WM_GETFONT, 0, 0);
    HGDIOBJ of = SelectObject(dc, f ? (HGDIOBJ)f : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    UINT dt = DT_NOCLIP | SkinPrefix(b);
    if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON)
    {
        RECT bx = r;
        InflateRect(&bx, -1, -1);
        RoundBox(dc, bx, down ? kBtnDown : hot ? kBtnHot : kBtnFill, disabled ? kDim : hot ? kMark : kBtnEdge, 7);
        SetTextColor(dc, disabled ? kDim : kInk);
        RECT tr = r;
        if (down) OffsetRect(&tr, 1, 1);
        DrawTextW(dc, text, -1, &tr, dt | DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else                                                        // check box or radio button
    {
        LRESULT check = SendMessageW(b, BM_GETCHECK, 0, 0);
        bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        const int s = 13;
        RECT tr = { r.left + 1 + s + 6, r.top, r.right, r.bottom };
        RECT calc = tr;
        DrawTextW(dc, text, -1, &calc, dt | DT_WORDBREAK | DT_CALCRECT);
        int th = calc.bottom - calc.top, y0 = r.top + max(0, (int)(r.bottom - r.top - th) / 2);
        int gy = y0 + (tm.tmHeight - s) / 2;
        RECT box = { r.left + 1, gy, r.left + 1 + s, gy + s };
        COLORREF edge = disabled ? kDim : hot ? kMark : kBoxEdge;
        HBRUSH fb = CreateSolidBrush(kBoxFill);
        HPEN ep = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ ob = SelectObject(dc, fb), op = SelectObject(dc, ep);
        if (radio) Ellipse(dc, box.left, box.top, box.right, box.bottom);
        else Rectangle(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(fb); DeleteObject(ep);
        COLORREF mark = disabled ? kDim : kMark;
        if (radio && check == BST_CHECKED)
        {
            HBRUSH mb = CreateSolidBrush(mark);
            HPEN mp = CreatePen(PS_SOLID, 1, mark);
            ob = SelectObject(dc, mb); op = SelectObject(dc, mp);
            Ellipse(dc, box.left + 3, box.top + 3, box.right - 3, box.bottom - 3);
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(mb); DeleteObject(mp);
        }
        else if (!radio && check == BST_CHECKED)
        {
            HPEN mp = CreatePen(PS_SOLID, 2, mark);
            op = SelectObject(dc, mp);
            POINT pts[3] = { { box.left + 3, box.top + 6 }, { box.left + 5, box.top + 9 }, { box.right - 3, box.top + 3 } };
            Polyline(dc, pts, 3);
            SelectObject(dc, op);
            DeleteObject(mp);
        }
        else if (!radio && check == BST_INDETERMINATE)
        {
            RECT in = { box.left + 3, box.top + 3, box.right - 3, box.bottom - 3 };
            HBRUSH mb = CreateSolidBrush(kDim);
            FillRect(dc, &in, mb);
            DeleteObject(mb);
        }
        SetTextColor(dc, disabled ? kDim : kInk);
        RECT out = { tr.left, y0, tr.right, r.bottom };
        DrawTextW(dc, text, -1, &out, dt | DT_WORDBREAK);
        if ((cd->uItemState & CDIS_FOCUS) && !(SendMessageW(b, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
        {
            RECT fr = { calc.left - 2, y0 - 1, calc.left + (calc.right - calc.left) + 2, y0 + th + 1 };
            DrawFocusRect(dc, &fr);
        }
    }
    SelectObject(dc, of);
}

// NM_CUSTOMDRAW of buttons and sliders; false = leave it to the control
static bool SkinCustomDraw(NMCUSTOMDRAW* cd, LRESULT* res)
{
    if (!skinBg) return false;
    HWND w = cd->hdr.hwndFrom;
    wchar_t cls[32] = L"";
    GetClassNameW(w, cls, 32);
    if (_wcsicmp(cls, L"Button") == 0)
    {
        LONG type = GetWindowLongW(w, GWL_STYLE) & BS_TYPEMASK;
        if (type == BS_GROUPBOX || type == BS_OWNERDRAW || type == BS_USERBUTTON) return false;
        if (cd->dwDrawStage == CDDS_PREPAINT) { *res = CDRF_NOTIFYPOSTPAINT; return true; }
        if (cd->dwDrawStage != CDDS_POSTPAINT) return false;
        SkinButton(cd, type);                                   // over the control's own picture, all of it
        *res = CDRF_DODEFAULT;
        return true;
    }
    if (_wcsicmp(cls, L"msctls_trackbar32") == 0)
    {
        if (cd->dwDrawStage == CDDS_PREPAINT)
        {
            RECT rc;
            GetClientRect(w, &rc);
            POINT o = SkinOrigin(w);
            SkinPaint(cd->hdc, 0, 0, rc.right, rc.bottom, o.x, o.y);
            *res = CDRF_NOTIFYITEMDRAW;
            return true;
        }
        if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return false;
        HDC dc = cd->hdc;
        if (cd->dwItemSpec == TBCD_CHANNEL)                     // brown line, crimson up to the thumb
        {
            RECT rc;
            GetClientRect(w, &rc);
            POINT o = SkinOrigin(w);
            SkinPaint(dc, 0, 0, rc.right, rc.bottom, o.x, o.y);   // the whole slider's place again (its own erase came between)
            RECT th;
            SendMessageW(w, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
            int cy = (cd->rc.top + cd->rc.bottom) / 2, tx = (th.left + th.right) / 2;
            SkinLine(dc, cd->rc.left + 2, cy, cd->rc.right - 2, cy, kLine, 2);
            SkinLine(dc, cd->rc.left + 2, cy, tx, cy, IsWindowEnabled(w) ? kMark : kDim, 2);
        }
        else if (cd->dwItemSpec == TBCD_THUMB)                  // a diamond
        {
            RECT t = cd->rc;
            int cx = (t.left + t.right) / 2, cy = (t.top + t.bottom) / 2, rr = min(7, (int)(t.bottom - t.top) / 2);
            POINT pts[4] = { { cx, cy - rr }, { cx + rr * 3 / 4, cy }, { cx, cy + rr }, { cx - rr * 3 / 4, cy } };
            bool hot = (cd->uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
            HBRUSH b = CreateSolidBrush(!IsWindowEnabled(w) ? kDim : hot ? RGB(190, 40, 34) : kMark);
            HPEN p = CreatePen(PS_SOLID, 1, kBoxEdge);
            HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
            Polygon(dc, pts, 4);
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(b); DeleteObject(p);
        }
        *res = CDRF_SKIPDEFAULT;                                // the tics too: none
        return true;
    }
    return false;
}

static LRESULT CALLBACK SkinGroupProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (skinBg && m == WM_ERASEBKGND) return 1;
    if (skinBg && m == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        wchar_t text[160] = L"";
        InternalGetWindowText(h, text, 160);
        HGDIOBJ of = SelectObject(dc, skinHeadFont ? (HGDIOBJ)skinHeadFont : (HGDIOBJ)SendMessageW(h, WM_GETFONT, 0, 0));
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        int top = tm.tmHeight / 2;
        HPEN p = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, r.left, r.top + top, r.right, r.bottom, 9, 9);
        SelectObject(dc, op); SelectObject(dc, ob);
        DeleteObject(p);
        if (text[0])                                            // the heading on a patch of the picture; "(...)" smaller
        {
            wchar_t* note = wcsstr(text, L" (");
            if (note) *note++ = 0;
            SIZE a = {}, b = {};
            GetTextExtentPoint32W(dc, text, (int)wcslen(text), &a);
            if (note && skinNoteFont)
            {
                SelectObject(dc, skinNoteFont);
                GetTextExtentPoint32W(dc, note, (int)wcslen(note), &b);
            }
            int x = r.left + 8, w = a.cx + (note ? b.cx + 4 : 0) + 6;
            if (x + w > r.right - 4) w = r.right - 4 - x;
            POINT o = SkinOrigin(h);
            SkinPaint(dc, x, r.top, w, tm.tmHeight, o.x + x, o.y + r.top);
            SetBkMode(dc, TRANSPARENT);
            SelectObject(dc, skinHeadFont ? (HGDIOBJ)skinHeadFont : (HGDIOBJ)SendMessageW(h, WM_GETFONT, 0, 0));
            SetTextColor(dc, kHead);
            RECT tr = { x + 3, r.top, x + w, r.top + tm.tmHeight };
            DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER | DT_END_ELLIPSIS);
            if (note && skinNoteFont)
            {
                SelectObject(dc, skinNoteFont);
                SetTextColor(dc, kDim);
                RECT nr = { x + 3 + a.cx + 4, r.top, x + w, r.top + tm.tmHeight };
                DrawTextW(dc, note, -1, &nr, DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER | DT_END_ELLIPSIS);
            }
        }
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0;
    }
    return SkinPrev(h, m, wp, lp);
}

// a static we put Hangul in (SetKoreanText): drawn here from the window's own text - Common-Controls 6 drew its copy of
// the text as ? (10-18, the shortcut row 「기본 쪽 「화면 크기」에서」)
static LRESULT CALLBACK SkinStaticProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (skinBg && m == WM_ERASEBKGND) return 1;
    if (skinBg && m == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        POINT o = SkinOrigin(h);
        SkinPaint(dc, 0, 0, r.right, r.bottom, o.x, o.y);
        wchar_t text[256] = L"";
        InternalGetWindowText(h, text, 256);
        HGDIOBJ of = SelectObject(dc, (HGDIOBJ)SendMessageW(h, WM_GETFONT, 0, 0));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, IsWindowEnabled(h) ? kInk : kDim);
        LONG st = GetWindowLongW(h, GWL_STYLE);
        UINT align = (st & SS_TYPEMASK) == SS_CENTER ? DT_CENTER : (st & SS_TYPEMASK) == SS_RIGHT ? DT_RIGHT : DT_LEFT;
        DrawTextW(dc, text, -1, &r, align | DT_NOPREFIX | ((st & SS_CENTERIMAGE) ? DT_VCENTER | DT_SINGLELINE : DT_WORDBREAK));
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0;
    }
    return SkinPrev(h, m, wp, lp);
}

static LRESULT CALLBACK SkinPageProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (skinBg)
        switch (m)
        {
        case WM_ERASEBKGND:
        case WM_PRINTCLIENT:
        {
            RECT rc;
            GetClientRect(h, &rc);
            POINT o = SkinOrigin(h);
            SkinPaint((HDC)wp, 0, 0, rc.right, rc.bottom, o.x, o.y);
            return 1;
        }
        case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORDLG:
            return SkinColor(m, (HDC)wp, (HWND)lp);
        case WM_NOTIFY:
            if (((NMHDR*)lp)->code == NM_CUSTOMDRAW)
            {
                LRESULT r;
                if (SkinCustomDraw((NMCUSTOMDRAW*)lp, &r)) return r;
            }
            break;
        case WM_HSCROLL:                                        // our voice level sliders (the game's own pass on)
        {
            int id = lp ? GetDlgCtrlID((HWND)lp) : 0;
            if (id >= kVoiceCtlBase && id < kVoiceCtlBase + 60 && (id - kVoiceCtlBase) % 4 == 3) { VoiceLevelMoved(h, (id - kVoiceCtlBase) / 4); return 0; }
            break;
        }
        case WM_WINDOWPOSCHANGING:                              // pinned under the tab strip
        {
            WINDOWPOS* p = (WINDOWPOS*)lp;
            if (!(p->flags & SWP_NOMOVE) && skinPage.bottom > skinPage.top) { p->x = skinPage.left; p->y = skinPage.top; }
            break;
        }
        }
    return SkinPrev(h, m, wp, lp);
}

static LRESULT CALLBACK SkinTabProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (skinBg && m == WM_ERASEBKGND) return 1;
    if (skinBg && m == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        POINT o = SkinOrigin(h);
        SkinPaint(dc, 0, 0, rc.right, rc.bottom, o.x, o.y);
        int n = (int)SendMessageW(h, TCM_GETITEMCOUNT, 0, 0), cur = (int)SendMessageW(h, TCM_GETCURSEL, 0, 0);
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        HGDIOBJ of = SelectObject(dc, f ? (HGDIOBJ)f : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        for (int i = 0; i < n; i++)
        {
            RECT t;
            if (!SendMessageW(h, TCM_GETITEMRECT, i, (LPARAM)&t)) continue;
            wchar_t text[64] = L"";
            TCITEMW it = {};
            it.mask = TCIF_TEXT; it.pszText = text; it.cchTextMax = 64;
            SendMessageW(h, TCM_GETITEMW, i, (LPARAM)&it);
            if (i == cur)
            {
                RECT pl = { t.left + 1, t.top + 1, t.right - 1, t.bottom + 3 };
                RoundBox(dc, pl, kTabOnFill, kLine, 6);
                SkinLine(dc, t.left + 5, t.bottom - 2, t.right - 5, t.bottom - 2, kMark, 2);
            }
            SetTextColor(dc, i == cur ? kHead : kTabText);
            DrawTextW(dc, text, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0;
    }
    return SkinPrev(h, m, wp, lp);
}

static LRESULT CALLBACK SkinDlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (skinBg)
        switch (m)
        {
        case WM_ERASEBKGND:
            SkinPaint((HDC)wp, 0, 0, skinW, skinH, 0, 0);
            return 1;
        case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLORDLG:
            return SkinColor(m, (HDC)wp, (HWND)lp);
        case WM_NOTIFY:
            if (((NMHDR*)lp)->code == NM_CUSTOMDRAW)
            {
                LRESULT r;
                if (SkinCustomDraw((NMCUSTOMDRAW*)lp, &r)) return r;
            }
            break;
        case WM_DESTROY:
            SkinFree();
            break;
        }
    return SkinPrev(h, m, wp, lp);
}

// the tabs in one row under the top band; the pages go where that row leaves the display area
static void SkinTabs(HWND tab)
{
    RECT r;
    GetWindowRect(tab, &r);
    MapWindowPoints(NULL, skinDlg, (POINT*)&r, 2);
    int n = (int)SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0), w = r.right - r.left, top = kTopBand + 5;
    SetWindowLongW(tab, GWL_STYLE, (GetWindowLongW(tab, GWL_STYLE) & ~TCS_MULTILINE) | TCS_FIXEDWIDTH);
    if (n > 0) SendMessageW(tab, TCM_SETITEMSIZE, 0, MAKELPARAM((w - 6) / n, 25));
    SetWindowPos(tab, NULL, r.left, top, w, r.bottom - top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RECT d = { r.left, top, r.left + w, r.bottom };
    SendMessageW(tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&d);
    skinPage = d;
}

// The game has no manifest, so its controls are the old common controls, whose check boxes, radio buttons and push
// buttons never ask the parent to draw them (NM_CUSTOMDRAW; 10-18 they stayed white boxes with black ticks). The settings
// window (dialog 106, DialogBoxParamA) is opened with autobattle\ui\khd_v6.manifest active - Common-Controls 6 - so it
// and its pages (made inside its modal loop) get the newer controls; the rest of the game stays as it was.
static HANDLE skinActCtx = INVALID_HANDLE_VALUE;
static bool skinActTried;
typedef INT_PTR(WINAPI* DialogBoxParamA_t)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
static DialogBoxParamA_t realDialogBoxParamA;
static INT_PTR WINAPI HookDialogBoxParamA(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM lp)
{
    ULONG_PTR cookie = 0;
    bool act = false;
    if (skinOn && IS_INTRESOURCE(tmpl) && LOWORD((UINT_PTR)tmpl) == 106)
    {
        if (!skinActTried)
        {
            skinActTried = true;
            wchar_t p[MAX_PATH];
            swprintf_s(p, L"%s\\ui\\khd_v6.manifest", abDir);
            ACTCTXW ac = {};
            ac.cbSize = sizeof ac;
            ac.lpSource = p;
            skinActCtx = CreateActCtxW(&ac);
            if (skinActCtx == INVALID_HANDLE_VALUE) Log("settings look: no Common-Controls 6 context (error %lu) - old check boxes", GetLastError());
        }
        act = skinActCtx != INVALID_HANDLE_VALUE && ActivateActCtx(skinActCtx, &cookie);
    }
    INT_PTR r = realDialogBoxParamA(inst, tmpl, parent, proc, lp);
    if (act) DeactivateActCtx(0, cookie);
    return r;
}

// the settings window, when its first page is made into it
static void SkinSettings(HWND dlg)
{
    if (!skinOn || !dlg || dlg == skinDlg) return;
    HWND tab = GetDlgItem(dlg, 0x3ED);
    if (!tab || (skinDlg && IsWindow(skinDlg))) return;
    skinDlg = dlg;
    if (!SkinBuild(dlg)) { skinDlg = NULL; skinOn = 0; return; }
    SkinTabs(tab);
    SkinSubclass(dlg, SkinDlgProc);
    SkinSubclass(tab, SkinTabProc);
    typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    DwmSetWindowAttribute_t setAttr = dwm ? (DwmSetWindowAttribute_t)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute") : NULL;
    if (setAttr)                                                // DWMWA_BORDER_COLOR 34, CAPTION_COLOR 35, TEXT_COLOR 36
    {
        COLORREF border = kBoxEdge, caption = RGB(74, 52, 34), title = kBoxFill;
        setAttr(dlg, 34, &border, sizeof border);
        setAttr(dlg, 35, &caption, sizeof caption);
        setAttr(dlg, 36, &title, sizeof title);
    }
    InvalidateRect(dlg, NULL, TRUE);
    Log("settings look: 마도서 (window %dx%d, tabs in one row, pages at %ld,%ld)", skinW, skinH, skinPage.left, skinPage.top);
}

// a page made into the settings window: its own drawing, group boxes, its place under the tabs
static void SkinPage(HWND page)
{
    if (!skinBg || !page) return;
    SkinSubclass(page, SkinPageProc);
    for (HWND c = GetWindow(page, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    {
        wchar_t cls[16] = L"";
        GetClassNameW(c, cls, 16);
        if (_wcsicmp(cls, L"Button") == 0 && (GetWindowLongW(c, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) SkinSubclass(c, SkinGroupProc);
        else if (_wcsicmp(cls, L"Static") == 0 && GetPropW(c, L"khd.ko")) SkinSubclass(c, SkinStaticProc);
    }
    if (skinPage.bottom > skinPage.top)
        SetWindowPos(page, NULL, skinPage.left, skinPage.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(page, NULL, TRUE);
}

// the difficulty dialog (has the buttons 쉬움 0x47B and 보통 0x478): show the hidden 어려움 0x47C - the dialog already
// checks it when the difficulty is 2 and reads it back on OK (system.dll RVA 0x72089, 0x72CA6) - and add 자동 전투;
// the system page (0x3F9, 0x3FA, 0x459): add 화면 크기
static HWND WINAPI HookCreateDialogParamA(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM lp)
{
    HWND h = realCreateDialogParamA(inst, tmpl, parent, proc, lp);
    FixSettingsTitle(parent);                                  // the pages are made into the settings window
    bool settingsPage = h && parent && GetDlgItem(parent, 0x3ED);
    if (settingsPage) SkinSettings(parent);                    // the 마도서 look, from the first page on
    if (h && GetDlgItem(h, 0x3F9) && GetDlgItem(h, 0x3FA) && GetDlgItem(h, 0x459)) AddScreenChoices(h);
    if (h && GetDlgItem(parent, 0x3ED)) FixShortcutPage(h);
    if (h && GetDlgItem(h, 0x42E) && GetDlgItem(h, 0x454))   // the system information page: its button from game.ini
    {
        char a[128] = "";
        wchar_t w[128] = L"";
        GetWindowTextA(GetDlgItem(h, 0x454), a, sizeof a);
        MultiByteToWideChar(932, 0, a, -1, w, 128);
        for (const auto& v : kSettingsText)
            if (wcscmp(w, v.jp) == 0) SetKoreanText(GetDlgItem(h, 0x454), v.ko);
    }
    if (h && GetDlgItem(h, 0x47B) && GetDlgItem(h, 0x478))
    {
        HWND hard = GetDlgItem(h, 0x47C);
        if (hardMode && hard && !(GetWindowLongW(hard, GWL_STYLE) & WS_VISIBLE))
        {
            ShowWindow(hard, SW_SHOWNA);
            Log("settings dialog: the hidden third difficulty button (0x47C) shown");
        }
        AddAutoBattleBox(h);
        AddDifficultyNotes(h);   // 11-09: what 쉬움 / 보통 / 어려움 change, under the buttons
    }
    if (settingsPage && GetDlgItem(h, 0x41A) && GetDlgItem(h, 0x41B)) AddVoiceLevels(h);   // the sound page
    if (settingsPage) SkinPage(h);                             // after our own controls are in it
    return h;
}

// the difficulty system.dll hands the battle (0 easy, 1 normal, 2 hard): its variable, found through the instruction
// that stores 2 for the third button (RVA 0x72CB2, C7 05 <address> 02 00 00 00) - else the saved setting
static int Difficulty()
{
    HMODULE sys = GetModuleHandleW(L"system.dll");
    if (sys)
    {
        BYTE* b = (BYTE*)sys;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
        DWORD size = nt->OptionalHeader.SizeOfImage;
        BYTE* at = b + 0x72CB2;
        if (0x72CB2 + 10 < size && at[0] == 0xC7 && at[1] == 0x05 && *(DWORD*)(at + 6) == 2)
        {
            BYTE* var = (BYTE*)(ULONG_PTR)*(DWORD*)(at + 2);
            if (var >= b && var + 4 <= b + size) return *(volatile int*)var;
        }
    }
    DWORD v = 0, cb = sizeof v;
    if (RegGetValueA(HKEY_CURRENT_USER, ownSettings ? kOwnKey : kSharedKey, "ActDifficult", RRF_RT_REG_DWORD, NULL, &v, &cb) == ERROR_SUCCESS) return (int)v;
    return -1;
}

static bool SameFile(const wchar_t* a, const wchar_t* b)
{
    HANDLE fa = CreateFileW(a, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE fb = CreateFileW(b, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    bool same = false;
    LARGE_INTEGER sa, sb;
    if (fa != INVALID_HANDLE_VALUE && fb != INVALID_HANDLE_VALUE && GetFileSizeEx(fa, &sa) && GetFileSizeEx(fb, &sb) && sa.QuadPart == sb.QuadPart)
    {
        static BYTE x[65536], y[65536];
        same = true;
        for (;;)
        {
            DWORD na = 0, nb = 0;
            if (!ReadFile(fa, x, sizeof x, &na, NULL) || !ReadFile(fb, y, sizeof y, &nb, NULL) || na != nb || memcmp(x, y, na) != 0) { same = false; break; }
            if (na == 0) break;
        }
    }
    if (fa != INVALID_HANDLE_VALUE) CloseHandle(fa);
    if (fb != INVALID_HANDLE_VALUE) CloseHandle(fb);
    return same;
}

static void SyncHardPack()
{
    wchar_t src[MAX_PATH], dst[MAX_PATH];
    swprintf_s(src, L"%s\\hard\\dmpack900_hard.dat", abDir);
    swprintf_s(dst, L"%s\\dmpack900_hard.dat", gameDir);
    int diff = DuelMenuContext() ? 1 : Difficulty(); // a story Hard pack must not enter DUEL
    bool have = GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES;
    if (diff == 2)
    {
        if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES) { Log("difficulty 2 but no %ls", src); return; }
        if (have && SameFile(src, dst)) return;
        if (CopyFileW(src, dst, FALSE)) Log("difficulty 2: Hard pack in (Normal's numbers, more skills)");
        else Log("difficulty 2: could not copy the Hard pack (error %lu)", GetLastError());
    }
    else if (have)   // the name is ours; an older build of the pack goes out too
    {
        if (DeleteFileW(dst)) Log("difficulty %d: Hard pack out (the original game)", diff);
        else Log("difficulty %d: could not remove the Hard pack (error %lu)", diff, GetLastError());
    }
}

// v29 (11-18): the message boxes of the engine (system.dll) and of the battle module (demonbane.dll) were Japanese - closing
// the window during a fight asked 「終了します。よろしいですか？…」. The game runs with code page 932, so the Korean goes
// through MessageBoxW; a text not in the table is shown as the game has it.
static const struct { const wchar_t* jp; const wchar_t* ko; } kBoxText[] = {
    { L"終了します。よろしいですか？\n\n(データロード中はロード完了後終了します）", L"게임을 끝낼까요?\n\n(불러오는 중이면 다 불러온 뒤에 끝납니다)" },
    { L"オーディオデバイスが変更されました。正常動作が保証されませんので終了します。\nよろしいですか？",
      L"오디오 장치가 바뀌어 정상 동작을 보장할 수 없으므로 게임을 끝냅니다.\n괜찮습니까?" },
    { L"Windowが作成できませんでした。", L"창을 만들지 못했습니다." },
    { L"DirectX8.1以降がインストールされていません。\nDirectXをインストール後、実行してください",
      L"DirectX 8.1 이상이 설치되어 있지 않습니다.\nDirectX 를 설치한 뒤 실행해 주세요." },
    { L"協調レベルが設定できませんでした。", L"협조 수준을 설정하지 못했습니다." },
    { L"DirectDrawが取得できませんでした。", L"DirectDraw 를 얻지 못했습니다." },
    { L"メモリー確保に失敗しました。", L"메모리를 확보하지 못했습니다." },
};
static const struct { const wchar_t* jp; const wchar_t* ko; } kBoxCaption[] = {
    { L"機神飛翔デモンベイン", L"기신비상 데몬베인" },
    { L"ご確認ください", L"확인해 주세요" },
};
typedef int(WINAPI* MessageBoxA_t)(HWND, LPCSTR, LPCSTR, UINT);
static MessageBoxA_t realMessageBoxA;
static int WINAPI HookMessageBoxA(HWND w, LPCSTR text, LPCSTR cap, UINT type)
{
    static wchar_t t[1024], c[256], buf[512];
    if (text && MultiByteToWideChar(932, 0, text, -1, t, 1024))
    {
        const wchar_t* ko = NULL;
        for (const auto& b : kBoxText)
            if (wcscmp(t, b.jp) == 0) ko = b.ko;
        int sw = 0, sh = 0;
        if (!ko && swscanf_s(t, L"現在の画面サイズが%d×%d", &sw, &sh) == 2)
        {
            swprintf_s(buf, L"지금 화면 크기가 %d×%d 이하입니다.\n전체 화면 모드로 바꿀까요?\n(취소하면 끝납니다)", sw, sh);
            ko = buf;
        }
        if (ko)
        {
            const wchar_t* kc = NULL;
            if (cap && MultiByteToWideChar(932, 0, cap, -1, c, 256))
            {
                kc = c;
                for (const auto& b : kBoxCaption)
                    if (wcscmp(c, b.jp) == 0) kc = b.ko;
            }
            static volatile LONG said;
            if (InterlockedIncrement(&said) <= 6) Log("message box in Korean (%d chars)", (int)wcslen(ko));
            return MessageBoxW(w, ko, kc, type);
        }
    }
    MessageBoxA_t mbReal = realMessageBoxA ? realMessageBoxA : (MessageBoxA_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "MessageBoxA");
    return mbReal(w, text, cap, type);
}

static HMODULE WINAPI HookLoadLibraryA(LPCSTR name)
{
    if (hardMode && name)
    {
        const char* file = strrchr(name, '\\');
        file = file ? file + 1 : name;
        if (_stricmp(file, "demonbane.dll") == 0) SyncHardPack();   // the battle module reads dmpack*.dat as it starts
    }
    HMODULE m = realLoadLibraryA(name);
    if (m && name)
    {
        const char* file = strrchr(name, '\\');
        file = file ? file + 1 : name;
        static void* unused;
        if (_stricmp(file, "demonbane.dll") == 0) HookImport(m, "USER32.dll", "MessageBoxA", (void*)HookMessageBoxA, &unused);
    }
    return m;
}

// ---------------------------------------------------------------- 4K work (10-15): uhd>=1 loads autobattle\khd4k.dll
// right away, from DllMain (system.dll's imports are bound and none of its code has run yet), so it can hook the
// window class and the settings reads. uhd=0 (the default) leaves everything as it was.
static int uhd;

// ---------------------------------------------------------------- story voices with subtitles (10-18)
// user 10-18 「패배시 패배 대사가 재생되는데 해당 대사들의 자막을 띄워줄수 있을까?」 → 「전투 모듈이 아니야 패배 스샷이
// 나오는 부분이야 전투가 종료되고」: after a lost fight the chapter script (da*.nps #負け) plays lines with no text box.
// For every voice system.dll opens 0voice.pak and voice.pak, reads their tables and then the voice from its first byte
// (10-18 with trace_files=1: voice\da01\0001f.ogg read from its start; no file names pass the file functions, and no
// moves - the read position is the handle's). autobattle\story_subs.tsv (tools\make_story_subs.py) lists those voices
// by pack and first byte; when system.dll reads one of them, its row goes on the game window as the property
// "khd.story" (row, and a play count << 16) and the subtitle window (KhdSubtitles.exe) shows the row's line.
// trace_files=1 (tests only) logs every open, move and read in the voice packs.
typedef HANDLE(WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef DWORD(WINAPI* SetFilePointer_t)(HANDLE, LONG, PLONG, DWORD);
typedef BOOL(WINAPI* ReadFile_t)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static CreateFileA_t realCreateFileA;
static SetFilePointer_t realSetFilePointer;
static ReadFile_t realReadFile;
static volatile LONG traceFilesLeft;
static bool Tracing() { return traceFilesLeft > 0 && InterlockedDecrement(&traceFilesLeft) >= 0; }
static HANDLE voicePakHandle[2];                                // [0] voice.pak, [1] 0voice.pak: the latest handle of each
static const char* const kVoicePak[2] = { "voice.pak", "0voice.pak" };
static int VoicePakOf(HANDLE h)
{
    for (int i = 0; i < 2; i++)
        if (h && h != INVALID_HANDLE_VALUE && voicePakHandle[i] == h) return i;
    return -1;
}

struct StoryVoice { int pak; DWORD at; int row; };
static StoryVoice storyVoices[256];
static int storyN;
static volatile LONG storyPlays;
static int storyLastRow = -1;
static DWORD storyLastTick;
static HWND storyWnd;

// story_subs.tsv: pak<TAB>first byte<TAB>pack size<TAB>... (# lines are notes; every other line is a row, numbered as
// the subtitle window numbers them). A pack whose size is not the one the table was made for is left out: its places
// would point into other voices.
static void LoadStoryVoices()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\story_subs.tsv", abDir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    static char buf[256 * 1024];
    DWORD got = 0;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    LONGLONG size[2] = { -1, -1 };
    for (int i = 0; i < 2; i++)
    {
        wchar_t pak[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA a;
        swprintf_s(pak, L"%s\\%hs", gameDir, kVoicePak[i]);
        if (GetFileAttributesExW(pak, GetFileExInfoStandard, &a)) size[i] = ((LONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    }
    int rows = 0, off = 0;
    char* ctx = NULL;
    for (char* line = strtok_s(buf, "\n", &ctx); line; line = strtok_s(NULL, "\n", &ctx))
    {
        if (line[0] == '#' || line[0] == '\r' || !line[0]) continue;
        int row = rows++;
        char pak[32] = "";
        unsigned long at = 0;
        long long bytes = 0;
        if (sscanf_s(line, "%31[^\t]\t%lu\t%lld", pak, (unsigned)sizeof pak, &at, &bytes) != 3) { off++; continue; }
        int which = _stricmp(pak, kVoicePak[0]) == 0 ? 0 : _stricmp(pak, kVoicePak[1]) == 0 ? 1 : -1;
        if (which < 0 || bytes != size[which] || storyN >= 256) { off++; continue; }
        storyVoices[storyN].pak = which;
        storyVoices[storyN].at = at;
        storyVoices[storyN].row = row;
        storyN++;
    }
    Log("story voices with subtitles: %d of %d rows of story_subs.tsv%s", storyN, rows,
        off ? " (the rest: a voice pack of another size - make the table again with tools\\make_story_subs.py)" : "");
}

static void StoryVoiceRead(int pak, DWORD at)
{
    for (int i = 0; i < storyN; i++)
    {
        if (storyVoices[i].pak != pak || storyVoices[i].at != at) continue;
        int row = storyVoices[i].row;
        DWORD now = GetTickCount();
        if (row == storyLastRow && now - storyLastTick < 1500) return;   // the same voice read from its start again at once
        storyLastRow = row;
        storyLastTick = now;
        if (!storyWnd || !IsWindow(storyWnd)) { storyWnd = NULL; EnumWindows(FindGameWnd, (LPARAM)&storyWnd); }
        LONG plays = InterlockedIncrement(&storyPlays);
        if (storyWnd) SetPropW(storyWnd, L"khd.story", (HANDLE)(UINT_PTR)(((DWORD)(plays & 0x7FFF) << 16) | (DWORD)(row + 1)));
        Log("story voice: row %d of story_subs.tsv plays (%s byte %lu)%s", row + 1, kVoicePak[pak], at, storyWnd ? "" : " - no game window");
        return;
    }
}

static HANDLE WINAPI HookCreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    HANDLE h = realCreateFileA(p, a, s, sa, c, f, t);
    const char* name = p ? strrchr(p, '\\') : NULL;
    name = name ? name + 1 : p;
    for (int i = 0; i < 2; i++)
    {
        if (name && _stricmp(name, kVoicePak[i]) == 0)
        {
            voicePakHandle[i] = h;
            if (Tracing()) Log("TRACE open %s -> %p", kVoicePak[i], h);
        }
        else if (voicePakHandle[i] == h) voicePakHandle[i] = NULL;   // the pack was closed and its handle value given to another file
    }
    return h;
}
static DWORD WINAPI HookSetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD how)
{
    DWORD r = realSetFilePointer(h, lo, hi, how);
    int i = VoicePakOf(h);
    if (i >= 0 && Tracing()) Log("TRACE seek %s %ld (method %lu) -> %lu", kVoicePak[i], lo, how, r);
    return r;
}
typedef BOOL(WINAPI* CloseHandle_t)(HANDLE);
static CloseHandle_t realCloseHandle;
static BOOL WINAPI HookCloseHandle(HANDLE h)            // trace_files (tests): a voice pack closed
{
    int i = VoicePakOf(h);
    BOOL r = realCloseHandle(h);
    if (i >= 0 && Tracing()) Log("TRACE close %s %p", kVoicePak[i], h);
    return r;
}
static BOOL WINAPI HookReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov)
{
    int i = VoicePakOf(h);
    DWORD at = 0;
    if (i >= 0) at = ov ? ov->Offset : SetFilePointer(h, 0, NULL, FILE_CURRENT);    // where this read starts
    BOOL r = realReadFile(h, buf, n, got, ov);
    if (i >= 0)
    {
        if (Tracing()) Log("TRACE read %s %lu bytes at %lu%s -> %s %lu", kVoicePak[i], n, at, ov ? " overlapped" : "",
                           r ? "ok" : "FAILED", got ? *got : 0);
        if (r) { StoryVoiceRead(i, at); VoiceGroupRead(i, at); }
    }
    return r;
}

// ---------------------------------------------------------------- per-character voice levels (10-18)
// user 10-18 「음성 설정 말인데 각 인물별로 간단한 아이콘을 넣고 음량을 자유자래로 설정하게 하는건 어떨까?」, 「캐릭터
// 음성에 안티 크로스나 마스터 테리온 나이아가 없어?」: fifteen groups - the game's twelve ([音声], its check boxes on the
// sound page) and three of ours inside its その他 - each with a level 0-100 in autobattle.ini [voice_volume].
//   * which voice is whose: autobattle\voice_groups.tsv (tools\make_voice_groups.py: every VOICE of the scripts by pack
//     and first byte, its group from the speaker); system.dll reads a voice from its first byte right before it makes
//     the voice's sound buffer (10-18 trace_sound=1: voice\da01\0001f.ogg read, a 1-s mono buffer made, volume 0 then
//     the voice volume -50, played);
//   * that buffer's volume gets the group's level added at every SetVolume: 100 = as the game sets it, 50 = -10 dB
//     (about half as loud), 0 = silent;
//   * the sound page shows the fifteen as face, name, slider and value (with the 마도서 look); the game's check boxes
//     stay, out of sight, and follow the levels (0 unticks one), since the game reads them when the window closes.
static const struct { const wchar_t* key; const wchar_t* ko; } kVoiceGroups[15] = {
    { L"九郎", L"쿠로" }, { L"アル", L"알" }, { L"九朔", L"쿠자쿠" }, { L"アナザーブラッド", L"어나더 블러드" },
    { L"アズラッド", L"아즈라드" }, { L"シュリュズベリイ", L"슈루즈베리" }, { L"ハヅキ", L"하즈키" }, { L"瑠璃", L"루리" },
    { L"ライカ", L"라이카" }, { L"ウェスト", L"닥터 웨스트" }, { L"エルザ", L"엘자" }, { L"その他", L"그 밖의 인물" },
    { L"マスターテリオン", L"마스터 테리온" }, { L"ナイア", L"나이아" }, { L"アンチクロス", L"안티 크로스" },
};
static const int kVoiceOrder[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 11 };   // on the page: 그 밖의 인물 last
static volatile LONG voiceLevel[15];
static HBITMAP voiceFace[15];
static int traceSound;                         // autobattle.ini trace_sound=1 (tests): the sound buffers are logged

static void LoadVoiceLevels(const wchar_t* ini)
{
    for (int g = 0; g < 15; g++) voiceLevel[g] = max(0, min(100, (int)GetPrivateProfileIntW(L"voice_volume", kVoiceGroups[g].key, 100, ini)));
}
static void SaveVoiceLevel(int g)
{
    wchar_t ini[MAX_PATH], v[16];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    swprintf_s(v, L"%ld", voiceLevel[g]);
    WritePrivateProfileStringW(L"voice_volume", kVoiceGroups[g].key, v, ini);
}

// added to what the game sets: 100 = 0, 50 = -10 dB, 25 = -20 dB, 0 = silent
static LONG VoiceAtten(int g)
{
    LONG lv = voiceLevel[g];
    if (lv >= 100) return 0;
    if (lv <= 0) return DSBVOLUME_MIN;
    return (LONG)(3333.0 * log10(lv / 100.0));
}

// voice_groups.tsv: pak<TAB>first byte<TAB>pack size<TAB>group<TAB>... sorted here by pack and byte
struct VoiceGroup { DWORD at; BYTE pak; BYTE group; };
static VoiceGroup* voiceGroups;
static int voiceGroupN;
static int CmpVoiceGroup(const void* a, const void* b)
{
    const VoiceGroup* x = (const VoiceGroup*)a; const VoiceGroup* y = (const VoiceGroup*)b;
    if (x->pak != y->pak) return x->pak - y->pak;
    return x->at < y->at ? -1 : x->at > y->at ? 1 : 0;
}
static void LoadVoiceGroups()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\voice_groups.tsv", abDir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!buf) { CloseHandle(f); return; }
    ReadFile(f, buf, size, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    LONGLONG pakSize[2] = { -1, -1 };
    for (int i = 0; i < 2; i++)
    {
        wchar_t pak[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA a;
        swprintf_s(pak, L"%s\\%hs", gameDir, kVoicePak[i]);
        if (GetFileAttributesExW(pak, GetFileExInfoStandard, &a)) pakSize[i] = ((LONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    }
    int cap = 0;
    for (DWORD i = 0; i < got; i++) if (buf[i] == '\n') cap++;
    voiceGroups = (VoiceGroup*)HeapAlloc(GetProcessHeap(), 0, (cap + 1) * sizeof(VoiceGroup));
    int off = 0;
    char* ctx = NULL;
    for (char* line = strtok_s(buf, "\n", &ctx); line && voiceGroups; line = strtok_s(NULL, "\n", &ctx))
    {
        if (line[0] == '#' || line[0] == '\r' || !line[0]) continue;
        char pak[32] = "";
        unsigned long at = 0;
        long long bytes = 0;
        int g = -1;
        if (sscanf_s(line, "%31[^\t]\t%lu\t%lld\t%d", pak, (unsigned)sizeof pak, &at, &bytes, &g) != 4 || g < 0 || g > 14) { off++; continue; }
        int which = _stricmp(pak, kVoicePak[0]) == 0 ? 0 : _stricmp(pak, kVoicePak[1]) == 0 ? 1 : -1;
        if (which < 0 || bytes != pakSize[which] || voiceGroupN > cap) { off++; continue; }
        voiceGroups[voiceGroupN].at = at;
        voiceGroups[voiceGroupN].pak = (BYTE)which;
        voiceGroups[voiceGroupN].group = (BYTE)g;
        voiceGroupN++;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (voiceGroupN) qsort(voiceGroups, voiceGroupN, sizeof(VoiceGroup), CmpVoiceGroup);
    Log("voice groups: %d voices of voice_groups.tsv%s", voiceGroupN, off ? " (some left out: a voice pack of another size)" : "");
}

// the voice just read from its start: its group waits for the sound buffer the same thread makes next
static volatile LONG pendingGroup = -1;
static DWORD pendingTick, pendingThread;
static void VoiceGroupRead(int pak, DWORD at)
{
    if (!voiceGroupN) return;
    VoiceGroup key = { at, (BYTE)pak, 0 };
    VoiceGroup* hit = (VoiceGroup*)bsearch(&key, voiceGroups, voiceGroupN, sizeof(VoiceGroup), CmpVoiceGroup);
    if (!hit) return;
    pendingTick = GetTickCount();
    pendingThread = GetCurrentThreadId();
    InterlockedExchange(&pendingGroup, hit->group);
}

// the voices' sound buffers (a freed buffer's address can come back for any new buffer, so every new one is looked at)
static CRITICAL_SECTION soundLock;
struct VoiceBuffer { void* buf; int group; };
static VoiceBuffer voiceBufs[32];
static void SoundBufferMade(void* buf)
{
    EnterCriticalSection(&soundLock);
    for (int i = 0; i < 32; i++) if (voiceBufs[i].buf == buf) voiceBufs[i].buf = NULL;
    LONG g = pendingGroup;
    if (g >= 0 && GetCurrentThreadId() == pendingThread && GetTickCount() - pendingTick < 2000)
    {
        InterlockedExchange(&pendingGroup, -1);
        int slot = 0;
        for (int i = 0; i < 32; i++) if (!voiceBufs[i].buf) { slot = i; break; }
        static int next;
        if (voiceBufs[slot].buf) slot = next++ & 31;            // all in use: the oldest-ish goes
        voiceBufs[slot].buf = buf;
        voiceBufs[slot].group = g;
        if (traceSound) Log("SOUND buffer %p is a voice of group %ld (level %ld)", buf, g, voiceLevel[g]);
    }
    LeaveCriticalSection(&soundLock);
}
static int VoiceGroupOfBuffer(void* buf)
{
    int g = -1;
    EnterCriticalSection(&soundLock);
    for (int i = 0; i < 32; i++) if (voiceBufs[i].buf == buf) { g = voiceBufs[i].group; break; }
    LeaveCriticalSection(&soundLock);
    return g;
}

// ---- the sound page
static void SetVoiceValue(HWND page, int g)
{
    wchar_t v[16];
    if (voiceLevel[g] <= 0) wcscpy_s(v, L"끔");
    else swprintf_s(v, L"%ld", voiceLevel[g]);
    SetWindowTextW(GetDlgItem(page, kVoiceCtlBase + g * 4 + 2), v);
}

// the game's boxes follow the levels: one of its own eleven is ticked while its level is above 0; その他 also while any
// of our three (inside it, for the game) is - those of its voices whose own group is at 0 are made silent here instead
static void SyncVoiceBoxes(HWND page)
{
    for (int g = 0; g < 12; g++)
    {
        HWND b = voiceBox[g] ? GetDlgItem(page, voiceBox[g]) : NULL;
        if (!b) continue;
        bool on = voiceLevel[g] > 0 || (g == 11 && (voiceLevel[12] > 0 || voiceLevel[13] > 0 || voiceLevel[14] > 0));
        SendMessageW(b, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static void VoiceLevelMoved(HWND page, int g)
{
    voiceLevel[g] = (LONG)SendDlgItemMessageW(page, kVoiceCtlBase + g * 4 + 3, TBM_GETPOS, 0, 0);
    SetVoiceValue(page, g);
    SaveVoiceLevel(g);
    SyncVoiceBoxes(page);
}

// the game's voice boxes under our sliders: kept, never shown again
// The game shows its boxes again when the page comes up; our labels let the mouse through (statics without SS_NOTIFY),
// so the box under them took the hover and repainted over our controls (10-18 user screenshot: 쿠로, 알 ... over the
// faces). The box stays where the game put it, enabled and ticked as it set it - it only draws nothing and lets the mouse
// through (moving and disabling it as well was more than needed; user 10-18 「문제가 생길거 같은데?」).
static LRESULT CALLBACK HiddenBoxProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINT:
    case WM_PRINTCLIENT:
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return SkinPrev(h, m, wp, lp);
}

static void AddVoiceLevels(HWND page)
{
    HWND grp = GetDlgItem(page, 0x41A);
    if (!skinBg || !grp || GetDlgItem(page, kVoiceCtlBase + 3)) return;
    // what the game's boxes say wins over our numbers when they disagree (another copy shares the game's settings)
    bool changed = false;
    for (int g = 0; g < 11; g++)
    {
        HWND b = voiceBox[g] ? GetDlgItem(page, voiceBox[g]) : NULL;
        if (!b) continue;
        bool on = SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (!on && voiceLevel[g] > 0) { voiceLevel[g] = 0; SaveVoiceLevel(g); changed = true; }
        if (on && voiceLevel[g] == 0) { voiceLevel[g] = 100; SaveVoiceLevel(g); changed = true; }
    }
    for (int id = 0x41B; id <= 0x429; id++)                  // out of sight for good: the game shows its boxes again
    {                                                          // when the page comes up (their labels peeped out, 10-18)
        HWND box = GetDlgItem(page, id);
        if (!box) continue;
        ShowWindow(box, SW_HIDE);
        SkinSubclass(box, HiddenBoxProc);
        SetWindowLongW(box, GWL_STYLE, GetWindowLongW(box, GWL_STYLE) & ~WS_TABSTOP);   // Tab goes to our sliders
    }
    RECT r;
    GetWindowRect(grp, &r);
    MapWindowPoints(NULL, page, (POINT*)&r, 2);
    HFONT font = (HFONT)SendMessageW(page, WM_GETFONT, 0, 0);
    int x0 = r.left + 10, top = r.top + 20, colW = (r.right - r.left - 20) / 3, rowH = (r.bottom - 4 - top) / 5;
    for (int k = 0; k < 15; k++)
    {
        int g = kVoiceOrder[k], cx = x0 + (k % 3) * colW, cy = top + (k / 3) * rowH;
        if (!voiceFace[g])
        {
            wchar_t name[16];
            swprintf_s(name, L"face_%02d.bmp", g);
            voiceFace[g] = SkinBmp(name);
        }
        HWND face = CreateWindowExW(0, L"Static", NULL, WS_CHILD | WS_VISIBLE | SS_BITMAP, cx, cy + (rowH - 30) / 2, 30, 30,
                                    page, (HMENU)(INT_PTR)(kVoiceCtlBase + g * 4), selfInst, NULL);
        if (face && voiceFace[g]) SendMessageW(face, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)voiceFace[g]);
        HWND name = CreateWindowExW(0, L"Static", kVoiceGroups[g].ko, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                    cx + 31, cy + 1, colW - 33, 16, page, (HMENU)(INT_PTR)(kVoiceCtlBase + g * 4 + 1), selfInst, NULL);
        HWND value = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX,
                                     cx + colW - 34, cy + 18, 30, 16, page, (HMENU)(INT_PTR)(kVoiceCtlBase + g * 4 + 2), selfInst, NULL);
        HWND bar = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                   cx + 31, cy + 17, colW - 31 - 36, 18, page, (HMENU)(INT_PTR)(kVoiceCtlBase + g * 4 + 3), selfInst, NULL);
        if (name) SendMessageW(name, WM_SETFONT, (WPARAM)font, FALSE);
        if (value) SendMessageW(value, WM_SETFONT, (WPARAM)font, FALSE);
        if (bar)
        {
            SendMessageW(bar, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
            SendMessageW(bar, TBM_SETLINESIZE, 0, 5);
            SendMessageW(bar, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(bar, TBM_SETPOS, TRUE, voiceLevel[g]);
        }
        SetVoiceValue(page, g);
    }
    SyncVoiceBoxes(page);
    Log("settings dialog: voice levels shown for 15 groups%s", changed ? " (some taken from the game's boxes)" : "");
}

// ---------------------------------------------------------------- story voices' sound (10-18)
// For per-character volume (user 10-18 「음성 설정 말인데 각 인물별로 간단한 아이콘을 넣고 음량을 자유자래로 설정하게
// 하는건 어떨까?」): system.dll makes its DirectSound with DirectSoundCreate8 (DSOUND ordinal 11); its buffers are
// followed - made, duplicated, volume set, played. trace_sound=1 (tests) logs them.
typedef HRESULT(WINAPI* DSCreate8_t)(LPCGUID, void**, LPUNKNOWN);
typedef HRESULT(STDMETHODCALLTYPE* CreateSoundBuffer_t)(void* self, LPCDSBUFFERDESC desc, void** buf, LPUNKNOWN outer);
typedef HRESULT(STDMETHODCALLTYPE* DuplicateSoundBuffer_t)(void* self, void* src, void** dup);
typedef HRESULT(STDMETHODCALLTYPE* SetVolume_t)(void* self, LONG vol);
typedef HRESULT(STDMETHODCALLTYPE* Play_t)(void* self, DWORD r1, DWORD prio, DWORD flags);
static DSCreate8_t realDSCreate8;
// 11-06 sound behind other windows (user, watching a test: 「아 현재는 백그라운드일때 실행은 되지만 소리는 안들리는데 소리도
// 들리게 해줘」): DirectSound keeps a buffer silent while its window is not the active one unless the buffer has
// DSBCAPS_GLOBALFOCUS. background_sound=1 (the default) gives that flag to every secondary buffer made through the
// patched IDirectSound8 table - system.dll's (story music, voices, effects) and the battle module's (CoCreateInstance of
// CLSID_DirectSound8: the same class, so the same table). 0 = as before.
static int bgSound = 1;
static volatile LONG bgSoundSaid;
static Patched csbPatched[4], dupPatched[4], volPatched[8], playPatched[8];
static volatile LONG csbN, dupN, volN, playN;
static volatile LONG traceSoundLeft = 3000;
static bool SoundTracing() { return traceSound && InterlockedDecrement(&traceSoundLeft) >= 0; }

static HRESULT STDMETHODCALLTYPE HookSetVolume(void* self, LONG vol)
{
    SetVolume_t orig = (SetVolume_t)FindOrig(volPatched, volN, *(void***)self);
    if (!orig) return DSERR_GENERIC;
    int g = VoiceGroupOfBuffer(self);
    LONG v = g >= 0 ? max((LONG)DSBVOLUME_MIN, vol + VoiceAtten(g)) : vol;
    if (SoundTracing()) Log("SOUND volume %p %ld%s", self, v, g >= 0 ? " (a voice, its level added)" : "");
    return orig(self, v);
}
typedef HRESULT(STDMETHODCALLTYPE* SetFrequency_t)(void* self, DWORD hz);
static Patched freqPatched[8];
static volatile LONG freqN;
static HRESULT STDMETHODCALLTYPE HookSetFrequency(void* self, DWORD hz)      // trace_sound (tests)
{
    SetFrequency_t orig = (SetFrequency_t)FindOrig(freqPatched, freqN, *(void***)self);
    if (!orig) return DSERR_GENERIC;
    HRESULT hr = orig(self, hz);
    if (SoundTracing()) Log("SOUND frequency %p %lu (%08lx)", self, hz, (unsigned long)hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookPlay(void* self, DWORD r1, DWORD prio, DWORD flags)
{
    Play_t orig = (Play_t)FindOrig(playPatched, playN, *(void***)self);
    if (!orig) return DSERR_GENERIC;
    HRESULT hr = orig(self, r1, prio, flags);
    if (SoundTracing()) Log("SOUND play %p flags %lu (%08lx)", self, flags, (unsigned long)hr);
    return hr;
}
// trace_sound (tests, 10-18 voices unheard from the second DUEL fight on): what is written into a buffer - its loudest
// 16-bit sample, so a buffer of silence shows as peak 0
typedef HRESULT(STDMETHODCALLTYPE* Unlock_t)(void* self, void* p1, DWORD n1, void* p2, DWORD n2);
static Patched unlockPatched[8];
static volatile LONG unlockN;
static int Peak16(const void* p, DWORD n)
{
    int peak = 0;
    const short* s = (const short*)p;
    for (DWORD i = 0; p && i < n / 2; i++) { int v = s[i] < 0 ? -s[i] : s[i]; if (v > peak) peak = v; }
    return peak;
}
static HRESULT STDMETHODCALLTYPE HookUnlock(void* self, void* p1, DWORD n1, void* p2, DWORD n2)
{
    Unlock_t orig = (Unlock_t)FindOrig(unlockPatched, unlockN, *(void***)self);
    if (!orig) return DSERR_GENERIC;
    if (n1 + n2 >= 4096 && SoundTracing())
        Log("SOUND data %p bytes %lu peak %d", self, n1 + n2, max(Peak16(p1, n1), Peak16(p2, n2)));
    return orig(self, p1, n1, p2, n2);
}
static void FollowBuffer(void* buf)
{
    void** vt = *(void***)buf;
    PatchSlot(vt, 15, (void*)HookSetVolume, volPatched, &volN, 8);    // IDirectSoundBuffer::SetVolume
    PatchSlot(vt, 12, (void*)HookPlay, playPatched, &playN, 8);       // IDirectSoundBuffer::Play
    if (traceSound) PatchSlot(vt, 19, (void*)HookUnlock, unlockPatched, &unlockN, 8);   // IDirectSoundBuffer::Unlock (tests)
    if (traceSound) PatchSlot(vt, 17, (void*)HookSetFrequency, freqPatched, &freqN, 8);   // ::SetFrequency (tests)
}
static HRESULT STDMETHODCALLTYPE HookCreateSoundBuffer(void* self, LPCDSBUFFERDESC d, void** buf, LPUNKNOWN outer)
{
    CreateSoundBuffer_t orig = (CreateSoundBuffer_t)FindOrig(csbPatched, csbN, *(void***)self);
    if (!orig) return DSERR_GENERIC;
    DSBUFFERDESC bg;
    if (bgSound && d && !(d->dwFlags & (DSBCAPS_PRIMARYBUFFER | DSBCAPS_GLOBALFOCUS)) && d->dwSize >= sizeof(DSBUFFERDESC1) && d->dwSize <= sizeof bg)
    {
        memset(&bg, 0, sizeof bg);
        memcpy(&bg, d, d->dwSize);
        bg.dwFlags = (bg.dwFlags & ~DSBCAPS_STICKYFOCUS) | DSBCAPS_GLOBALFOCUS;
        if (InterlockedIncrement(&bgSoundSaid) <= 6)
            Log("background sound: buffer flags %08lx -> %08lx on device %p", d->dwFlags, bg.dwFlags, self);
        d = &bg;
    }
    HRESULT hr = orig(self, d, buf, outer);
    if (SUCCEEDED(hr) && buf && *buf) { FollowBuffer(*buf); SoundBufferMade(*buf); }
    if (SoundTracing())
        Log("SOUND made %p on device %p flags %08lx bytes %lu rate %lu ch %u (%08lx)", buf ? *buf : NULL, self, d ? d->dwFlags : 0,
            d ? d->dwBufferBytes : 0, d && d->lpwfxFormat ? d->lpwfxFormat->nSamplesPerSec : 0,
            d && d->lpwfxFormat ? d->lpwfxFormat->nChannels : 0, (unsigned long)hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookDuplicateSoundBuffer(void* self, void* srcBuf, void** dup)
{
    DuplicateSoundBuffer_t orig = (DuplicateSoundBuffer_t)FindOrig(dupPatched, dupN, *(void***)self);
    if (!orig) return DSERR_GENERIC;
    HRESULT hr = orig(self, srcBuf, dup);
    if (SUCCEEDED(hr) && dup && *dup) FollowBuffer(*dup);
    if (SoundTracing()) Log("SOUND duplicated %p -> %p (%08lx)", srcBuf, dup ? *dup : NULL, (unsigned long)hr);
    return hr;
}
// trace_sound (tests): when a sound device goes away (10-18: voices unheard from the second DUEL fight on)
typedef ULONG(STDMETHODCALLTYPE* Release_t)(void* self);
static Patched relPatched[4];
static volatile LONG relN;
static ULONG STDMETHODCALLTYPE HookDSRelease(void* self)
{
    Release_t orig = (Release_t)FindOrig(relPatched, relN, *(void***)self);
    if (!orig) return 0;
    ULONG r = orig(self);
    if (r == 0) Log("SOUND device %p released", self);
    return r;
}
static HRESULT WINAPI HookDSCreate8(LPCGUID g, void** ds, LPUNKNOWN outer)
{
    HRESULT hr = realDSCreate8(g, ds, outer);
    if (SUCCEEDED(hr) && ds && *ds)
    {
        void** vt = *(void***)(*ds);
        PatchSlot(vt, 3, (void*)HookCreateSoundBuffer, csbPatched, &csbN, 4);      // IDirectSound8::CreateSoundBuffer
        PatchSlot(vt, 5, (void*)HookDuplicateSoundBuffer, dupPatched, &dupN, 4);   // IDirectSound8::DuplicateSoundBuffer
        if (traceSound) PatchSlot(vt, 2, (void*)HookDSRelease, relPatched, &relN, 4);   // IDirectSound8::Release (tests)
        Log("DirectSound8 made by system.dll: its buffers are followed (device %p)", *ds);
    }
    return hr;
}

// ---------------------------------------------------------------- enemy robots by hand: keys for B and M (DUEL, 10-18)
// user 10-18 「다른 적 기체도 같은 방식으로 조종 판을 만들고, 엑스트라에 대전 메뉴를 넣어줘」: the enemies' command lists
// (.waz) also use the letters B (shots and spells - command CSHOT) and M (turns, up and down - CSMOVE). For every fight
// system.dll loads demonbane.dll, gets a settings object from its GetIActionPartStart, fills it and calls RunActionPart
// with that object as the sixth argument (after hInstance, 0, the window, its style and system.dll's window procedure).
// The battle module asks the object for the key settings (vtable +0x34: count, then 16-byte rows device, key, mode,
// command) and from then on hands every character those rows, whatever kcf_file the scene names (demonbane.dll
// 0x100025D0: a kept list wins over the file). No row of the game's key settings is CSHOT or CSMOVE, so B and M could
// not be pressed (10-18 test: A and S of the scene's kcf never reached the fight). duel_keys=1 (the default) adds rows
// for what the settings leave free: keyboard A = CSHOT, S = CSMOVE, and the first free pad buttons from the sixth on.
// The robots of the story have no B or M in their lists, so in the other fights these keys do nothing.
// The object's class lives in demonbane.dll, which comes and goes with each fight: the slot is checked every time.
struct KeyRow { int device, key, mode, cmd; };
typedef void(__fastcall* GetKeys_t)(void* self, void* edx, int* count, KeyRow** rows);
static GetKeys_t origGetKeys;
static KeyRow keyRows[200];
static FARPROC realRunActionPart;
typedef FARPROC(WINAPI* GetProcAddress_t)(HMODULE, LPCSTR);
static GetProcAddress_t realGetProcAddress;

// DUEL: 058..249 enemy robots, 250.. heroes, 478..897 their AI-vs-AI copies, 1058..1897 the balance-mode copies (10-20)
#include "hard_v11_routes.h"
#include "tiberius_mix_routes.h"
static bool IsDuelScene(LONG scene)
{
    if (scene==9401 || scene==9421 || scene==9501 || scene==9531 || scene==9532 || scene==9781 || scene==9782 || scene==9301 || scene==9302 || scene==9303 || scene==9304 || scene==9083 || scene==9109 || (scene>=7300 && scene<=7355) || (scene >= 58 && scene <= 1999) || (scene >= 5923 && scene <= 5926) || scene==5910 || (scene>=5940 && scene<=5942)) return true;
    for(unsigned i=0;i<_countof(hvRoutes);++i) if(hvRoutes[i].dest==scene)return true;
    for(unsigned i=0;i<_countof(imMixRoutes);++i) if(imMixRoutes[i].dest==scene)return true;
    for(int i=0;i<aiRouteCount;++i) if(aiRoutes[i].destination==scene)return true;
    return false;
}

typedef int(__fastcall* DifficultyGet_t)(void*, void*);
static DifficultyGet_t origDifficultyGet;
static int __fastcall HookDifficultyGet(void* self, void* edx)
{
    const BYTE* obj = (const BYTE*)self;
    if (Readable(obj + 0x18, sizeof(LONG)) && IsDuelScene(*(const LONG*)(obj + 0x18)))
        return 1; // original Normal combat values; the DUEL difficulty picks the balance damage column (BalanceRate) and hard's HV11
    return origDifficultyGet ? origDifficultyGet(self, edx) : 1;
}

static bool InImage(const void* p, HMODULE m)
{
    if (!m || !p) return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)m + ((IMAGE_DOS_HEADER*)m)->e_lfanew);
    return (const BYTE*)p >= (BYTE*)m && (const BYTE*)p < (BYTE*)m + nt->OptionalHeader.SizeOfImage;
}

static void __fastcall HookGetKeys(void* self, void* edx, int* count, KeyRow** rows)
{
    if (origGetKeys) origGetKeys(self, edx, count, rows);
    if (!count || !rows || !*rows || *count < 0 || *count > 190) return;
    // DUEL only (user 10-18 「본게임에 영향을 안 끼치게 이것들은 듀얼 전용으로만 고쳐줘」): the battle module has just read
    // the scene number from the same object (vtable +0x20, kept at demonbane.dll+0x11E494 - scene095 read 95 in a DUEL
    // fight); the DUEL scenes are 058..249 (stage 59..250, the enemy robots) and 250..345 (the select screen's hero
    // robots, dmpack504), the story and FREE FIGHT ones below 58 (FINISHERS stages 032..057)
    HMODULE bm = GetModuleHandleW(battleModule);
    LONG scene = -1;
    if (bm && InImage((BYTE*)bm + 0x11E494, bm)) scene = *(volatile LONG*)((BYTE*)bm + 0x11E494);
    if (!IsDuelScene(scene))
    {
        Log("fight keys: scene %ld is not a DUEL scene - the game's keys as they are", scene);
        return;
    }
    int n = *count;
    memcpy(keyRows, *rows, n * sizeof(KeyRow));
    char added[160] = "";
    const int cmds[2] = { 0x22, 0x24 };                  // CSHOT (B), CSMOVE (M)
    const int boardKeys[2] = { 0x1E, 0x1F };             // DIK_A, DIK_S
    const char* names[2] = { "B (CSHOT)", "M (CSMOVE)" };
    for (int c = 0; c < 2; c++)
    {
        for (int dev = 0; dev <= 2; dev += 2)            // KEYBOARD1, JOYSTICK
        {
            bool have = false;
            for (int i = 0; i < n; i++) if (keyRows[i].device == dev && keyRows[i].cmd == cmds[c]) have = true;
            if (have) continue;
            for (int key = dev ? 9 : boardKeys[c]; key <= (dev ? 13 : boardKeys[c]); key++)   // pad: JOY_BTN5..JOY_BTN9
            {
                bool used = false;
                for (int i = 0; i < n; i++) if (keyRows[i].device == dev && keyRows[i].key == key) used = true;
                if (used) continue;
                KeyRow r = { dev, key, 3, cmds[c] };     // mode 3 = DIRECT
                keyRows[n++] = r;
                char one[48];
                sprintf_s(one, dev ? " pad button %d = %s;" : " key %02X = %s;", dev ? key - 3 : key, names[c]);
                strcat_s(added, one);
                break;
            }
        }
    }
    Log("fight keys: %d rows from the game's settings, added:%s", *count, added[0] ? added : " none (all in use)");
    *count = n;
    *rows = keyRows;
}

// The fight's pause menu (ESC) has three rows, each a button: FREE FIGHT MENU (id 0 - leaves the fight), COMMAND LIST
// (id 2) and EXIT (id 3 - only closes the menu), with the cursor on EXIT (CPauseMenu 0x1002E8E0: the chosen row's id at
// entry +0x14 decides; 0 or 1 sets the result 2 and the end flag demonbane+0x26F30C). User 10-18 「듀얼 모드에선 esc로
// 나갈수 없는거 같아」: ESC then EXIT went back into the fight. In a DUEL fight (only - user 「듀얼 전용으로만」) EXIT's id
// becomes 0, so it leaves the fight like the top row; ESC (the menu's cancel) still closes the menu. duel_menu_exit=0: off.
static void DuelMenuExit()
{
    HMODULE bm = GetModuleHandleW(battleModule);
    if (!bm || !InImage((BYTE*)bm + 0x26F30C, bm)) return;
    BYTE* b = (BYTE*)bm;
    __try
    {
        LONG scene = *(volatile LONG*)(b + 0x11E494);
        if (!IsDuelScene(scene)) return;
        BYTE* app = *(BYTE**)(b + 0x11E3B0);
        BYTE* menu = app ? *(BYTE**)(app + 0x78) : NULL;
        if (!menu || *(LONG*)(menu + 0x144) != 1) return;         // 1 = open, choosing
        BYTE* lo = *(BYTE**)(menu + 0x1E0);
        BYTE* hi = *(BYTE**)(menu + 0x1E4);
        if (!lo || hi <= lo || hi - lo > 8 * 40) return;
        for (BYTE* e = lo; e + 40 <= hi; e += 40)
            if (*(LONG*)e == 1 && *(LONG*)(e + 0x14) == 3)
            {
                *(LONG*)(e + 0x14) = 0;
                Log("DUEL pause menu: EXIT leaves the fight (scene %ld)", scene);
            }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------- DUEL auto battle: AI vs AI (10-19)
// user 10-19 「자동 전투가 안되는거 개선 가능해?」 → 「AI 대 AI 자동 전투 시험부터 하고 …」: the agent only knows the 16 pairs of
// the story fights, so in DUEL the game's own AI flies our robot too. dmpack505_duelauto.dat holds a copy of each DUEL scene
// with PLAYER1 attribute(NONPLAYER) + AI(<its AI>) (10-19 test: Lord Byakhee flew and shot by itself and beat D Blood; with
// attribute(PLAYER) the AI barely moved). With 자동 전투 on (autobattle.ini agent, which the settings page writes at once)
// the scene number in the settings object (+0x18 - vtable +0x20 is `mov eax,[ecx+0x18]`, kept at demonbane+0x11E494) is
// turned into its copy's from autobattle\duel_auto.tsv right before RunActionPart reads it. duel_auto=0: off.
static bool duelAuto;
static short duelAutoMap[1000];
static signed char hoAiOf[1000];               // duel_handover.h: manual DUEL scene -> its auto copy's AI type + 1
static void HoNoteAi(int scene, const char* name);
static void HoArm(LONG scene);

static bool LoadDuelAuto()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\duel_auto.tsv", abDir);
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"rb") || !f) { Log("DUEL auto battle: no duel_auto.tsv - off"); return false; }
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f))
    {
        int a = -1, b = -1;
        char pilot[16] = "", ai[32] = "";
        if (line[0] == '#' || sscanf_s(line, "%d\t%d\t%15s\t%31s", &a, &b, pilot, (unsigned)sizeof pilot, ai, (unsigned)sizeof ai) < 2) continue;
        if (a >= 0 && a < 1000 && b > 0 && b < 1000) { duelAutoMap[a] = (short)b; n++; if (ai[0]) HoNoteAi(a, ai); }
    }
    fclose(f);
    Log("DUEL auto battle: %d scenes have an AI-vs-AI copy (used while 자동 전투 is on)", n);
    return n > 0;
}

// 11-05 the one DUEL fight the story agent plays: Ambrosius (PLAYER1) vs Lord Byakhee, manual scene 303 (BATTLE-06's stage)
static bool DuelAgentWanted(LONG scene)
{
    if (scene != 303) return false;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    return GetPrivateProfileIntW(L"autobattle", L"hm_agent_mm", 1, ini) != 0;
}

static void DuelAutoScene(BYTE* obj)
{
    HoArm(*(LONG*)(obj + 0x18));                       // DUEL hand-over (duel_handover.h): this fight's PLAYER1
    if (!duelAuto) return;
    LONG scene = *(LONG*)(obj + 0x18);
    if (scene < 0 || scene >= 1000 || !duelAutoMap[scene]) return;
    if (!duelAutoOn) return;                            // DUEL 자동 전투 off: the person fights (11-03: not the story's agent=)
    if (DuelAgentWanted(scene))                         // 11-05 Ambrosius vs Lord Byakhee: the story agent plays PLAYER1
    {
        duelAgentScene = duelAgentFight = true;
        Log("DUEL agent battle: scene %ld kept (no AI-vs-AI copy) - the story agent's BATTLE-06 AI plays PLAYER1, Ambrosius vs Lord Byakhee (hm_agent_mm=0: off)", scene);
        return;
    }
    *(LONG*)(obj + 0x18) = duelAutoMap[scene];
    duelAutoFight = 1;                                  // keys typed into other windows stay out of it (HookGetDeviceState)
    duelAutoBlankLogged = 0;
    Log("DUEL auto battle: scene %ld -> %d (자동 전투 is on: both robots on the game's AI)", scene, duelAutoMap[scene]);
}

// ---------------------------------------------------------------- DUEL balance mode (10-19)
// user 10-19 「일부 전투들은 그냥 일방적으로 끝나는데 균형 모드를 별개로 만들어 손봐줘」·「스테이지간에 수치가 제멋대로인가보네
// 어느 판에선 어이없을 정도로 쉽게 죽어」: a DUEL scene is a copy of the opponent's story fight, so that fight's difficulty
// factors came along (scene game_rank_attack / game_rank_defence: on Normal the player's damage from 50% (BATTLE-01) to
// 300% (BATTLE-02), the enemy's 100-150%, on 어려움 mostly 300%) and so did the hit points (810..2000). In demonbane.dll
// the scene keeps ten ints of each (+0x2A4, +0x2CC); 0x39880 / 0x398A0 hand out the one of the difficulty (+0x11E498);
// a robot's damage taken is base x (1 - guard) x 0x33350(the robot) - PLAYER robots get game_rank_defence, NONPLAYER ones
// game_rank_attack, anything else 1 - and the direct hits of 0x3681B and add_param(#100, -n) go the same way through
// 0x33300(the attacker) and 0x33350. In auto battle both robots are NONPLAYER, so those numbers cannot tell them apart;
// their group() can (+0x724: PLAYER1 0, PLAYER2 1 - the AI-vs-AI copies keep it).
// duel_balance=1 (or the DUEL 균형 모드 check box): for a scene listed in autobattle\duel_balance.tsv (scene, then the
// damage PLAYER1 takes and the damage PLAYER2 takes in percent for 쉬움, 보통, 어려움 - the builder evens out the stage
// factors and the hit points and adds per-robot corrections) both functions jump to ours for that fight; in any other
// fight their first bytes are put back, so they run the game's own code.
struct BalRow { short taken[2][3]; };
static BalRow balRows[1000];
static bool balHave[1000];
static int balCount;
static volatile LONG balScene = -1;          // the scene of this fight while balance is in force, else -1
static BYTE* balModule;                      // demonbane.dll's base for this fight
typedef int(__fastcall* RankGet_t)(void* mgr, void* edx);
static const DWORD kRateRva[2] = { 0x33350, 0x33300 };   // the damage-taken rate of a robot, of its target (attacker)

static void LoadDuelBalance()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\duel_balance.tsv", abDir);
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"rb") || !f) { Log("DUEL balance: no duel_balance.tsv - balance mode cannot be used"); return; }
    char line[256];
    while (fgets(line, sizeof line, f))
    {
        int s = -1, v[6];
        if (line[0] == '#' || sscanf_s(line, "%d\t%d\t%d\t%d\t%d\t%d\t%d", &s, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 7) continue;
        if (s < 0 || s >= 1000) continue;
        for (int k = 0; k < 6; k++) balRows[s].taken[k / 3][k % 3] = (short)(v[k] < 1 ? 1 : v[k] > 2000 ? 2000 : v[k]);
        if (!balHave[s]) { balHave[s] = true; balCount++; }
    }
    fclose(f);
    Log("DUEL balance: %d scenes have balance numbers (used while DUEL 균형 모드 is on; now %s)", balCount,
        duelBalance ? "on" : "off");
}

// what demonbane.dll's own 0x33350 (victim) / 0x33300 (attacker) give
static float GameRate(BYTE* self, bool attacker)
{
    BYTE* bm = balModule;
    void* mgr = *(void**)(bm + 0x11E3B0);
    RankGet_t rankAttack = (RankGet_t)(void*)(bm + 0x39880), rankDefence = (RankGet_t)(void*)(bm + 0x398A0);
    LONG attr = *(LONG*)(self + 0x720);
    if (attr & 1) return (float)(attacker ? rankAttack(mgr, NULL) : rankDefence(mgr, NULL)) * 0.01f;
    if (attr & 2) return (float)(attacker ? rankDefence(mgr, NULL) : rankAttack(mgr, NULL)) * 0.01f;
    return 1.0f;
}

static float BalanceRate(BYTE* self, bool attacker)
{
    LONG s = balScene;
    if (s >= 0 && s < 1000)
    {
        LONG g = *(LONG*)(self + 0x724);
        if (g == 0 || g == 1)
        {
            int victim = attacker ? 1 - (int)g : (int)g;
            // 11-08 user 「AI의 선택을 같게 해달라는거였는데 배율에 적용이 되어버린거구나」: the DUEL easy / normal / hard picks the
            // balance table's damage column (the AI choice stays the same in all three). Before, the column came from the battle's
            // rank, which HookDifficultyGet pins to Normal in DUEL - so all three were Normal's column.
            LONG rank = duelDifficulty;
            if (rank < 0) rank = 0;
            if (rank > 2) rank = 2;
            return balRows[s].taken[victim][rank] * 0.01f;
        }
    }
    return GameRate(self, attacker);
}

// both replace thiscall functions without stack arguments that leave a float in st(0)
static float __fastcall BalVictimRate(BYTE* self, void*) { return BalanceRate(self, false); }
static float __fastcall BalAttackerRate(BYTE* self, void*) { return BalanceRate(self, true); }

static bool WriteCode(BYTE* at, const BYTE* bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

// on: both functions jump to ours (only over the expected code); off: our jumps are taken out again
static bool SetRatePatch(BYTE* bm, bool on)
{
    void* const fn[2] = { (void*)BalVictimRate, (void*)BalAttackerRate };
    static const BYTE head[9] = { 0x51, 0x8B, 0x81, 0x20, 0x07, 0x00, 0x00, 0xD9, 0x05 };   // push ecx; mov eax,[ecx+720h]; fld [1.0]
    bool ok = true;
    for (int i = 0; i < 2; i++)
    {
        BYTE* at = bm + kRateRva[i];
        bool ours = at[0] == 0xE9 && at + 5 + *(LONG*)(at + 1) == (BYTE*)fn[i];
        if (on && !ours)
        {
            if (memcmp(at, head, 9) != 0 || *(DWORD*)(at + 9) != (DWORD)(UINT_PTR)(bm + 0xE5404))
            {
                Log("DUEL balance: demonbane.dll+0x%lX is not the expected code - balance not used", kRateRva[i]);
                ok = false;
                break;
            }
            BYTE jmp[5] = { 0xE9, 0, 0, 0, 0 };
            *(LONG*)(jmp + 1) = (LONG)((BYTE*)fn[i] - (at + 5));
            if (!WriteCode(at, jmp, 5)) { Log("DUEL balance: could not write demonbane.dll+0x%lX (error %lu)", kRateRva[i], GetLastError()); ok = false; break; }
        }
        else if (!on && ours) WriteCode(at, head, 5);
    }
    if (on && !ok) SetRatePatch(bm, false);
    return ok;
}

static void DuelBalanceFight(BYTE* obj, HMODULE bm)
{
    LONG scene = *(LONG*)(obj + 0x18);
    bool want = duelBalance && balCount && scene >= 0 && scene < 1000 && balHave[scene];
    balModule = (BYTE*)bm;
    if (!want || !SetRatePatch((BYTE*)bm, true))
    {
        balScene = -1;
        SetRatePatch((BYTE*)bm, false);
        return;
    }
    balScene = scene;
    const BalRow& r = balRows[scene];
    Log("DUEL balance: scene %ld - damage PLAYER1 takes %d/%d/%d%%, PLAYER2 %d/%d/%d%% (쉬움/보통/어려움; DUEL difficulty %ld picks the column)",
        scene, r.taken[0][0], r.taken[0][1], r.taken[0][2], r.taken[1][0], r.taken[1][1], r.taken[1][2], duelDifficulty);
}

// ---------------------------------------------------------------- DUEL balance mode: the hit fixes (10-20)
// user 10-20 「판정 고침도 균형 모드에서만 적용되게 해 줘」「보통 모드는 그냥 지면 지는대로」: the two Clockwork Phantoms'
// armor, the first Phantom on the ground (BATTLE-15) and Cyclanoth's aim point live only in copies of the scenes
// (dmpack506_duelfix.dat, scene + 1000 - build_duel_fixed.py). While balance is in force for a fight (DuelBalanceFight found
// its row), its scene number becomes the copy's from autobattle\duel_balance_scenes.tsv; the damage rows stay those of the
// scene before the swap (balScene). demonbane.dll formats the path with %03d and checks no range (0x10028F20).
static short balCopy[1000];
static int balCopyCount;

static void LoadDuelBalanceScenes()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\duel_balance_scenes.tsv", abDir);
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"rb") || !f) { Log("DUEL balance: no duel_balance_scenes.tsv - the hit fixes are not used"); return; }
    char line[256];
    while (fgets(line, sizeof line, f))
    {
        int a = -1, b = -1;
        if (line[0] == '#' || sscanf_s(line, "%d\t%d", &a, &b) != 2) continue;
        if (a >= 58 && a < 1000 && b >= 1000 && b < 2000) { balCopy[a] = (short)b; balCopyCount++; }
    }
    fclose(f);
    Log("DUEL balance: %d scenes have a copy with the hit fixes (used while DUEL 균형 모드 is on)", balCopyCount);
}

static void DuelBalanceCopy(BYTE* obj)
{
    LONG s = balScene;
    if (s < 0 || s >= 1000 || !balCopy[s]) return;
    *(LONG*)(obj + 0x18) = balCopy[s];
    Log("DUEL balance: scene %ld -> %d (the copy with the hit fixes)", s, balCopy[s]);
}

#include "mm_other_scope.h"
static void ApplyMmOtherPairScene(BYTE* obj)
{
    int loaded=*(int*)(obj+0x18);
    int dest=MmOtherDestination(duelBalance!=0,duelBalance?(int)balScene:loaded,loaded);   // 11-05 18:5x: balance off too
    if(dest<0)return;
    wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack509_mm_stall.dat",gameDir);
    if(GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES)return;
    *(int*)(obj+0x18)=dest;
    Log("MM_OTHER_C1 pair route %d -> %d (balance %s); original damage and difficulty retained",loaded,dest,duelBalance?"on":"off");
}
#include "mm_qm_scope.h"
#include "mm_qm_anchor_fix.h"
static void ApplyMmQmPairScene(BYTE* obj,HMODULE module)
{
    const int loaded=*(int*)(obj+0x18);
    mmQmRoutedTo=-1;
    int destination=MmQmDestination(duelBalance!=0,duelBalance?(int)balScene:loaded,loaded);   // 11-05 18:5x: balance off too
    if (destination>=0) {
        wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack508_mm_qm_c12.dat",gameDir);
        if (GetFileAttributesW(pack)!=INVALID_FILE_ATTRIBUTES) {
            *(int*)(obj+0x18)=destination;
            mmQmRoutedTo=destination;
            Log("MM_QM_C12 pair route %d -> %d (balance %s); native difficulty and damage rows preserved",loaded,destination,
                duelBalance?"on":"off");
        }
    }
    // Always reset the hook scope at the next battle, including other opponents/story/balance-off.
    InstallMmAimFix(obj,module);
}

#include "nero_ground.h"
#include "nero_ground_scope.h"
static void ApplyNeroGroundScene(BYTE* obj,HMODULE module)
{
    const int loaded=*(int*)(obj+0x18);
    const int destination=NeroGroundDestination(duelBalance!=0,balScene,loaded);
    if(destination<0)return;
    wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack510_nero_ground.dat",gameDir);
    if(GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES)return;
    InstallNeroGroundAim(module);
    *(int*)(obj+0x18)=destination;
    Log("NERO_GROUND balanced source %ld -> %d; existing AI and damage rows retained",balScene,destination);
}

#include "duel_ai_routes.h"

#include "aion_fingerprint.h"
#include "aion_native.h"
#include "kraken_observe.h"
#include "native_command_compat.h"
#include "native_command_adapter.h"
#include "nero_player_camera.h"
#include "kokuin_camera.h"           // 11-05 KOKUIN666: camera to its target
#include "duel_hud_names.h"
#include "hm_boss_ai.h"
#include "am_ground_ai.h"
#include "a2_air_ai.h"
#include "em_owned_ai.h"
#include "f2_owned_ai.h"
#include "hero_ai_slots.h"
#include "im_onryou_vs_mm.h"
#include "hard_v11.h"
#include "mm_tackle_gate.h"
#include "hm_duel_ai.h"
#include "tiberius_mix.h"
#include "pause_menu.h"
#include "continue_wait.h"
#include "battle_autosave.h"         // 11-06 the battle save prompt's YES saves at once
#include "duel_handover.h"

// frame = RunActionPart's stack at its entry: [0] return address, [1].. the arguments. demonbane.dll reads the settings
// object from the sixth (0x10001FD1: [esp+0x4C] after six pushes and the stdcall ImmAssociateContext)
static void __cdecl BeforeFight(void** frame)
{
    HMODULE bm = GetModuleHandleW(battleModule);
    static int logged;
    char why[96] = "unreadable";
    __try
    {
        void** vt = frame[6] ? *(void***)frame[6] : NULL;
        if (vt && InImage(vt, bm) && (vt[13] == (void*)HookGetKeys || InImage(vt[13], bm)))
        {
            if (vt[13] != (void*)HookGetKeys)              // a fresh load of the battle module: wrap the call again
            {
                DWORD old;
                if (!VirtualProtect(&vt[13], sizeof(void*), PAGE_READWRITE, &old))
                {
                    if (logged++ < 3) Log("fight start: VirtualProtect failed on the settings object (error %lu)", GetLastError());
                    return;
                }
                origGetKeys = (GetKeys_t)vt[13];
                InterlockedExchangePointer(&vt[13], (void*)HookGetKeys);
                VirtualProtect(&vt[13], sizeof(void*), old, &old);
            }
            // Native settings getter +0x28 feeds demonbane.dll+0x11E498. Hook only this getter,
            // never the story variable/registry. Original code: 0x1104 call [edx+28], 0x1107 mov [rank],eax.
            if (vt[10] != (void*)HookDifficultyGet)
            {
                if (!InImage(vt[10], bm)) { Log("DUEL difficulty getter identity mismatch"); return; }
                DWORD old;
                if (!VirtualProtect(&vt[10], sizeof(void*), PAGE_READWRITE, &old))
                { Log("DUEL difficulty getter protection failed"); return; }
                origDifficultyGet = (DifficultyGet_t)vt[10];
                InterlockedExchangePointer(&vt[10], (void*)HookDifficultyGet);
                VirtualProtect(&vt[10], sizeof(void*), old, &old);
            }
            if (*(LONG*)((BYTE*)frame[6]+0x18)==imMixDestination && imMixDestination>=30000)
                *(LONG*)((BYTE*)frame[6]+0x18)=imMixInput;
            if (*(LONG*)((BYTE*)frame[6]+0x18)==hvDestination && hvDestination>=20000)
                *(LONG*)((BYTE*)frame[6]+0x18)=hvInput; // retry the original request through every v10 route
            LONG sourceScene = *(LONG*)((BYTE*)frame[6] + 0x18);
            fightKind = IsDuelScene(sourceScene) ? 2 : (SystemMenuMask() & 0x20000000UL) ? 1 : 0;
            // the agent finds a fight only at its start (11-03 08:48: started mid-fight from the pause menu it never took
            // it): it runs from the first story / FREE FIGHT fight on, and its keys count only while that mode is on
            if (fightKind != 2) EnsureAgent();
            Log("fight kind: %s (scene %ld, menu mask %#lx) - 자동 전투 %s", fightKind == 2 ? "DUEL" : fightKind == 1 ? "FREE FIGHT" : "story",
                sourceScene, SystemMenuMask(), (fightKind == 2 ? duelAutoOn : fightKind == 1 ? freeAuto : autoBattle) ? "on" : "off");
            Log("fight difficulty scope: scene=%ld duel=%d combat_rank=%d duel_AI=%ld story_rank=%d",
                sourceScene, IsDuelScene(sourceScene), HookDifficultyGet(frame[6], NULL), duelDifficulty, Difficulty());
            duelAutoFight = 0;                          // DuelAutoScene sets it for an AI-vs-AI copy
            duelAgentScene = duelAgentFight = false;    // 11-05 ... or keeps the person's scene for the story agent
            DuelAutoScene((BYTE*)frame[6]);
            if (duelAgentFight) EnsureAgent();
            DuelBalanceFight((BYTE*)frame[6], bm);      // after the auto swap: the AI-vs-AI copies have their own rows
            DuelBalanceCopy((BYTE*)frame[6]);           // 10-20 then the copy with the hit fixes (balance mode only)
            ApplyDuelAiProfile((BYTE*)frame[6]); // optional validated tier pack only
            RestoreHandoverSlots(bm);           // 11-03 first: the AI slots go back before the others check them
            RestoreHmDuelSlots(bm);             // 11-05 Ambrosius's DUEL AI: right after the hand-over, before the others check
            RestoreHardV11(bm);
            RestoreHeroAiSlots(bm);
            ResetNativeCommandHooks(bm);
            ResetMmTackleGate(bm);              // 11-05 before the installers check the action-5 call
            ApplyMmOtherPairScene((BYTE*)frame[6]);
            ApplyMmQmPairScene((BYTE*)frame[6],bm);
            ApplyNeroGroundScene((BYTE*)frame[6],bm);
            ApplyAionScene((BYTE*)frame[6],bm);
            ApplyKrakenObserve((BYTE*)frame[6],bm);
            ApplyNativeCommand((BYTE*)frame[6],bm);
            ApplyMmTackleGate(bm, fightKind == 2);   // 11-05 Lord Byakhee's tackle only in range (DUEL)
            if (neroSideCamera) InstallNeroPlayerCamera(bm);
            InstallKokuinCamera(bm);              // 11-05 user: KOKUIN666 shows its target being crushed
            InstallDuelHudNames(bm,IsDuelScene(sourceScene));
            if (fightKind == 2 && (HdWanted() || !HmPilotWanted())) hmEnabled = false;   // 11-05 DUEL: Ambrosius on the LOAD AI, no 10-26 pilot
            else ApplyHmAi((BYTE*)frame[6],bm);
            ApplyAmAi((BYTE*)frame[6],bm);
            ApplyA2Ai((BYTE*)frame[6],bm);
            ApplyEmOwnedAi((BYTE*)frame[6],bm);
            ApplyF2Ai((BYTE*)frame[6],bm);
            ApplyHardV11((BYTE*)frame[6],bm,sourceScene);
            ApplyImMix((BYTE*)frame[6],bm,sourceScene);
            InstallHmDuelAi(bm, fightKind == 2);   // 11-05 inside the hand-over, outside hard_v11 and the hero AIs
            InstallPauseMenu(bm);
            InstallHandover(bm);
            InstallImOnryou(bm, fightKind == 2);   // 11-03 after the hand-over: Beelzebub vs Lord Byakhee asks ONRYOU
            InstallStoryOnryouFast(bm, fightKind != 2 && hardMode && Difficulty() == 2);   // v31 story 어려움: 원령 주탄 charge half
            return;
        }
        sprintf_s(why, "vtable %p, +0x34 %p", (void*)vt, vt ? vt[13] : NULL);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (logged++ < 3)
        Log("fight start: the sixth argument %p is not the battle module's settings object (%s) - B and M keys not added",
            frame[6], why);
}

__declspec(naked) static void NakedRunActionPart()
{
    __asm {
        push esp                        // (the value before the push) RunActionPart's frame: its return address, then its arguments
        call BeforeFight
        add esp, 4
        jmp dword ptr [realRunActionPart]
    }
}

static FARPROC WINAPI HookGetProcAddress(HMODULE m, LPCSTR name)
{
    FARPROC f = realGetProcAddress(m, name);
    if (f && HIWORD((UINT_PTR)name) && strcmp(name, "RunActionPart") == 0)
    {
        realRunActionPart = f;
        return (FARPROC)(void*)NakedRunActionPart;
    }
    return f;
}

// from DllMain: system.dll loads us as one of its imports and reads its settings right after, so its import slots are
// bound already and nothing of it has run yet
static void HookSystemDll()
{
    HMODULE sys = GetModuleHandleW(L"system.dll");
    if (!sys) { Log("system.dll not loaded yet: own settings and the Hard button are off this time"); return; }
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    InstallContinueWait(sys,ini);
    InstallBattleAutosave(sys, ini);
    ownSettings = GetPrivateProfileIntW(L"autobattle", L"own_settings", 1, ini);
    hardMode = GetPrivateProfileIntW(L"autobattle", L"hard", 1, ini);
    skinOn = GetPrivateProfileIntW(L"autobattle", L"settings_skin", 1, ini);
    if (ownSettings)
        ownSettings = HookImport(sys, "ADVAPI32.dll", "RegOpenKeyExA", (void*)HookRegOpenKeyExA, (void**)&realRegOpenKeyExA)
                      && HookImport(sys, "ADVAPI32.dll", "RegCreateKeyExA", (void*)HookRegCreateKeyExA, (void**)&realRegCreateKeyExA);
    // the settings pages always (자동 전투, 화면 크기, the voice names); the Hard pack swap with hard=1
    bool dlg = HookImport(sys, "USER32.dll", "CreateDialogParamA", (void*)HookCreateDialogParamA, (void**)&realCreateDialogParamA);
    if (skinOn) HookImport(sys, "USER32.dll", "DialogBoxParamA", (void*)HookDialogBoxParamA, (void**)&realDialogBoxParamA);
    HookImport(sys, "USER32.dll", "SetDlgItemTextA", (void*)HookSetDlgItemTextA, (void**)&realSetDlgItemTextA);
    HookImport(sys, "USER32.dll", "SetWindowTextA", (void*)HookSetWindowTextA, (void**)&realSetWindowTextA);
    HookImport(sys, "USER32.dll", "MessageBoxA", (void*)HookMessageBoxA, (void**)&realMessageBoxA);   // v29 Korean boxes
    UaInstall(sys, ini);                                       // v30 모든 콘텐츠 열기
    {
        static void* unusedExe;
        HookImport(GetModuleHandleW(NULL), "USER32.dll", "MessageBoxA", (void*)HookMessageBoxA, &unusedExe);
    }
    escBack = GetPrivateProfileIntW(L"autobattle", L"esc_back", 1, ini);
    if (escBack)                                               // ESC on the title/감상실 side: the previous menu
        escBack = HookImport(sys, "USER32.dll", "TranslateAcceleratorA", (void*)HookTranslateAcceleratorA, (void**)&realTranslateAcceleratorA);
    if (hardMode)
        hardMode = dlg && HookImport(sys, "KERNEL32.dll", "LoadLibraryA", (void*)HookLoadLibraryA, (void**)&realLoadLibraryA);
    if (GetPrivateProfileIntW(L"autobattle", L"duel_keys", 1, ini))     // B and M of the enemy robots (DUEL)
        HookImport(sys, "KERNEL32.dll", "GetProcAddress", (void*)HookGetProcAddress, (void**)&realGetProcAddress);
    duelMenuExit = GetPrivateProfileIntW(L"autobattle", L"duel_menu_exit", 1, ini) != 0;   // DUEL: the pause menu's EXIT leaves
    neroSideCamera = GetPrivateProfileIntW(L"autobattle", L"nero_side_camera", 0, ini) != 0;   // DUEL: Nero's side view (off)
    duelAuto = LoadDuelAuto();                         // the AI-vs-AI copies and the hand-over AIs (duel_handover.h)
    LoadHoAliases();
    duelAutoOn = GetPrivateProfileIntW(L"autobattle", L"duel_auto", 1, ini) != 0;   // DUEL 자동 전투 (11-03: its own switch)
    LoadDuelSettings(ini); // separate DUEL state; legacy balance setting migrates once
    LoadDuelBalance();
    LoadDuelBalanceScenes();                                   // 10-20 the hit fixes' copies (balance mode only)
    LoadDuelAiRoutes();
    // the defeat screen's lines (story voices with subtitles) - only with the subtitle window; trace_files=1 for tests
    if (GetPrivateProfileIntW(L"autobattle", L"subtitles", 1, ini)) LoadStoryVoices();
    LoadVoiceLevels(ini);                                      // per-character voice levels
    LoadVoiceGroups();
    bool trace = GetPrivateProfileIntW(L"autobattle", L"trace_files", 0, ini) != 0;
    if (storyN || voiceGroupN || trace)
    {
        HookImport(sys, "KERNEL32.dll", "CreateFileA", (void*)HookCreateFileA, (void**)&realCreateFileA);
        HookImport(sys, "KERNEL32.dll", "ReadFile", (void*)HookReadFile, (void**)&realReadFile);
    }
    traceSound = GetPrivateProfileIntW(L"autobattle", L"trace_sound", 0, ini);
    if (traceSound > 1) traceSoundLeft = traceSound;          // trace_sound=N: the first N sound events (1: 3000)
    bgSound = GetPrivateProfileIntW(L"autobattle", L"background_sound", 1, ini);   // 11-06 sound behind other windows
    if (voiceGroupN || traceSound || bgSound)                  // per-character voice levels, background sound: the buffers
        HookImport(sys, "DSOUND.dll", (const char*)(UINT_PTR)11, (void*)HookDSCreate8, (void**)&realDSCreate8);
    if (trace)
    {
        traceFilesLeft = 20000;
        HookImport(sys, "KERNEL32.dll", "SetFilePointer", (void*)HookSetFilePointer, (void**)&realSetFilePointer);
        HookImport(sys, "KERNEL32.dll", "CloseHandle", (void*)HookCloseHandle, (void**)&realCloseHandle);
        Log("TRACE: opens, moves, reads and closes in the voice packs are logged (trace_files=1, first 20000)");
    }
    uhd = GetPrivateProfileIntW(L"autobattle", L"uhd", 0, ini);
    if (uhd >= 1)   // before system.dll reads its settings and makes its window (khd4k hooks those)
    {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\khd4k.dll", abDir);
        if (LoadLibraryW(path)) Log("4K module loaded at start: %ls", path);
        else Log("4K module not loaded (error %lu): %ls", GetLastError(), path);
    }
    Log("system.dll hooked: own settings %d, Hard difficulty %d, 4K %d", ownSettings, hardMode, uhd);
}

// ---------------------------------------------------------------- exports
extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    if (!LoadReal()) return DIERR_GENERIC;
    HRESULT hr = realCreate(inst, ver, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out)
    {
        PatchSlot(*(void***)(*out), 3, (void*)HookCreateDevice, cdPatched, &cdN, 8);   // IDirectInput8::CreateDevice
        StartOnce();
    }
    return hr;
}

extern "C" HRESULT WINAPI Proxy_DllCanUnloadNow(void)
{
    if (!LoadReal()) return S_FALSE;
    Plain_t f = (Plain_t)GetProcAddress(real, "DllCanUnloadNow");
    return f ? f() : S_FALSE;
}

extern "C" HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out)
{
    if (!LoadReal()) return CLASS_E_CLASSNOTAVAILABLE;
    GetClassObject_t f = (GetClassObject_t)GetProcAddress(real, "DllGetClassObject");
    return f ? f(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI Proxy_DllRegisterServer(void) { return E_NOTIMPL; }
extern "C" HRESULT WINAPI Proxy_DllUnregisterServer(void) { return E_NOTIMPL; }

extern "C" LPCDIDATAFORMAT WINAPI Proxy_GetdfDIJoystick(void)
{
    if (!LoadReal()) return NULL;
    GetdfJoy_t f = (GetdfJoy_t)GetProcAddress(real, "GetdfDIJoystick");
    return f ? f() : NULL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        selfInst = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&patchLock);
        InitializeCriticalSection(&soundLock);
        DWORD n = GetModuleFileNameW(inst, gameDir, MAX_PATH);
        if (n && n < MAX_PATH)
        {
            wchar_t* slash = wcsrchr(gameDir, L'\\');
            if (slash) *slash = 0;
            swprintf_s(abDir, L"%s\\autobattle", gameDir);
            swprintf_s(logPath, L"%s\\proxy_log.txt", abDir);
            if (GetFileAttributesW(abDir) == INVALID_FILE_ATTRIBUTES) logPath[0] = 0;
        }
        if (logPath[0]) HookSystemDll();
    }
    return TRUE;
}
