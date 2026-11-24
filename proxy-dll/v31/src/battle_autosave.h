#pragma once
// ---------------------------------------------------------------- battle save prompt: YES saves at once (11-06 Claude)
// user 11-06 「만일 전투 전 세이브에서 예스를 누르면 세이브 창이 뜨는 대신 자동으로 세이브되는 작은 변경」. Before a fight the
// chapter scripts ask 「저장할까요?」 (da0119 … da1401 #ほぞんほぞんっと - 14 places, the game's only <SAVE> tags); YES runs
// <SAVE>. Its handler (system.dll 0x32b00) saves at once into the slot of a "no" attribute (0x1bb00, thiscall(engine,
// slot, comment, 0) - the engine's own save, from the script thread) and opens the save window only when there is no
// "no" (0x32bcc: ecx = -1, call 0x64840). battle_autosave=1 (the default) sends that last call here: the next empty
// slot after the latest save (the vn-qol standard 「S = 다음 빈 칸에 바로 저장」 - latest [engine+0x18940], files
// save00.dat … save12.dat in the save folder [0x100ecaa8]) is saved at once and a notice shows the slot over the game;
// with every slot taken nothing is overwritten - the save window opens as before. battle_autosave=0: as before.
static BYTE* asSys;
static void* asDialog;                 // system.dll 0x64840: the save window (registers: ecx slot or -1, ebx comment, edi engine)
static void* asSaveSlot;               // system.dll 0x1bb00
// save00 … save59: the save/load window lists 60 slots, 「1번」 … 「60번」 (11-06 23:06: its scroll bar 0-55 with five
// rows in view; the bottom rows read 56번 … 60번), file number + 1. Slot 99 is the engine's own (the exit save,
// 0x1faa9). An earlier note's "save00~12" was only what existed in August - the user's copy holds save00 … save28.
static const int kAsSlots = 60;
static volatile LONG asNoticeSlot = -1;

static int __cdecl AsPickSlot(BYTE* engine)
{
    const char* dir = *(const char**)(asSys + 0xecaa8);
    if (!dir || !*dir) return -1;
    int latest = *(int*)(engine + 0x18940);
    int start = latest >= 0 && latest < kAsSlots ? latest + 1 : 0;
    for (int i = 0; i < kAsSlots; i++)
    {
        int s = (start + i) % kAsSlots;
        char p[MAX_PATH];
        _snprintf_s(p, _TRUNCATE, "%s\\save%02d.dat", dir, s);
        if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) return s;
    }
    return -1;
}

static BOOL CALLBACK AsFindGame(HWND h, LPARAM out)
{
    DWORD pid = 0;
    wchar_t cls[64];
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetClassNameW(h, cls, 64) && wcscmp(cls, L"OCGameApplicationWindow") == 0)
    {
        *(HWND*)out = h;
        return FALSE;
    }
    return TRUE;
}

static LRESULT CALLBACK AsNoticeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH back = CreateSolidBrush(RGB(24, 10, 18));
        FillRect(dc, &r, back);
        DeleteObject(back);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 160, 80));
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(pen);
        HFONT f = CreateFontW(-(r.bottom - r.top) * 11 / 20, 0, 0, 0, FW_BOLD, 0, 0, 0, HANGUL_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"맑은 고딕");
        HGDIOBJ of = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(240, 225, 190));
        wchar_t text[64];
        swprintf_s(text, L"전투 전 자동 저장 — %d번 칸", (int)asNoticeSlot + 1);
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);
        DeleteObject(f);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_TIMER) { DestroyWindow(h); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

// the notice: its own thread and message loop (the save runs on the script thread), 2.5 s over the top of the game
// picture, never activated, mouse goes through
static DWORD WINAPI AsNoticeThread(LPVOID)
{
    HWND game = NULL;
    EnumWindows(AsFindGame, (LPARAM)&game);
    if (!game) return 0;
    RECT c;
    GetClientRect(game, &c);
    POINT o = { 0, 0 };
    ClientToScreen(game, &o);
    int ch = c.bottom - c.top, cw = c.right - c.left;
    if (ch <= 0 || cw <= 0) return 0;
    int h = max(28, ch * 36 / 600), w = min(cw - 20, h * 9);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = AsNoticeProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"KhdBattleSaveNotice";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);              // a second time just fails: the class is there
    HWND n = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT, wc.lpszClassName, L"",
                             WS_POPUP, o.x + (cw - w) / 2, o.y + ch / 12, w, h, game, NULL, wc.hInstance, NULL);
    if (!n) return 0;
    SetLayeredWindowAttributes(n, 0, 225, LWA_ALPHA);
    ShowWindow(n, SW_SHOWNOACTIVATE);
    SetTimer(n, 1, 2500, NULL);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) DispatchMessageW(&m);
    return 0;
}

static void __cdecl AsSaved(BYTE* engine, int slot)
{
    Log("battle save prompt: YES saved at once into slot %d (save%02d.dat; latest now %d) - no save window", slot, slot, *(int*)(engine + 0x18940));
    asNoticeSlot = slot;
    HANDLE t = CreateThread(NULL, 0, AsNoticeThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

static void __cdecl AsFull()
{
    Log("battle save prompt: every slot (save00-%02d) is taken - nothing overwritten, the save window opens", kAsSlots - 1);
}

// in place of system.dll's call at 0x32bcc (the <SAVE> tag without "no"); registers as the save window wants them
__declspec(naked) static void AsThunk()
{
    __asm {
        pushad
        pushfd
        push edi                        // the engine
        call AsPickSlot
        add esp, 4
        cmp eax, -1
        je full
        mov esi, eax                    // the slot (esi comes back from pushad's copy afterwards)
        push 0                          // as <SAVE no=N> without a comment: (slot, 0, 0)
        push 0
        push eax
        mov ecx, edi
        call dword ptr [asSaveSlot]     // thiscall, the callee takes the three
        push esi
        push edi
        call AsSaved
        add esp, 8
        popfd
        popad
        ret                             // back after the call at 0x32bcc
    full:
        call AsFull
        popfd
        popad
        jmp dword ptr [asDialog]        // the save window, as if called from 0x32bcc
    }
}

static void InstallBattleAutosave(HMODULE module, const wchar_t* ini)
{
    if (!GetPrivateProfileIntW(L"autobattle", L"battle_autosave", 1, ini)) { Log("battle save prompt: the save window as before (battle_autosave=0)"); return; }
    BYTE* m = (BYTE*)module;
    BYTE* at = m + 0x32bcc;
    // the expected code around the call: or ecx,-1 / call 0x64840 / xor ebx,ebx - and the direct save the "no" path calls
    static const BYTE before[3] = { 0x83, 0xC9, 0xFF };
    if (memcmp(at - 3, before, 3) != 0 || at[0] != 0xE8 || at + 5 + *(LONG*)(at + 1) != m + 0x64840 || at[5] != 0x33 ||
        m[0x32ba6] != 0xE8 || m + 0x32ba6 + 5 + *(LONG*)(m + 0x32ba7) != m + 0x1bb00)
    {
        Log("battle save prompt: system.dll differs from the known build - the save window as before");
        return;
    }
    asSys = m;
    asDialog = m + 0x64840;
    asSaveSlot = m + 0x1bb00;
    BYTE call[5] = { 0xE8 };
    *(LONG*)(call + 1) = (LONG)((BYTE*)AsThunk - (at + 5));
    if (WriteCode(at, call, 5)) Log("battle save prompt: YES saves at once into the next empty slot (battle_autosave=1)");
}
