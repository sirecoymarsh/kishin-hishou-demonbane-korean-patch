// khd4k.dll - 4K work for the Win11 copy of 機神飛翔デモンベイン (2026-10-15, Claude).
// The dinput8.dll proxy loads us while system.dll is being loaded (autobattle.ini [autobattle] uhd >= 1), before any
// of system.dll's code runs. Here: settings, logging, the hooking helpers, and the engine's own full-screen switch,
// which we keep from changing the display mode (it would switch the user's monitor to 800x600).
#include "common.h"

Settings cfg;
HMODULE sys;
DWORD sysBase, sysEnd;
wchar_t abDir[MAX_PATH];
static wchar_t logPath[MAX_PATH];
static CRITICAL_SECTION logLock;
static DWORD t0;

void Log(const char* fmt, ...)
{
    char line[1024];
    int n = _snprintf_s(line, _TRUNCATE, "%7lu ", GetTickCount() - t0);
    va_list ap;
    va_start(ap, fmt);
    if (n > 0) _vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&logLock);
    HANDLE f = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD w;
        WriteFile(f, line, (DWORD)strlen(line), &w, NULL);
        WriteFile(f, "\r\n", 2, &w, NULL);
        CloseHandle(f);
    }
    LeaveCriticalSection(&logLock);
}

DWORD Rva(void* ret)
{
    DWORD a = (DWORD)(UINT_PTR)ret;
    return (a >= sysBase && a < sysEnd) ? a - sysBase : 0x80000000 | (a & 0x7fffffff);
}

// ---------------------------------------------------------------- per (function, caller) budget for the trace
struct Site { const char* fn; DWORD rva; LONG n; };
static Site sites[512];
static LONG nSites;

bool Budget(const char* fn, DWORD rva, LONG full)
{
    for (LONG i = 0; i < nSites; i++)
        if (sites[i].fn == fn && sites[i].rva == rva) return InterlockedIncrement(&sites[i].n) <= full;
    EnterCriticalSection(&logLock);
    LONG i = nSites;
    if (i < 512) { sites[i].fn = fn; sites[i].rva = rva; sites[i].n = 1; nSites = i + 1; }
    LeaveCriticalSection(&logLock);
    return true;
}

static DWORD WINAPI CountThread(LPVOID)
{
    for (;;)
    {
        Sleep(10000);
        Log("---- call counts");
        for (LONG i = 0; i < nSites; i++) Log("  %-22s caller %08lx  x%ld", sites[i].fn, sites[i].rva, sites[i].n);
    }
}

// ---------------------------------------------------------------- import and inline patching
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
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* n = (IMAGE_IMPORT_BY_NAME*)(b + names[i].u1.AddressOfData);
            if (strcmp((char*)n->Name, func) == 0) return &slots[i];
        }
    }
    return NULL;
}

bool Patch(void** slot, void* hook, void** orig)
{
    if (!slot) return false;
    if (*slot == hook) return true;
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    if (orig) *orig = *slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

bool HookImport(const char* dll, const char* func, void* hook, void** orig)
{
    bool ok = Patch(ImportSlot(sys, dll, func), hook, orig);
    if (!ok) Log("could not hook %s!%s", dll, func);
    return ok;
}

bool HookModuleImport(HMODULE mod, const char* dll, const char* func, void* hook, void** orig)
{
    bool ok = Patch(ImportSlot(mod, dll, func), hook, orig);
    if (!ok) Log("could not hook %s!%s in module %p", dll, func, mod);
    return ok;
}

bool HookModuleImportIfAny(HMODULE mod, const char* dll, const char* func, void* hook)
{
    void** slot = ImportSlot(mod, dll, func);
    return slot && Patch(slot, hook, NULL);
}

// replaces the first n (>= 5) bytes at system.dll+rva with a jump to hook; *tramp gets those bytes + a jump back
bool InlineHook(DWORD rva, int n, void* hook, BYTE** tramp)
{
    BYTE* at = (BYTE*)sys + rva;
    BYTE* t = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return false;
    memcpy(t, at, n);
    t[n] = 0xE9;
    *(DWORD*)(t + n + 1) = (DWORD)(UINT_PTR)(at + n) - (DWORD)(UINT_PTR)(t + n + 5);
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    at[0] = 0xE9;
    *(DWORD*)(at + 1) = (DWORD)(UINT_PTR)hook - (DWORD)(UINT_PTR)(at + 5);
    for (int i = 5; i < n; i++) at[i] = 0x90;
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    if (tramp) *tramp = t;
    return true;
}

// ---------------------------------------------------------------- the engine's full-screen switch (system.dll+0x47410)
// The object is in edi, plain ret; +0x4b4 = 1 while full screen. The engine calls it at start when FullScreen is set
// in the registry, on its full-screen menu command (0x9c4e) and at exit to undo full screen. We never let it switch:
// the 4K display has its own full screen (borderless, no display mode change).
static void __stdcall LogToggle(DWORD ret, DWORD self)
{
    Log("engine full-screen switch asked by %08lx (full screen now %lu) - kept off", Rva((void*)(UINT_PTR)ret),
        self ? *(DWORD*)(UINT_PTR)(self + 0x4b4) : 0xffffffff);
}

__declspec(naked) static void ToggleHook()
{
    __asm {
        pushad
        mov eax, [esp + 32]
        push edi
        push eax
        call LogToggle
        popad
        ret
    }
}

static void BlockEngineFullScreen()
{
    const BYTE expect[7] = { 0x6A, 0xFF, 0x68, 0x88, 0xE5, 0x0B, 0x10 };      // push -1; push 0x100be588
    if (memcmp((BYTE*)sys + 0x47410, expect, 7) == 0 && InlineHook(0x47410, 7, (void*)ToggleHook, NULL))
        Log("engine full-screen switch (0x47410) kept off");
    else Log("engine full-screen switch NOT hooked - unexpected bytes");
}

// ---------------------------------------------------------------- ... and its FullScreen setting
// The settings saver (0x8100, from the config dialog) and the switch write FullScreen to the registry. A 1 there makes
// the next start without this module switch the monitor to 800x600 - on 10-16 that took the user's screen away. While
// we run it is always written as 0.
typedef LONG(WINAPI* RegSetValueExA_t)(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD);
static RegSetValueExA_t nextRegSetValueExA;

static LONG WINAPI KeepFullScreenOff(HKEY k, LPCSTR name, DWORD res, DWORD type, const BYTE* data, DWORD size)
{
    static const DWORD zero = 0;
    if (name && HIWORD((DWORD)(UINT_PTR)name) && _stricmp(name, "FullScreen") == 0 && type == REG_DWORD && data && size >= 4 &&
        *(const DWORD*)data != 0)
    {
        Log("engine wrote FullScreen=%lu (caller %08lx) - written as 0", *(const DWORD*)data, Rva(_ReturnAddress()));
        data = (const BYTE*)&zero;
    }
    return nextRegSetValueExA(k, name, res, type, data, size);
}

// ---------------------------------------------------------------- start
static void Start()
{
    wchar_t ini[MAX_PATH], mode[32];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    cfg.uhd = GetPrivateProfileIntW(L"autobattle", L"uhd", 0, ini);
    GetPrivateProfileStringW(L"autobattle", L"uhd_mode", L"full", mode, 32, ini);
    cfg.full = _wcsicmp(mode, L"window") != 0;
    cfg.scale = GetPrivateProfileIntW(L"autobattle", L"uhd_scale", 3, ini);
    if (cfg.scale < 1 || cfg.scale > 4) cfg.scale = 3;
    cfg.trace = cfg.uhd == 2 || GetPrivateProfileIntW(L"autobattle", L"uhd_trace", 0, ini) != 0;
    cfg.offscreen = GetPrivateProfileIntW(L"autobattle", L"uhd_offscreen", 0, ini) != 0;
    cfg.probe = GetPrivateProfileIntW(L"autobattle", L"uhd_probe", 0, ini) != 0;
    cfg.text = !cfg.probe && GetPrivateProfileIntW(L"autobattle", L"uhd_text", 1, ini) != 0;
    cfg.battle = GetPrivateProfileIntW(L"autobattle", L"uhd_battle", 1, ini) != 0;
    wchar_t cap[160];
    GetPrivateProfileStringW(L"autobattle", L"uhd_capture", L"", cap, 160, ini);
    wchar_t* ctx = NULL;
    for (wchar_t* part = wcstok_s(cap, L";", &ctx); part && cfg.capN < 4; part = wcstok_s(NULL, L";", &ctx))
    {
        int a = 0, b = 0, s = 0, k = 4, x = 0, y = 0, w = 0, h = 0;
        wchar_t* at = wcschr(part, L'@');
        if (at)
        {
            *at = 0;
            if (swscanf_s(at + 1, L"%d,%d,%d,%d", &x, &y, &w, &h) != 4 || w < 1 || h < 1) x = y = w = h = 0;
        }
        int got = swscanf_s(part, L"%d-%d/%d/%d", &a, &b, &s, &k);
        if (got < 3 || a < 1 || b < a || s < 1) continue;
        if (got < 4 || (k != 1 && k != 2 && k != 4)) k = 4;
        int i = cfg.capN++;
        cfg.capA[i] = a; cfg.capB[i] = b; cfg.capS[i] = s; cfg.capK[i] = w ? 1 : k;
        cfg.capX[i] = x; cfg.capY[i] = y; cfg.capW[i] = w; cfg.capH[i] = h;
    }
    cfg.capFirst = cfg.capN ? cfg.capA[0] : 0;
    cfg.capFrom = GetPrivateProfileIntW(L"autobattle", L"uhd_capfrom", 0, ini);
    cfg.envfix = GetPrivateProfileIntW(L"autobattle", L"uhd_envfix", 0, ini);
    cfg.pictrace = GetPrivateProfileIntW(L"autobattle", L"uhd_pictrace", 0, ini);
    cfg.stagec = GetPrivateProfileIntW(L"autobattle", L"uhd_stagec", 0, ini);
    cfg.dumpfo = GetPrivateProfileIntW(L"autobattle", L"uhd_dumpfo", 0, ini);
    cfg.check = GetPrivateProfileIntW(L"autobattle", L"uhd_check", 0, ini);
    GetPrivateProfileStringW(L"autobattle", L"uhd_hires", L"", cfg.hiresDir, MAX_PATH, ini);

    sys = GetModuleHandleW(L"system.dll");
    if (!sys) { Log("system.dll not found"); return; }
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)sys + ((IMAGE_DOS_HEADER*)sys)->e_lfanew);
    sysBase = (DWORD)(UINT_PTR)sys;
    sysEnd = sysBase + nt->OptionalHeader.SizeOfImage;
    Log("khd4k: uhd %d, %s, scale %d, text %d, battle %d, trace %d, probe %d; system.dll at %08lx", cfg.uhd,
        cfg.full ? "borderless full screen" : "window", cfg.scale, cfg.text, cfg.battle, cfg.trace, cfg.probe, sysBase);
    BlockEngineFullScreen();
    HookImport("ADVAPI32.dll", "RegSetValueExA", (void*)KeepFullScreenOff, (void**)&nextRegSetValueExA);
    if (cfg.trace)
    {
        InstallTrace();
        CloseHandle(CreateThread(NULL, 0, CountThread, NULL, 0, NULL));
    }
    if (cfg.uhd == 1) InstallDisplay();
    if (cfg.uhd == 1 && cfg.text) InstallUhd();
    if (cfg.probe) InstallProbe();
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&logLock);
        t0 = GetTickCount();
        DWORD n = GetModuleFileNameW(inst, abDir, MAX_PATH);
        if (n && n < MAX_PATH)
        {
            wchar_t* slash = wcsrchr(abDir, L'\\');
            if (slash) *slash = 0;
            swprintf_s(logPath, L"%s\\khd4k_trace.txt", abDir);
        }
        Start();
    }
    return TRUE;
}
