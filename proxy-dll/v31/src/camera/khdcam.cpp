// khdcam - 기신비상 데몬베인: 전투 중 카메라 시점 돌리기·당기기.
// Loaded as d3dxof.dll from the game folder: the battle module demonbane.dll imports d3dxof.dll, so this DLL comes in
// with the first battle. The real one (system folder) is forwarded to. The DLL pins itself (window hook, trampoline)
// and hooks every new battle module when that module asks for DirectXFileCreate (it is unloaded after each battle).
// 2026-11-01 Claude. Notes: P:\AI\khd-camera\진행.md
//
// demonbane.dll (D4F992BE91AD..., preferred base 0x10000000 but always relocated - system.dll sits there), found 11-01:
//   0x13b90 cdecl SetCamera(float* rot, float* pos, bool)   hands the camera to the draw device (vtbl +0x64, which
//           builds the view, the culling frustum and the copy at 0x11f2a8). rot = (pitch x, yaw y, roll z) radians,
//           rotation = D3DX YawPitchRoll (0xef80), the camera looks along +z of it; pos = world position.
//           CCamera::Update (0x13d00, the active camera only) calls it once a frame with the globals rot 0x18d768 /
//           pos 0x18d758; billboards, effects and sounds read those back later in the frame (0x13b70 / 0x13b80), so
//           changing them here moves everything consistently. 0x13cd0 may send the same globals again.
//   0x11e3b0 battle manager: [+4] -> list +0x28 / +0x2c of entries, entry +0x64 = character, character +0x720 & 3 =
//           alive, +0x724 = group (0 = 1P, 1 = 2P)  (0x39dc0 walks it)
//   character = scene node: 0x15930 thiscall -> world matrix (16 floats, row 3 = position);
//           +0xb6c float = camera mode of its motion (.def camera(n)): 1/2 main camera (rubber band), 0 = CAMERA_Null
//           without rubber band (special moves animate it), 3..9 = fixed CAMERA_Null3..9.
//   Scene files can hold more cameras (camera(n) in .scn, active = index [0x26f364]) but nothing in the release
//   build ever changes that index; the number keys in 0x298ee only force the camera mode (debug, disabled).
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- the real d3dxof.dll
static HMODULE realDll;
static FARPROC realCreate, realCanUnload, realGetClass;

static void LoadReal()
{
    if (realDll) return;
    wchar_t p[MAX_PATH];
    GetSystemDirectoryW(p, MAX_PATH);
    wcscat_s(p, L"\\d3dxof.dll");
    realDll = LoadLibraryW(p);
    if (!realDll) return;
    realCreate = GetProcAddress(realDll, "DirectXFileCreate");
    realCanUnload = GetProcAddress(realDll, "DllCanUnloadNow");
    realGetClass = GetProcAddress(realDll, "DllGetClassObject");
}

// ---------------------------------------------------------------- log and settings
static HMODULE self;
static wchar_t gameDir[MAX_PATH], iniPath[MAX_PATH], logPath[MAX_PATH];
static CRITICAL_SECTION logLock;

static void Log(const char* fmt, ...)
{
    char line[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = sprintf_s(line, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    n += vsprintf_s(line + n, sizeof(line) - n - 2, fmt, a);
    va_end(a);
    line[n++] = '\r';
    line[n++] = '\n';
    EnterCriticalSection(&logLock);
    HANDLE h = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, line, n, &w, NULL);
        CloseHandle(h);
    }
    LeaveCriticalSection(&logLock);
}

static float IniFloat(const wchar_t* sec, const wchar_t* key, float def)
{
    wchar_t buf[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, iniPath);
    return (float)_wtof(buf);
}

static int IniInt(const wchar_t* sec, const wchar_t* key, int def)
{
    return (int)GetPrivateProfileIntW(sec, key, def, iniPath);
}

static const float DEG = 3.14159265f / 180.0f;

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

struct View { float yaw, pitch, zoom; };    // offsets from the game's own camera: radians, radians, distance factor

static struct Config {
    int enabled, mouse, keys, invertY, remember, cinematicOriginal, logEvery;
    float mouseSens;        // radians per mouse pixel
    float keyYawRate, keyPitchRate, keyZoomRate;    // per second
    float wheelStep;        // distance factor per wheel notch
    float pitchMin, pitchMax;   // absolute camera pitch limits (radians) while we tilt
    float zoomMin, zoomMax;
    float smooth;           // seconds (time constant) for following input
    View presets[8];
    int presetCount;
} cfg;

static View saved;          // what the game remembers between battles (and in the ini)

static void LoadConfig()
{
    cfg.enabled = IniInt(L"camera", L"enabled", 1);
    cfg.mouse = IniInt(L"camera", L"mouse", 1);
    cfg.keys = IniInt(L"camera", L"keys", 1);
    cfg.invertY = IniInt(L"camera", L"invert_y", 0);
    cfg.remember = IniInt(L"camera", L"remember", 1);
    cfg.cinematicOriginal = IniInt(L"camera", L"cinematic_original", 1);
    cfg.mouseSens = IniFloat(L"camera", L"mouse_sens", 0.25f) * DEG;
    cfg.keyYawRate = IniFloat(L"camera", L"key_turn_speed", 90.0f) * DEG;
    cfg.keyPitchRate = IniFloat(L"camera", L"key_tilt_speed", 60.0f) * DEG;
    cfg.keyZoomRate = IniFloat(L"camera", L"key_zoom_speed", 1.8f);
    cfg.wheelStep = IniFloat(L"camera", L"wheel_step", 1.12f);
    cfg.pitchMin = IniFloat(L"camera", L"pitch_min", -20.0f) * DEG;
    cfg.pitchMax = IniFloat(L"camera", L"pitch_max", 80.0f) * DEG;
    cfg.zoomMin = IniFloat(L"camera", L"zoom_min", 0.4f);
    cfg.zoomMax = IniFloat(L"camera", L"zoom_max", 4.0f);
    cfg.smooth = IniFloat(L"camera", L"smooth", 0.08f);
    cfg.logEvery = IniInt(L"test", L"log_every", 0);
    // presets: "yaw pitch zoom" in degrees; the first is always the game's own view
    static const wchar_t* defs[] = { L"0 0 1", L"0 6 1.7", L"0 32 1.7", L"90 6 1.5", L"-90 6 1.5", L"180 8 1.3" };
    cfg.presetCount = 0;
    for (int i = 0; i < 8; i++) {
        wchar_t key[16], buf[64];
        swprintf_s(key, L"preset%d", i + 1);
        GetPrivateProfileStringW(L"camera", key, i < 6 ? defs[i] : L"", buf, 64, iniPath);
        float y, p, z;
        if (swscanf_s(buf, L"%f %f %f", &y, &p, &z) != 3) continue;
        cfg.presets[cfg.presetCount++] = View{ y * DEG, p * DEG, z };
    }
    if (!cfg.presetCount) cfg.presets[cfg.presetCount++] = View{ 0, 0, 1 };
}

static void LoadState()      // new game process always starts from the game's default view
{
    saved = View{ 0, 0, 1 };
    Log("startup default view 0 0 1; previous process state ignored");
}

static void SaveState(const View& v)
{
    wchar_t b[32];
    swprintf_s(b, L"%.1f", v.yaw / DEG);
    WritePrivateProfileStringW(L"state", L"yaw", b, iniPath);
    swprintf_s(b, L"%.1f", v.pitch / DEG);
    WritePrivateProfileStringW(L"state", L"pitch", b, iniPath);
    swprintf_s(b, L"%.3f", v.zoom);
    WritePrivateProfileStringW(L"state", L"zoom", b, iniPath);
    Log("view saved %.1f %.1f %.3f", v.yaw / DEG, v.pitch / DEG, v.zoom);
}

// test hooks (battle frames):
//   [test] script = frame:yaw:pitch:zoom, ...   jumps the view (degrees)
//          keys   = from-to:VK(hex), ...         counts that key as held (and the game as in front) - no real input
//          wheel  = frame:delta, ...             posts a WM_MOUSEWHEEL to the game window (goes through the hook)
struct Step { int frame; View v; };
static Step script[32];
static int scriptCount;
struct TestKey { int from, to, vk; };
static TestKey testKeys[32];
static int testKeyCount;
struct TestWheel { int frame, delta; };
static TestWheel testWheel[8];
static int testWheelCount;

static void LoadScript()
{
    wchar_t buf[1024];
    wchar_t* ctx = NULL;
    GetPrivateProfileStringW(L"test", L"script", L"", buf, 1024, iniPath);
    scriptCount = 0;
    for (wchar_t* tok = wcstok_s(buf, L",", &ctx); tok && scriptCount < 32; tok = wcstok_s(NULL, L",", &ctx)) {
        int f;
        float y, p, z;
        if (swscanf_s(tok, L" %d:%f:%f:%f", &f, &y, &p, &z) == 4)
            script[scriptCount++] = Step{ f, View{ y * DEG, p * DEG, z } };
    }
    GetPrivateProfileStringW(L"test", L"keys", L"", buf, 1024, iniPath);
    testKeyCount = 0;
    for (wchar_t* tok = wcstok_s(buf, L",", &ctx); tok && testKeyCount < 32; tok = wcstok_s(NULL, L",", &ctx)) {
        int a, b, vk;
        if (swscanf_s(tok, L" %d-%d:%x", &a, &b, &vk) == 3) testKeys[testKeyCount++] = TestKey{ a, b, vk };
    }
    GetPrivateProfileStringW(L"test", L"wheel", L"", buf, 1024, iniPath);
    testWheelCount = 0;
    for (wchar_t* tok = wcstok_s(buf, L",", &ctx); tok && testWheelCount < 8; tok = wcstok_s(NULL, L",", &ctx)) {
        int f, d;
        if (swscanf_s(tok, L" %d:%d", &f, &d) == 2) testWheel[testWheelCount++] = TestWheel{ f, d };
    }
}

// ---------------------------------------------------------------- input
static HWND gameWnd;
static HHOOK msgHook;
static volatile LONG wheelDelta;
static volatile LONG resetRequest; // queued pause-menu request

static LRESULT CALLBACK MsgProc(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && wp == PM_REMOVE) {
        const MSG* m = (const MSG*)lp;
        if (m->hwnd == gameWnd && m->message == WM_MOUSEWHEEL)
            InterlockedExchangeAdd(&wheelDelta, GET_WHEEL_DELTA_WPARAM(m->wParam));

    }
    return CallNextHookEx(msgHook, code, wp, lp);
}

static BOOL CALLBACK FindGameWnd(HWND h, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t cls[64];
    if (pid == GetCurrentProcessId() && GetClassNameW(h, cls, 64) && !wcscmp(cls, L"OCGameApplicationWindow")) {
        gameWnd = h;
        return FALSE;
    }
    return TRUE;
}

static void EnsureWindowHook()
{
    if (gameWnd && IsWindow(gameWnd)) return;
    gameWnd = NULL;
    EnumWindows(FindGameWnd, 0);
    if (!gameWnd) return;
    if (msgHook) UnhookWindowsHookEx(msgHook);
    msgHook = SetWindowsHookExW(WH_GETMESSAGE, MsgProc, self, GetWindowThreadProcessId(gameWnd, NULL));
    Log("game window %p, wheel hook %p", gameWnd, msgHook);
}

static bool Foreground()
{
    HWND f = GetForegroundWindow();
    DWORD pid = 0;
    if (f) GetWindowThreadProcessId(f, &pid);
    return pid == GetCurrentProcessId();
}

// ---------------------------------------------------------------- the camera
static uint8_t* db;             // battle module base the hooks are in
static uint8_t* tramp;          // original SetCamera head + jump back
typedef void(__cdecl* SetCamera_t)(float* rot, float* pos, unsigned flag);
typedef float*(__fastcall* GetWorld_t)(void* node, void* unused);

static View target, cur;        // what the player asked for / what is on screen now
static float weight;            // 1 = our view, 0 = the game's own (special-move cameras), eased back in
static int frame;               // SetCamera frames in this battle

static bool Down(int vk)
{
    for (int i = 0; i < testKeyCount; i++)
        if (testKeys[i].vk == vk && frame >= testKeys[i].from && frame <= testKeys[i].to) return true;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
static float lastOut[6];
static bool haveOut;
static LARGE_INTEGER qpf, lastTick;
static bool rDrag, mPrev, presetPrev[2], resetPrev;
static POINT dragAt;
static int presetIdx;
static DWORD dirtySince;

static void ResetViewNow(const char* source)
{
    View before = target;
    cur = target = saved = View{ 0, 0, 1 };
    presetIdx = 0;
    haveOut = false;
    rDrag = false;
    InterlockedExchange(&wheelDelta, 0);
    dirtySince = GetTickCount();
    Log("view reset source=%s old=%.2f/%.2f/%.3f new=0/0/1", source,
        before.yaw / DEG, before.pitch / DEG, before.zoom);
}

extern "C" BOOL WINAPI KhdCamResetView()
{
    if (!cfg.enabled) return FALSE;
    InterlockedExchange(&resetRequest, 1);
    return TRUE;
}

static void Fwd(float pitch, float yaw, float f[3])
{
    f[0] = sinf(yaw) * cosf(pitch);
    f[1] = -sinf(pitch);
    f[2] = cosf(yaw) * cosf(pitch);
}

static uint8_t* Character(int group)
{
    uint8_t* mgr = *(uint8_t**)(db + 0x11e3b0);
    if (!mgr) return NULL;
    uint8_t* obj = *(uint8_t**)(mgr + 4);
    if (!obj) return NULL;
    uint8_t** b = *(uint8_t***)(obj + 0x28);
    uint8_t** e = *(uint8_t***)(obj + 0x2c);
    if (!b || e < b || e - b > 64) return NULL;
    for (uint8_t** p = b; p < e; p++) {
        uint8_t* c = *p ? *(uint8_t**)(*p + 0x64) : NULL;
        if (c && (c[0x720] & 3) && *(int*)(c + 0x724) == group) return c;
    }
    return NULL;
}

static void Input(float dt)
{
    bool fg = Foreground() || testKeyCount || testWheelCount;
    if (fg) EnsureWindowHook();
    for (int i = 0; i < testWheelCount; i++)
        if (testWheel[i].frame == frame && gameWnd)
            PostMessageW(gameWnd, WM_MOUSEWHEEL, MAKEWPARAM(0, testWheel[i].delta), 0), Log("test wheel %d", testWheel[i].delta);
    LONG wheel = InterlockedExchange(&wheelDelta, 0);
    if (!fg) {
        rDrag = false;
        return;
    }
    bool changed = false;
    if (cfg.mouse) {
        if (Down(VK_RBUTTON)) {
            POINT p;
            GetCursorPos(&p);
            if (!rDrag) dragAt = p;
            else if (p.x != dragAt.x || p.y != dragAt.y) {
                target.yaw += (p.x - dragAt.x) * cfg.mouseSens;
                target.pitch += (p.y - dragAt.y) * cfg.mouseSens * (cfg.invertY ? -1.0f : 1.0f);
                SetCursorPos(dragAt.x, dragAt.y);        // the cursor is hidden in battle; keep it from the edges
                changed = true;
            }
            rDrag = true;
        } else rDrag = false;
        if (wheel) {
            target.zoom *= powf(cfg.wheelStep, -wheel / (float)WHEEL_DELTA);
            changed = true;
        }
        bool m = Down(VK_MBUTTON);
        if (m && !mPrev) {
            ResetViewNow("middle-button");
            changed = true;
        }
        mPrev = m;
    }
    if (cfg.keys) {
        // numpad (NumLock on) and the six-key block for keyboards without one; the game reads neither by default
        float yaw = (Down(VK_NUMPAD6) || Down(VK_NEXT) ? 1.0f : 0.0f) - (Down(VK_NUMPAD4) || Down(VK_DELETE) ? 1.0f : 0.0f);
        float tilt = (Down(VK_NUMPAD8) || Down(VK_HOME) ? 1.0f : 0.0f) - (Down(VK_NUMPAD2) || Down(VK_END) ? 1.0f : 0.0f);
        float zoom = (Down(VK_SUBTRACT) || Down(VK_PRIOR) ? 1.0f : 0.0f) - (Down(VK_ADD) || Down(VK_INSERT) ? 1.0f : 0.0f);
        if (yaw || tilt || zoom) {
            target.yaw += yaw * cfg.keyYawRate * dt;
            target.pitch += tilt * cfg.keyPitchRate * dt;
            target.zoom *= powf(cfg.keyZoomRate, zoom * dt);
            changed = true;
        }
        bool reset = Down(VK_NUMPAD5) || Down(VK_BACK);
        if (reset && !resetPrev) {
            ResetViewNow("keyboard");
            changed = true;
        }
        resetPrev = reset;
        bool next[2] = { Down(VK_NUMPAD0) || (Down(VK_TAB) && !Down(VK_MENU)), Down(VK_DECIMAL) };
        for (int i = 0; i < 2; i++) {
            if (next[i] && !presetPrev[i]) {
                presetIdx = (presetIdx + (i ? cfg.presetCount - 1 : 1)) % cfg.presetCount;
                target = cfg.presets[presetIdx];
                changed = true;
                Log("preset %d", presetIdx + 1);
            }
            presetPrev[i] = next[i];
        }
    }
    target.zoom = Clamp(target.zoom, cfg.zoomMin, cfg.zoomMax);
    target.pitch = Clamp(target.pitch, -80 * DEG, 80 * DEG);
    while (target.yaw > 3.14159265f) target.yaw -= 6.2831853f, cur.yaw -= 6.2831853f;
    while (target.yaw < -3.14159265f) target.yaw += 6.2831853f, cur.yaw += 6.2831853f;
    if (changed) dirtySince = GetTickCount();
}

static void Adjust(float* rot, float* pos)
{
    if (haveOut && !memcmp(rot, lastOut, 12) && !memcmp(pos, lastOut + 3, 12)) return;   // sent again, already ours
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = frame ? (float)(now.QuadPart - lastTick.QuadPart) / qpf.QuadPart : 0.0f;
    dt = Clamp(dt, 0.0f, 0.1f);
    lastTick = now;
    frame++;

    for (int i = 0; i < scriptCount; i++)
        if (script[i].frame == frame) cur = target = script[i].v, Log("script frame %d -> view %d", frame, i);
    Input(dt);
    float k = cfg.smooth > 0 ? 1.0f - expf(-dt / cfg.smooth) : 1.0f;
    cur.yaw += (target.yaw - cur.yaw) * k;
    cur.pitch += (target.pitch - cur.pitch) * k;
    cur.zoom *= powf(target.zoom / cur.zoom, k);
    saved = target;
    if (cfg.remember && dirtySince && GetTickCount() - dirtySince > 1000) {
        SaveState(saved);
        dirtySince = 0;
    }

    uint8_t* me = Character(0);
    int mode = me ? (int)*(float*)(me + 0xb6c) : -1;
    bool cinematic = cfg.cinematicOriginal && me && (mode == 0 || mode >= 3);
    if (cinematic) weight = 0;
    else weight = weight + dt / 0.5f > 1 ? 1 : weight + dt / 0.5f;

    float pitch0 = rot[0], yaw0 = rot[1];
    float f0[3];
    Fwd(pitch0, yaw0, f0);
    float depth = 150, t = 0;
    if (me) {
        const float* w = ((GetWorld_t)(db + 0x15930))(me, NULL);
        float d[3] = { w[12] - pos[0], w[13] - pos[1], w[14] - pos[2] };
        t = d[0] * f0[0] + d[1] * f0[1] + d[2] * f0[2];
        if (t > 20 && t < 3000) depth = t;
    }
    if (cfg.logEvery > 0 && frame % cfg.logEvery == 1) {
        uint8_t* foe = Character(1);
        Log("f%d mode %d/%d w %.2f view %.0f %.0f %.2f | cam %.0f %.0f %.0f rot %.1f %.1f %.1f | depth %.0f",
            frame, mode, foe ? (int)*(float*)(foe + 0xb6c) : -1, weight, cur.yaw / DEG,
            cur.pitch / DEG, cur.zoom, pos[0], pos[1], pos[2], rot[0] / DEG, rot[1] / DEG, rot[2] / DEG, t);
    }
    if (weight <= 0 || (fabsf(cur.yaw) < 1e-4f && fabsf(cur.pitch) < 1e-4f && fabsf(cur.zoom - 1) < 1e-4f)) {
        haveOut = false;
        return;
    }
    // orbit about the point on the view axis level with our machine, then pull back by the zoom factor
    float pivot[3] = { pos[0] + f0[0] * depth, pos[1] + f0[1] * depth, pos[2] + f0[2] * depth };
    float lo = cfg.pitchMin - pitch0, hi = cfg.pitchMax - pitch0;    // never push a view that is already outside back in
    if (lo > 0) lo = 0;
    if (hi < 0) hi = 0;
    float pitch = pitch0 + weight * Clamp(cur.pitch, lo, hi);
    float yaw = yaw0 + weight * cur.yaw;
    float dist = depth * (1 + weight * (cur.zoom - 1));
    float f1[3];
    Fwd(pitch, yaw, f1);
    for (int i = 0; i < 3; i++) pos[i] = pivot[i] - f1[i] * dist;
    rot[0] = pitch;
    rot[1] = yaw;
    memcpy(lastOut, rot, 12);
    memcpy(lastOut + 3, pos, 12);
    haveOut = true;
}

static void __cdecl HookSetCamera(float* rot, float* pos, unsigned flag)
{
    __try {
        LONG request = InterlockedExchange(&resetRequest, 0);
        if (request) {
            ResetViewNow("pause-menu");
        }
        if (cfg.enabled) Adjust(rot, pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool once;
        if (!once) Log("camera adjust faulted (code %08lx) - left the game's camera alone", GetExceptionCode()), once = true;
        haveOut = false;
    }
    ((SetCamera_t)tramp)(rot, pos, flag);
}

// ---------------------------------------------------------------- hooking the battle module
static bool Same(const uint8_t* p, const char* hex)
{
    for (; *hex; hex += 3, p++) {
        unsigned v;
        if (sscanf_s(hex, "%2x", &v) != 1 || *p != v) return false;
        if (!hex[2]) break;
    }
    return true;
}

static void HookBattleModule()
{
    uint8_t* b = (uint8_t*)GetModuleHandleW(L"demonbane.dll");
    if (!b) return;
    uint8_t* sc = b + 0x13b90;
    if (sc[0] == 0xE9 && sc + 5 + *(int32_t*)(sc + 1) == (uint8_t*)HookSetCamera) return;     // this module already
    bool ok = Same(sc, "8a 44 24 0c 83 ec 60 84 c0 53 55 56 57 75 6d")
        && b[0x13e77] == 0xE8 && b + 0x13e7c + *(int32_t*)(b + 0x13e78) == sc
        && Same(b + 0x29a00, "8b 0d") && *(uint8_t**)(b + 0x29a02) == b + 0x11e3b0
        && Same(b + 0x3478e, "d9 83 6c 0b 00 00")
        && Same(b + 0x39dc0, "8b 49 04 56 8b 71 28")
        && Same(b + 0x39dec, "8b 40 64 f6 80 20 07 00 00 03 74 08 39 b8 24 07 00 00")
        && Same(b + 0x15930, "83 ec 40 53 8b d9 8a 83 94 00 00 00");
    if (!ok) {
        Log("demonbane.dll at %p is not the build this was made for - camera left alone", b);
        return;
    }
    if (!tramp) tramp = (uint8_t*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;
    memcpy(tramp, sc, 7);                                   // mov al,[esp+0c] / sub esp,60
    tramp[7] = 0xE9;
    *(int32_t*)(tramp + 8) = (int32_t)(sc + 7 - (tramp + 12));
    FlushInstructionCache(GetCurrentProcess(), tramp, 16);
    DWORD old;
    if (!VirtualProtect(sc, 7, PAGE_EXECUTE_READWRITE, &old)) return;
    sc[0] = 0xE9;
    *(int32_t*)(sc + 1) = (int32_t)((uint8_t*)HookSetCamera - (sc + 5));
    sc[5] = sc[6] = 0x90;
    VirtualProtect(sc, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), sc, 7);
    db = b;
    LoadConfig();
    LoadScript();
    if (cfg.remember && dirtySince) SaveState(saved);       // changed in the last second of the previous battle
    dirtySince = 0;
    if (!cfg.remember) saved = View{ 0, 0, 1 };
    frame = 0;
    haveOut = false;
    weight = 1;
    cur = target = saved;
    presetIdx = 0;
    Log("battle module %p hooked (enabled %d, view %.0f %.0f %.2f, %d presets, test: %d script steps, %d keys, %d wheel)",
        b, cfg.enabled, saved.yaw / DEG, saved.pitch / DEG, saved.zoom, cfg.presetCount, scriptCount, testKeyCount,
        testWheelCount);
}

// ---------------------------------------------------------------- exports
extern "C" HRESULT WINAPI P_DirectXFileCreate(void** out)
{
    LoadReal();
    HookBattleModule();
    if (!realCreate) return E_FAIL;
    return ((HRESULT(WINAPI*)(void**))realCreate)(out);
}

extern "C" HRESULT WINAPI P_DllCanUnloadNow()
{
    LoadReal();
    return realCanUnload ? ((HRESULT(WINAPI*)())realCanUnload)() : S_FALSE;
}

extern "C" HRESULT WINAPI P_DllGetClassObject(REFCLSID c, REFIID i, void** out)
{
    LoadReal();
    if (!realGetClass) return CLASS_E_CLASSNOTAVAILABLE;
    return ((HRESULT(WINAPI*)(REFCLSID, REFIID, void**))realGetClass)(c, i, out);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID)
{
    if (why == DLL_PROCESS_ATTACH) {
        self = h;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&logLock);
        QueryPerformanceFrequency(&qpf);
        GetModuleFileNameW(h, gameDir, MAX_PATH);
        wchar_t* slash = wcsrchr(gameDir, L'\\');
        if (slash) slash[1] = 0;
        swprintf_s(iniPath, L"%skhdcam.ini", gameDir);
        swprintf_s(logPath, L"%skhdcam_log.txt", gameDir);
        DeleteFileW(logPath);
        HMODULE pin;   // stay for the whole game: the window hook and the trampoline outlive each battle module
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)DllMain, &pin);
        LoadConfig();
        LoadState();
        Log("khdcam loaded (%ls)", iniPath);
    }
    return TRUE;
}
