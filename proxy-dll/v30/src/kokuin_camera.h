#pragma once
// 11-05 user 「또 각인 666이 시전되었을떄 카메라를 강제로 시전 대상으로 옮겨서 우그라드는 모습을 보여줘」 → 「각인 666의 의도는 발동되는
// 순간 피격체를 크게 카메라에 잡아서 우그라드는 모습이 잘 보이게 하는거야」 → 「화면에 들어차게」 「네로가 잡히는게 아니라 발동당하는 적이
// 화면 크게」 → 「거의 게임 시작시의 크기처럼」 「카메라는 게임 시작시 우리편이 잡히는 카메라로 잡고」.
// The Nameless One's (qm) KOKUIN666 ends in its target's DUEL_666 (category 78, every robot's DUEL copies via 520: its
// AM_sl_R node swells to 2x at frame 15 and shrinks to 0.01 by 50; HP 0). While a robot plays DUEL_666 the camera is a
// portrait of it, the way the fight-start shot shows our machine: from its front a little to one side, slightly above,
// at the distance that makes its body fill the screen (half-height H, focal f: d = H * f / (330 * 0.92)).
// History: c1375821-dbc3b0ae put the shot into the camera-matrix CALLs (0x34d89 / 0x34dc9 -> 0x34780) and cut the rubber band
// (0x14c38): the 17:17 log showed the engine camera position exactly at our eye (394 from the chest) but the view direction
// stayed the old one (the matrix -> angles step read our rows differently) - the target was out of frame. Now the shot goes
// into the two SetCamera calls of the camera update (0x13e77, and 0x13ce9 that sends the same globals again): position and
// (pitch, yaw, roll) radians, +z ahead (khdcam's convention).
// 18:4x user 「전투중 카메라 이동이 누락된거 같은데 맞아?」: 419b2951 pointed both calls at us for the whole fight, and khdcam
// (d3dxof.dll, the battle camera view mod) hooks while the fight loads only if the call at 0x13e77 still goes to SetCamera -
// it logged "not the build this was made for - camera left alone" every fight. Now the calls stay SetCamera's: they point at
// us only while a robot plays DUEL_666 (switched from the battle keyboard read, KcTick), and are put back at every fight
// start. During the shot we enter SetCamera past its head (our own copy of its first 7 bytes) - khdcam's jump there would orbit
// the shot around our machine by the person's view.
// Display only: no actor / node / frame writes.
typedef void (__cdecl* KcSet_t)(float* rot, float* pos, unsigned flag);
static BYTE* kcModule;
static KcSet_t kcSetPrev;
static BYTE* kcFocus;
static DWORD kcSince, kcLogged;
static bool kcPatched;                       // the two calls point at KcSetCamera
static BYTE* kcTramp;                        // SetCamera's own first 7 bytes + a jump to the rest (past any hook in its head)
static bool kcTrampOk;
static const DWORD kcSites[2] = {0x13e77, 0x13ce9};
static const char* KcMotion(BYTE* a)
{
    BYTE* m = a ? *(BYTE**)(a + 0x398) : NULL;
    return m ? *(const char**)m : "";
}
static int KcFrame(BYTE* a)
{
    BYTE* m = a ? *(BYTE**)(a + 0x398) : NULL;
    return m ? *(int*)(m + 0x54) : -1;
}
static BYTE* KcCharacter(int group)
{
    BYTE* mgr = *(BYTE**)(kcModule + 0x11e3b0);
    BYTE* obj = mgr ? *(BYTE**)(mgr + 4) : NULL;
    if (!obj) return NULL;
    BYTE** b = *(BYTE***)(obj + 0x28);
    BYTE** e = *(BYTE***)(obj + 0x2c);
    if (!b || e < b || e - b > 64) return NULL;
    for (BYTE** p = b; p < e; ++p) {
        BYTE* c = *p ? *(BYTE**)(*p + 0x64) : NULL;
        if (c && (c[0x720] & 3) && *(int*)(c + 0x724) == group) return c;
    }
    return NULL;
}
static float KcHalf(const char* def)
{
    if (NcDefinitionFamily(def, "qm")) return 320.0f;             // the Nameless One
    if (NcDefinitionFamily(def, "rm") || NcDefinitionFamily(def, "sm")) return 600.0f;
    if (NcDefinitionFamily(def, "mm")) return 125.0f;             // Lord Byakhee
    return 230.0f;                                                // Ambrosius, Demonbane and the other machines
}
static NcVec KcAt(BYTE* a)                                        // lockOn node (chest), else the root
{
    MmWorld_t world = (MmWorld_t)(kcModule + 0x15930);
    BYTE* n = *(BYTE**)(a + 0xb84);
    const float* m = world(n ? n : a);
    return NcVec{m[12], m[13], m[14]};
}
static void KcAngles(NcVec eye, NcVec at, float* rot)
{
    NcVec f = NcUnit(NcSub(at, eye));
    if (f.y > 0.99f) f.y = 0.99f;
    if (f.y < -0.99f) f.y = -0.99f;
    rot[0] = -asinf(f.y);                                         // pitch (down positive)
    rot[1] = atan2f(f.x, f.z);                                    // yaw, +z ahead
    rot[2] = 0.0f;
}
// 17:35 calibration: the fight-start shot is our machine's chase camera - 295 behind it, 513 above its root, 25.7 degrees down,
// 337 from its chest (the Nameless One). The user wants the target framed like that: the engine's own camera position /
// angles for the target as if it owned the camera (0x34d80 / 0x34dc0, thiscall (actor, out) - what the camera update reads
// for our machine). If that comes back unusable, the portrait below.
typedef float* (__thiscall* KcCamVec_t)(BYTE* actor, float* out);
static bool KcOwnChase(BYTE* focus, float* rot, float* pos)
{
    float p[4] = {}, r[4] = {};
    ((KcCamVec_t)(kcModule + 0x34d80))(focus, p);
    ((KcCamVec_t)(kcModule + 0x34dc0))(focus, r);
    const NcVec at = KcAt(focus), eye = {p[0], p[1], p[2]};
    const float d = NcLength(NcSub(eye, at));
    if (!NcValid(eye) || !NcFinite(r[0]) || !NcFinite(r[1]) || !NcFinite(r[2]) || d < 60.0f || d > 2500.0f) return false;
    pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
    rot[0] = r[0]; rot[1] = r[1]; rot[2] = r[2];
    return true;
}
static bool KcShot(float* rot, float* pos)
{
    BYTE* p1 = KcCharacter(0);
    BYTE* p2 = KcCharacter(1);
    BYTE* focus = NULL;
    if (p1 && !_stricmp(KcMotion(p1), "DUEL_666")) focus = p1;
    else if (p2 && !_stricmp(KcMotion(p2), "DUEL_666")) focus = p2;
    if (!focus) return false;
    MmWorld_t world = (MmWorld_t)(kcModule + 0x15930);
    const float* r = world(focus);
    NcVec front = {r[8], 0, r[10]}, right = {r[0], 0, r[2]};
    if (NcLength(front) < 0.1f || NcLength(right) < 0.1f) return false;
    front = NcUnit(front); right = NcUnit(right);
    const NcVec v = KcAt(focus), up = {0, 1, 0};
    const char* def = NcDefinition(focus);
    float focal = p1 ? *(float*)(p1 + 0xb7c) : 512.0f;
    if (!NcFinite(focal) || focal < 64.0f || focal > 4096.0f) focal = 512.0f;
    float range = KcHalf(def) * focal / (330.0f * 0.92f);
    if (range < 150.0f) range = 150.0f;
    NcVec eye = NcAdd(v, NcAdd(NcMul(front, range * 0.93f), NcAdd(NcMul(right, range * 0.30f), NcMul(up, range * 0.12f))));
    if (!NcValid(v) || !NcValid(eye)) return false;
    const bool chase = KcOwnChase(focus, rot, pos);
    if (chase) eye = NcVec{pos[0], pos[1], pos[2]};
    else { pos[0] = eye.x; pos[1] = eye.y; pos[2] = eye.z; KcAngles(eye, v, rot); }
    const DWORD now = GetTickCount();
    if (focus != kcFocus) {
        kcFocus = focus; kcSince = now; kcLogged = 0;
        Log("KOKUIN_CAMERA on: side=%d %s@%d def=%.60s %s (portrait range %.0f) focal=%.1f mode=%.0f", *(int*)(focus + 0x724),
            KcMotion(focus), KcFrame(focus), def ? def : "?", chase ? "its own chase camera" : "portrait", range, focal,
            *(float*)(focus + 0xb6c));
    }
    if (now - kcLogged >= 250) {
        kcLogged = now;
        Log("KOKUIN_CAMERA shot %s@%d eye=%.0f,%.0f,%.0f at=%.0f,%.0f,%.0f pitch=%.1f yaw=%.1f", KcMotion(focus), KcFrame(focus),
            eye.x, eye.y, eye.z, v.x, v.y, v.z, rot[0] * 57.29578f, rot[1] * 57.29578f);
    }
    return true;
}
static void __cdecl KcSetCamera(float* rot, float* pos, unsigned flag)
{
    bool ours = false;
    __try {
        if (rot && pos) ours = KcShot(rot, pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) { ours = false; }
    ((ours && kcTrampOk) ? (KcSet_t)kcTramp : kcSetPrev)(rot, pos, flag);
}
// Point both calls at KcSetCamera (ours) or back at SetCamera. Only the 4-byte target changes (one interlocked write each).
static bool KcPoint(bool ours)
{
    for (int i = 0; i < 2; ++i) {
        BYTE* at = kcModule + kcSites[i];
        const LONG rel = (LONG)((ours ? (BYTE*)KcSetCamera : kcModule + 0x13b90) - (at + 5));
        if (*(LONG*)(at + 1) == rel) continue;
        DWORD old;
        if (!VirtualProtect(at + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
        InterlockedExchange((volatile LONG*)(at + 1), rel);
        VirtualProtect(at + 1, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
    kcPatched = ours;
    return true;
}
// Every battle keyboard read (HookGetDeviceState, the game's own thread, once a frame): the calls are ours only while a robot
// plays DUEL_666.
static void KcTick()
{
    if (!kcModule) return;
    __try {
        BYTE* p1 = KcCharacter(0);
        BYTE* p2 = KcCharacter(1);
        const bool want = (p1 && !_stricmp(KcMotion(p1), "DUEL_666")) || (p2 && !_stricmp(KcMotion(p2), "DUEL_666"));
        if (want == kcPatched) return;
        if (!want && kcFocus) Log("KOKUIN_CAMERA off after %lu ms", GetTickCount() - kcSince);
        if (!want) kcFocus = NULL;
        if (!KcPoint(want)) { Log("KOKUIN_CAMERA: could not switch the SetCamera calls - off for this fight"); kcModule = NULL; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// Per fight, before it loads. The two calls push the globals (rot 0x18d768, pos 0x18d758) - writing through the pointers keeps
// the billboards, effects and sounds that read them later in the frame consistent (khdcam notes).
static void InstallKokuinCamera(HMODULE module)
{
    kcFocus = NULL; kcSince = kcLogged = 0;
    kcModule = NULL; kcPatched = false; kcTrampOk = false;
    if (!module) return;
    BYTE* m = (BYTE*)module;
    for (int i = 0; i < 2; ++i) {
        BYTE* at = m + kcSites[i];
        if (at[0] != 0xe8) { Log("KOKUIN_CAMERA disabled: call opcode mismatch at %lx", kcSites[i]); return; }
        BYTE* to = at + 5 + *(LONG*)(at + 1);
        if (to != m + 0x13b90 && to != (BYTE*)KcSetCamera) { Log("KOKUIN_CAMERA disabled: SetCamera call %p unknown", to); return; }
    }
    kcModule = m;
    kcSetPrev = (KcSet_t)(m + 0x13b90);
    // a fight that ended in DUEL_666 can leave them ours: back to SetCamera before khdcam looks at 0x13e77 (it hooks while the
    // fight loads, after this)
    if (!KcPoint(false)) { Log("KOKUIN_CAMERA disabled: write failed"); kcModule = NULL; return; }
    static const BYTE head[7] = {0x8a, 0x44, 0x24, 0x0c, 0x83, 0xec, 0x60};             // mov al,[esp+0c] / sub esp,60
    static const BYTE rest[8] = {0x84, 0xc0, 0x53, 0x55, 0x56, 0x57, 0x75, 0x6d};
    BYTE* sc = m + 0x13b90;
    if ((!memcmp(sc, head, 7) || sc[0] == 0xe9) && !memcmp(sc + 7, rest, 8)) {
        if (!kcTramp) kcTramp = (BYTE*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (kcTramp) {
            memcpy(kcTramp, head, 7);
            kcTramp[7] = 0xe9;
            *(LONG*)(kcTramp + 8) = (LONG)(sc + 7 - (kcTramp + 12));
            FlushInstructionCache(GetCurrentProcess(), kcTramp, 16);
            kcTrampOk = true;
        }
    }
    Log("KOKUIN_CAMERA installed: a robot crumpling in DUEL_666 (刻印666) gets its own fight-start camera; SetCamera calls ours"
        " only then (khdcam keeps its hook)%s", kcTrampOk ? "" : " - SetCamera head unknown, the shot goes through it");
}
