// ---------------------------------------------------------------- 모든 콘텐츠 열기 (v30, 11-18)
// user 11-18 「근데 옵션에 모든 콘텐츠 개방을 내가 안넣었었나?」→「그렇네 옵션에 넣어줘」: 환경설정 기본 탭의 체크 칸
// 「모든 콘텐츠 열기」. Nothing the game keeps is written (vn-qol qa-mode (가) - the reads are answered instead), so unticking
// it locks everything as it was and progress made meanwhile stays:
//  * script conditions - system.dll 0x10021450 evaluates every OPERATOR of A / CHOICE (stdcall: engine, the condition's string
//    object (char* at +4), two pointers that are 0 for conditions). On a menu screen (system-menu mask 0x7E2 at the game object
//    [system.dll+0xEE680]+0x18E2C - the title, 감상실, DUEL and its info windows, story chart, theater; the story sets #000/#002) a condition on a
//    content flag - StageNN, クリア, 真クリア, RPp (>n), RPu_*, RP_da*, RP_end*, RPcN, RPeN, RPmN with =1 / =true / >n - is true.
//    The story's own reads (da0100 「2周目」 on クリア ...) stay real. The menu scripts' own writes (RPu_*, RPc*, RPp, RPm*) are
//    local variables they compute again on every entry, so no flag is kept by them either.
//  * CG gallery - its list builder (0x1003b6de / 0x1003b6ea) and its counter (0x1003dfa8 / 0x1003dfb0) take the engine's
//    debug branch (every entry open) while this is on: those four jumps are NOPs then and their bytes are put back after.
// autobattle.ini [autobattle] unlock_all=1 keeps it on.
static volatile LONG unlockAll;
static BYTE* uaSys;
typedef int(__stdcall* UaEval_t)(BYTE*, BYTE*, void*, void*);
static UaEval_t uaEvalOrig;
static const DWORD kUaEvalRva = 0x21450;
static const BYTE kUaEvalHead[7] = { 0x6A, 0xFF, 0x68, 0x18, 0xFC, 0x0B, 0x10 };   // push -1; push 0x100bfc18
struct UaPatch { DWORD rva; int n; BYTE orig[6]; };
static const UaPatch kUaCg[4] = {
    { 0x3B6DE, 6, { 0x0F, 0x84, 0x41, 0x01, 0x00, 0x00 } },   // CG list builder: debug flag of the engine -> normal list
    { 0x3B6EA, 6, { 0x0F, 0x84, 0x35, 0x01, 0x00, 0x00 } },   //                  global debug flag -> normal list
    { 0x3DFA8, 2, { 0x74, 0x34 } },                             // CG counter: the same two
    { 0x3DFB0, 2, { 0x74, 0x2C } },
};
static bool uaCgOk;      // the four sites were the expected code at the start

static bool UaWrite(BYTE* at, const BYTE* bytes, int n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

static void UaSetCg(bool on)
{
    if (!uaSys || !uaCgOk) return;
    static const BYTE nops[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    for (const auto& p : kUaCg) UaWrite(uaSys + p.rva, on ? nops : p.orig, p.n);
}

// a content flag's condition: <name><op><value> with a name of the content families and =1 / =true / >n
static bool UaContentGate(const char* s)
{
    size_t n = 0;
    while (s[n] && !strchr("=<>!+-*/% ", s[n])) n++;
    if (!n) return false;
    const char* op = s + n;
    while (*op == ' ') op++;
    bool ok = (op[0] == '>' && op[1] >= '0' && op[1] <= '9') ||
              (op[0] == '=' && (strcmp(op + 1, "1") == 0 || _stricmp(op + 1, "true") == 0));
    if (!ok) return false;
    auto starts = [&](const char* p) { size_t k = strlen(p); return n > k && _strnicmp(s, p, k) == 0; };
    auto digitsFrom = [&](size_t k) { if (n <= k) return false; for (size_t i = k; i < n; i++) if (s[i] < '0' || s[i] > '9') return false; return true; };
    static const char kClear[] = "\x83\x4e\x83\x8a\x83\x41";                 // クリア (CP932)
    static const char kTrueClear[] = "\x90\x5e\x83\x4e\x83\x8a\x83\x41";     // 真クリア
    if (n == 6 && memcmp(s, kClear, 6) == 0) return true;
    if (n == 8 && memcmp(s, kTrueClear, 8) == 0) return true;
    if (n == 3 && _strnicmp(s, "RPp", 3) == 0) return op[0] == '>';
    if (_strnicmp(s, "Stage", 5) == 0) return digitsFrom(5);
    if (starts("RPu_") || starts("RP_da") || starts("RP_end")) return true;
    if (_strnicmp(s, "RPc", 3) == 0 || _strnicmp(s, "RPe", 3) == 0 || _strnicmp(s, "RPm", 3) == 0) return digitsFrom(3);   // not RPch / RPmode
    return false;
}

static int uaTrace;      // tests: unlock_trace=1 logs the first 80 conditions the hook sees
// the system-menu mask of the game object (the evaluator's first argument is not that object): [system.dll+0xEE680]+0x18E2C,
// as MenuMask reads it for ESC
static DWORD UaMask()
{
    BYTE** global = uaSys ? (BYTE**)(uaSys + 0xEE680) : NULL;
    BYTE* obj = Readable(global, 4) ? *global : NULL;
    return obj && Readable(obj + 0x18E2C, 4) ? *(DWORD*)(obj + 0x18E2C) : 0xFFFFFFFF;
}
static int __stdcall UaEval(BYTE* eng, BYTE* str, void* a3, void* a4)
{
    if (uaTrace && eng && str)
    {
        static volatile LONG seen;
        __try
        {
            if (InterlockedIncrement(&seen) <= 80)
                Log("unlock trace: cond '%s' mask %08lx a3 %p a4 %p gate %d", *(const char**)(str + 4) ? *(const char**)(str + 4) : "(null)",
                    (unsigned long)UaMask(), a3, a4, *(const char**)(str + 4) ? (int)UaContentGate(*(const char**)(str + 4)) : -1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { Log("unlock trace: could not read the condition"); }
    }
    if (unlockAll && !a3 && !a4 && eng && str)
    {
        __try
        {
            const char* t = *(const char**)(str + 4);
            DWORD mask = UaMask();
            if (t && (mask & 0xFFFF) == 0x7E2 && UaContentGate(t))
            {
                static volatile LONG said;
                if (InterlockedIncrement(&said) <= 12) Log("unlock all: 「%hs」 answered true on a menu screen (mask %08lx)", t, (unsigned long)mask);
                return 1;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return uaEvalOrig(eng, str, a3, a4);
}

static void UaInstall(HMODULE sys, const wchar_t* ini)
{
    uaSys = (BYTE*)sys;
    BYTE* at = uaSys + kUaEvalRva;
    if (memcmp(at, kUaEvalHead, sizeof kUaEvalHead) != 0) { Log("unlock all: system.dll+0x%lX is not the expected code - not available", kUaEvalRva); return; }
    BYTE* tr = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return;
    memcpy(tr, kUaEvalHead, 7);                           // the 7 bytes we take (both position-independent pushes)
    tr[7] = 0xE9;
    *(LONG*)(tr + 8) = (LONG)((at + 7) - (tr + 12));
    uaEvalOrig = (UaEval_t)(void*)tr;
    BYTE jmp[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    *(LONG*)(jmp + 1) = (LONG)((BYTE*)(void*)UaEval - (at + 5));
    if (!UaWrite(at, jmp, 7)) { Log("unlock all: could not write the condition hook (error %lu)", GetLastError()); uaEvalOrig = NULL; return; }
    uaCgOk = true;
    for (const auto& p : kUaCg)
        if (memcmp(uaSys + p.rva, p.orig, p.n) != 0) uaCgOk = false;
    unlockAll = GetPrivateProfileIntW(L"autobattle", L"unlock_all", 0, ini) != 0;
    uaTrace = GetPrivateProfileIntW(L"autobattle", L"unlock_trace", 0, ini);
    UaSetCg(unlockAll != 0);
    Log("unlock all: condition hook in, CG gallery sites %ls; 모든 콘텐츠 열기 %ls", uaCgOk ? L"found" : L"NOT the expected code",
        unlockAll ? L"on (unlock_all=1)" : L"off");
}

static void UaSet(bool on)
{
    if (!uaEvalOrig) return;
    InterlockedExchange(&unlockAll, on ? 1 : 0);
    UaSetCg(on);
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    BOOL ok = WritePrivateProfileStringW(L"autobattle", L"unlock_all", on ? L"1" : L"0", ini);
    Log("unlock all: 모든 콘텐츠 열기 %ls%ls", on ? L"on" : L"off", ok ? L"" : L" (could not save)");
}
