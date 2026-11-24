// DUEL settings controls. Included only by the isolated proxy source.
// No stats or native technique commands are written here.
// 11-03 the same strip also carries 자동 전투 - on the DUEL screens (duel_auto, beside 균형 모드 and the difficulty) and on
// the FREE FIGHT screen (free_auto, alone; freebattle.nps marks that screen with the system-menu mask #200007E2).
static const int kDuelDifficultyId = 0x5A03;
static const DWORD kFreeMenuMask = 0x200007E2;  // 11-03: freebattle.nps (its copy in 0replay.pak) sets it
static HWND duelPanel, duelPanelOwner;
static HFONT duelPanelFont;
static int duelPanelFontHeight;
static HBRUSH duelPanelBrush;
static int duelPanelContext;                    // what the strip shows now: 2 DUEL, 1 FREE FIGHT
#ifdef KHD_SETTINGS_TEST
static bool testDuelMenu;
#endif

// 2 a DUEL screen, 1 the FREE FIGHT screen, 0 anything else
static int MenuPanelContext()
{
#ifdef KHD_SETTINGS_TEST
    return testDuelMenu ? 2 : 0;
#else
    HMODULE sys = GetModuleHandleW(L"system.dll");
    BYTE** global = sys ? (BYTE**)((BYTE*)sys + 0xEE680) : NULL;
    BYTE* obj = Readable(global, 4) ? *global : NULL;
    if (!Readable(obj + 4, 4)) return 0;
    DWORD mask = MenuMask(*(HWND*)(obj + 4));
    return mask == kDuelMenuMask ? 2 : mask == kFreeMenuMask ? 1 : 0;
#endif
}

static bool DuelMenuContext() { return MenuPanelContext() == 2; }

static void LoadDuelSettings(const wchar_t* ini)
{
    wchar_t value[32];
    GetPrivateProfileStringW(L"duel", L"balance", L"", value, 32, ini);
    bool old = !value[0];
    int balance = old ? GetPrivateProfileIntW(L"autobattle", L"duel_balance", 0, ini) : _wtoi(value);
    duelBalance = balance ? 1 : 0;
    int diff = GetPrivateProfileIntW(L"duel", L"difficulty", 1, ini);
    duelDifficulty = (diff >= 0 && diff <= 2) ? diff : 1;
    if (old) WritePrivateProfileStringW(L"duel", L"balance", duelBalance ? L"1" : L"0", ini);
    Log("DUEL settings loaded: balance=%ld AI difficulty=%ld; story difficulty independent", duelBalance, duelDifficulty);
}

static void SetDuelDifficulty(int value)
{
    if (value < 0 || value > 2 || !duelBalance) return;
    wchar_t ini[MAX_PATH], text[8];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    swprintf_s(text, L"%d", value);
    BOOL saved = WritePrivateProfileStringW(L"duel", L"difficulty", text, ini);
    if (saved) InterlockedExchange(&duelDifficulty, value);
    Log("DUEL selection: AI difficulty=%ld saved=%d (story settings untouched)", duelDifficulty, saved);
}

static void SyncDuelControls(HWND h)
{
    bool duel = duelPanelContext == 2;
    SendDlgItemMessageW(h, kBalanceCheckId, BM_SETCHECK, duelBalance ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(h, kDuelDifficultyId, CB_SETCURSEL, duelDifficulty, 0);
    EnableWindow(GetDlgItem(h, kDuelDifficultyId), duelBalance != 0);
    ShowWindow(GetDlgItem(h, kBalanceCheckId), duel ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(h, kDuelDifficultyId), duel ? SW_SHOW : SW_HIDE);
    SendDlgItemMessageW(h, kDuelAutoCheckId, BM_SETCHECK, (duel ? duelAutoOn : freeAuto) ? BST_CHECKED : BST_UNCHECKED, 0);
}

static LRESULT CALLBACK DuelPanelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateWindowExW(0, L"BUTTON", L"균형 모드", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        2, 1, 102, 24, h, (HMENU)(INT_PTR)kBalanceCheckId, selfInst, NULL);
        {
            HWND list = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        108, 1, 70, 120, h, (HMENU)(INT_PTR)kDuelDifficultyId, selfInst, NULL);
            SendMessageW(list, CB_ADDSTRING, 0, (LPARAM)L"이지");
            SendMessageW(list, CB_ADDSTRING, 0, (LPARAM)L"노말");
            SendMessageW(list, CB_ADDSTRING, 0, (LPARAM)L"하드");
        }
        CreateWindowExW(0, L"BUTTON", L"자동 전투", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        184, 1, 108, 24, h, (HMENU)(INT_PTR)kDuelAutoCheckId, selfInst, NULL);
        SyncDuelControls(h);
        return 0;
    case WM_COMMAND:
        if (!MenuPanelContext() || !IsWindowVisible(h)) return 0;
        if (LOWORD(wp) == kBalanceCheckId && HIWORD(wp) == BN_CLICKED && duelPanelContext == 2)
        {
            SetDuelBalance(SendDlgItemMessageW(h, kBalanceCheckId, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SyncDuelControls(h);
            return 0;
        }
        if (LOWORD(wp) == kDuelDifficultyId && HIWORD(wp) == CBN_SELCHANGE && duelPanelContext == 2)
        {
            SetDuelDifficulty((int)SendDlgItemMessageW(h, kDuelDifficultyId, CB_GETCURSEL, 0, 0));
            SyncDuelControls(h);
            return 0;
        }
        if (LOWORD(wp) == kDuelAutoCheckId && HIWORD(wp) == BN_CLICKED)
        {
            bool on = SendDlgItemMessageW(h, kDuelAutoCheckId, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (duelPanelContext == 2) SetDuelAuto(on);
            else SetFreeAuto(on);
            SyncDuelControls(h);
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor((HDC)wp, RGB(235, 240, 248));
        SetBkColor((HDC)wp, RGB(10, 18, 30));
        return (LRESULT)duelPanelBrush;
    case WM_ERASEBKGND:
        {
            RECT r; GetClientRect(h, &r); FillRect((HDC)wp, &r, duelPanelBrush);
        }
        return 1;
    case WM_DESTROY:
        if (duelPanel == h) duelPanel = NULL;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void UpdateDuelControls(HWND w)
{
    int context = (w && IsWindow(w) && IsWindowVisible(w) && IsWindowEnabled(w) && !InFight(w)) ? MenuPanelContext() : 0;
    if (!context)
    {
        if (duelPanel && IsWindowVisible(duelPanel)) ShowWindow(duelPanel, SW_HIDE);
        return;
    }
    if (duelPanel && (!IsWindow(duelPanel) || duelPanelOwner != w))
    {
        if (IsWindow(duelPanel)) DestroyWindow(duelPanel);
        duelPanel = NULL;
    }
    if (!duelPanel)
    {
        if (!duelPanelBrush) duelPanelBrush = CreateSolidBrush(RGB(10, 18, 30));
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DuelPanelProc; wc.hInstance = selfInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = duelPanelBrush; wc.lpszClassName = L"KhdDuelSettings";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        { Log("DUEL controls class creation failed"); return; }
        duelPanelContext = context;
        duelPanel = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"듀얼 설정",
                                  WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 296, 26, w, NULL, selfInst, NULL);
        duelPanelOwner = w;
        if (!duelPanel) { Log("DUEL controls creation failed: %lu", GetLastError()); return; }
        Log("menu controls created (DUEL: balance, easy/normal/hard, 자동 전투; FREE FIGHT: 자동 전투)");
    }
    if (context != duelPanelContext)
    {
        duelPanelContext = context;
        SyncDuelControls(duelPanel);
        Log("menu controls now for %s (자동 전투 %s)", context == 2 ? "DUEL" : "FREE FIGHT",
            (context == 2 ? duelAutoOn : freeAuto) ? "on" : "off");
    }
    RECT r; GetClientRect(w, &r);
    int cw = r.right, ch = r.bottom, iw, ih;
    if (cw <= 0 || ch <= 0) return;
    if (cw * 3 >= ch * 4) { ih = ch; iw = ch * 4 / 3; } else { iw = cw; ih = cw * 3 / 4; }
    int x = (cw-iw)/2 + 10*iw/800, y = (ch-ih)/2 + 570*ih/600;
    bool duel = context == 2;
    int pw = (duel ? 296 : 114)*iw/800, ph = 30*ih/600;
    SetWindowPos(duelPanel, HWND_TOP, x, y, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    MoveWindow(GetDlgItem(duelPanel, kBalanceCheckId), 2*iw/800, ih/600, 102*iw/800, 24*ih/600, TRUE);
    MoveWindow(GetDlgItem(duelPanel, kDuelDifficultyId), 108*iw/800, ih/600, 70*iw/800, 120*ih/600, TRUE);
    MoveWindow(GetDlgItem(duelPanel, kDuelAutoCheckId), (duel ? 184 : 2)*iw/800, ih/600, 108*iw/800, 24*ih/600, TRUE);
    int fontHeight = 15*ih/600;
    if (fontHeight < 10) fontHeight = 10;
    if (fontHeight != duelPanelFontHeight)
    {
        HFONT old = duelPanelFont;
        duelPanelFont = CreateFontW(-fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
        SendDlgItemMessageW(duelPanel, kBalanceCheckId, WM_SETFONT, (WPARAM)duelPanelFont, TRUE);
        SendDlgItemMessageW(duelPanel, kDuelDifficultyId, WM_SETFONT, (WPARAM)duelPanelFont, TRUE);
        SendDlgItemMessageW(duelPanel, kDuelAutoCheckId, WM_SETFONT, (WPARAM)duelPanelFont, TRUE);
        duelPanelFontHeight = fontHeight;
        if (old) DeleteObject(old);
    }
}

static void DrainDuelMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (duelPanel && IsWindowVisible(duelPanel) && IsDialogMessageW(duelPanel, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
}
