// 11-05 user: 「클라우디우스의 몸통 박치기가 대상이 사거리 바깥에 있어도 그냥 쓰고 보는데 몸통 박치기가 정확히 직격할
// 거리에서만 쓰게 만들어줘」.
// The native LOAD (Lord Byakhee) AI picks its body tackle at root distance 1000-2000 (tick 0x60290, sequence 4 ->
// action 5 -> 0x601f0, which requests name index 4 B_ATTACK once idle). The dash runs frames 95-125 (catch window)
// at about 43 units a frame; in 22 test starts it caught only at d 1100 (1300-1900: 0 of 21).
// DUEL fights, Lord Byakhee actors (definition under /character/mm/) on either side, native AI only:
//  - the tackle request goes out only within TG_RANGE (3D root distance, the native AI's own measure) and TG_HEIGHT;
//  - farther, the native approach step (vtable +0x3c, the call native action 1 makes each tick) closes in first;
//  - not in range after TG_APPROACH_MS: RIFLE1 (the native tree's other mid-range branch, MP 20) or, without the MP,
//    the tackle is dropped and the native sequence goes on.
// Hard-mode requests (HvThink: B_ATTACK at <= 220) are already inside the range. No motion/frame/position/HP/MP writes.
// v17 (11-05, after the cut-short v16 test: 53 holds, 1 go, 4 RIFLE1): height 300 -> 450 (the tackle's own CORRECTDIST_Y
// reaches 450 and the approach leaves Lord Byakhee ~300 above a ground robot: a 726 hold fell back at dy -302); the clock
// pauses while it is hit / held (one fallback fired straight out of DAMAGE4) and a special that restarts within 400 ms is
// the same attempt (a burst re-armed it every 50-250 ms for 6 s - it stood still, the 3 s limit never came); REINIT lines.
#pragma once
typedef void (__thiscall* TgSpecial_t)(BYTE*);
typedef bool (__thiscall* TgIdle_t)(BYTE*);
typedef float* (__thiscall* TgPos_t)(BYTE*, float*);
typedef void (__thiscall* TgReset_t)(BYTE*);
typedef void (__thiscall* TgAttack_t)(BYTE*, int);
static BYTE* tgModule;
static bool tgEnabled;
static const float TG_RANGE = 1000.0f, TG_HEIGHT = 450.0f;
static const DWORD TG_APPROACH_MS = 3000;
enum { TG_NATIVE, TG_INIT, TG_GO, TG_APPROACH, TG_RIFLE, TG_DROP, TG_WAIT };
struct TgState { BYTE* ai; DWORD since, lastTrace, used, lastCall, lastGap, lastReinitLog; int phase, reinits; bool held; float firstDist; };
static TgState tgStates[4];
static DWORD tgClock;

static const char* TgName(BYTE* ai, int index)
{
    BYTE* str = ai + 0x35c + index * 0x1c;
    return *(unsigned*)(str + 0x18) >= 16 ? *(const char**)(str + 4) : (const char*)(str + 4);
}
static bool TgNamesOk(BYTE* ai)
{
    if (*(int*)(ai + 0x6dc) < 11) return false;
    const char* tackle = TgName(ai, 4);
    const char* rifle = TgName(ai, 7);
    return tackle && rifle && !strcmp(tackle, "B_ATTACK") && !strcmp(rifle, "RIFLE1");
}
static TgState* TgSlot(BYTE* ai)
{
    TgState* oldest = &tgStates[0];
    for (unsigned i = 0; i < _countof(tgStates); ++i) {
        if (tgStates[i].ai == ai) { tgStates[i].used = ++tgClock; return &tgStates[i]; }
        if (tgStates[i].used < oldest->used) oldest = &tgStates[i];
    }
    ZeroMemory(oldest, sizeof *oldest);
    oldest->ai = ai; oldest->used = ++tgClock;
    return oldest;
}
static BYTE* TgTarget(BYTE* actor)
{
    BYTE* manager = *(BYTE**)(tgModule + 0x11e3b0);
    BYTE* target = manager ? ((MmTarget_t)(tgModule + 0x39d60))(manager, actor) : NULL;
    if (!target || *(int*)(target + 0x558) <= 0 || *(int*)(target + 0x724) == *(int*)(actor + 0x724)) return NULL;
    return target;
}
// Moving or idle: the approach and the clock run. Hit, held, guarding, a mode change: they wait.
static bool TgActorFree(BYTE* actor)
{
    static const char* const p[] = {"STOP", "FORWARD", "BACK", "LMOVE", "RMOVE", "LFORWARD", "RFORWARD", "LBACK", "RBACK",
        "UP", "DOWN", "LQUICK", "RQUICK", "FQUICK", "BQUICK", "CHARGE", "EMPTY"};
    BYTE* mo = *(BYTE**)(actor + 0x398);
    const char* n = mo ? *(const char**)mo : NULL;
    if (!n) return false;
    for (unsigned i = 0; i < _countof(p); ++i)
        if (!_strnicmp(n, p[i], strlen(p[i]))) return true;
    return false;
}
// Reads only (native target / world-matrix getters). Decides what this tick does with the native special.
static int TgPlan(BYTE* ai, BYTE* actor, TgState* s, float* d3, float* dy, BYTE** foe)
{
    const int sub = *(int*)(ai + 0x1c);   // 0 init, 1 waiting to request B_ATTACK, 2 running, others: native reset
    const DWORD now = GetTickCount(), gap = now - s->lastCall;
    s->lastCall = now; s->lastGap = gap;
    if (sub == 0) return TG_INIT;
    if (sub != 1 || s->phase == 2) return TG_NATIVE;
    if (s->phase == 0 || gap > 400) { s->phase = 1; s->since = now; s->held = false; s->firstDist = 0; }
    if (!TgActorFree(actor)) { s->since += gap < 400 ? gap : 0; return TG_WAIT; }   // the clock pauses
    BYTE* target = TgTarget(actor);
    if (!target || *(int*)(actor + 0x558) <= 0) return TG_NATIVE;
    *foe = target;
    MmWorld_t world = (MmWorld_t)(tgModule + 0x15930);
    const float* a = world(actor);
    const float* b = world(target);
    const float dx = b[12] - a[12], dz = b[14] - a[14];
    *dy = b[13] - a[13];
    *d3 = sqrtf(dx * dx + *dy * *dy + dz * dz);
    if (!_finite(*d3) || !_finite(*dy)) return TG_NATIVE;
    if (*d3 <= TG_RANGE && fabsf(*dy) <= TG_HEIGHT) return TG_GO;
    if (now - s->since < TG_APPROACH_MS) return TG_APPROACH;
    return *(int*)(actor + 0x55c) >= 20 ? TG_RIFLE : TG_DROP;
}
static void __fastcall TgSpecial(BYTE* ai, void*)
{
    const TgSpecial_t original = (TgSpecial_t)(tgModule + 0x601f0);
    int plan = TG_NATIVE;
    TgState* s = NULL;
    BYTE* actor = NULL;
    BYTE* foe = NULL;
    float d3 = 0, dy = 0;
    char foeDef[512] = "";
    __try {
        if (tgEnabled && GetModuleHandleW(battleModule) == (HMODULE)tgModule && *(void**)ai == tgModule + 0xe80d4) {
            actor = *(BYTE**)(ai + 4);
            if (actor && ImDefIn(actor, "/character/mm/") && TgNamesOk(ai)) {
                s = TgSlot(ai);
                plan = TgPlan(ai, actor, s, &d3, &dy, &foe);
                if (foe) ImDefPath(foe, foeDef, sizeof foeDef);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { plan = TG_NATIVE; s = NULL; }
    const DWORD now = GetTickCount();
    switch (plan) {
    case TG_INIT: {
        const bool again = s->phase == 1 && s->since && s->lastGap < 400;   // restarted straight away: the same attempt
        const int action = *(int*)(ai + 0x10), seq = *(int*)(ai + 0x14);
        original(ai);                          // native init: reset, substate 1
        if (again) {
            ++s->reinits;
            if (now - s->lastReinitLog >= 500) {
                s->lastReinitLog = now;
                BYTE* mo = *(BYTE**)(actor + 0x398);
                Log("MM_TACKLE_GATE reinit t=%lu side=%d count=%d action=%d seq=%d phase=%d motion=%.24s ai60=%.24s react=%d timer=%.2f waited=%lu",
                    now, *(int*)(actor + 0x724), s->reinits, action, seq, *(int*)(ai + 0xc), mo ? *(const char**)mo : "",
                    (const char*)(ai + 0x60), *(int*)(ai + 0x2a0), *(float*)(ai + 0x334), now - s->since);
            }
        } else { s->phase = 1; s->since = now; s->held = false; s->firstDist = 0; s->reinits = 0; }
        s->lastTrace = 0;
        return;
    }
    case TG_WAIT:
        return;                                // hit / held / guarding: nothing this tick, the native special waits too
    case TG_GO: {
        // 11-05 23:3x (found by Astra/Codex, user 「시험해 줘」 for every Lord Byakhee fight): the reset (vtable[16], 0x61ce0)
        // writes request 0/0 and the dispatch flag +0x2c = 1; calling 0x601f0 in the same tick cleared +0x2c before 0x64b20
        // could send that STOP - FORWARD_G stayed on and 0x601f0 waited for an idle that never came (scene 547: Whateley,
        // 75 s of walking, no attack). The reset tick now ends there; the next tick (held off) asks the tackle once idle.
        if (s->held) {
            ((TgReset_t)(*(void***)ai)[16])(ai); s->held = false;   // stop the approach's move codes
            Log("MM_TACKLE_GATE stop t=%lu side=%d d3=%.0f dy=%.0f waited=%lu: approach over - native STOP first, the tackle next tick",
                now, *(int*)(actor + 0x724), d3, dy, now - s->since);
            return;
        }
        original(ai);                          // native request once idle (it waits by itself)
        if (*(int*)(ai + 0x1c) == 2) {
            s->phase = 2;
            const char* foeName = strrchr(foeDef, '/');
            Log("MM_TACKLE_GATE go t=%lu side=%d d3=%.0f dy=%.0f waited=%lu approach=%d first=%.0f mp=%d hp=%d enemy=%.40s",
                now, *(int*)(actor + 0x724), d3, dy, now - s->since, (int)(s->firstDist > 0), s->firstDist,
                *(int*)(actor + 0x55c), *(int*)(actor + 0x558), foeName ? foeName + 1 : foeDef);
        }
        return;
    }
    case TG_APPROACH: {
        float buf[4] = {0, 0, 0, 1};
        const float* pos = ((TgPos_t)(tgModule + 0x61d00))(ai, buf);
        ((MmApproach_t)(*(void***)ai)[15])(ai, pos ? pos : buf);
        if (!s->held) {
            if (s->firstDist <= 0) s->firstDist = d3;
            s->held = true;
            Log("MM_TACKLE_GATE hold t=%lu side=%d d3=%.0f dy=%.0f (range %.0f/%.0f) - native approach first, mp=%d",
                now, *(int*)(actor + 0x724), d3, dy, TG_RANGE, TG_HEIGHT, *(int*)(actor + 0x55c));
        } else if (now - s->lastTrace >= 500) {
            BYTE* mo = *(BYTE**)(actor + 0x398);
            Log("MM_TACKLE_GATE approach t=%lu side=%d d3=%.0f dy=%.0f waited=%lu motion=%.24s",
                now, *(int*)(actor + 0x724), d3, dy, now - s->since, mo ? *(const char**)mo : "");
        }
        if (now - s->lastTrace >= 500) s->lastTrace = now;
        return;
    }
    case TG_RIFLE:
        // The native tree's other branch at this distance (sequence 8: RIFLE1, then sequence 5).
        ((TgReset_t)(*(void***)ai)[16])(ai);
        *(int*)(ai + 0x10) = 7; *(int*)(ai + 0x14) = 5; *(int*)(ai + 0x1c) = 0;
        ((TgAttack_t)(*(void***)ai)[4])(ai, 7);   // native attack init: substate 1, requested by the next tick
        Log("MM_TACKLE_GATE fallback=RIFLE1 t=%lu side=%d d3=%.0f dy=%.0f waited=%lu first=%.0f mp=%d",
            now, *(int*)(actor + 0x724), d3, dy, now - s->since, s->firstDist, *(int*)(actor + 0x55c));
        ZeroMemory(s, sizeof *s);
        return;
    case TG_DROP:
        ((TgReset_t)(*(void***)ai)[16])(ai);
        *(int*)(ai + 0x1c) = 0;                // the native sequence goes on (10 -> wait -> 5)
        Log("MM_TACKLE_GATE fallback=drop t=%lu side=%d d3=%.0f dy=%.0f waited=%lu first=%.0f mp=%d",
            now, *(int*)(actor + 0x724), d3, dy, now - s->since, s->firstDist, *(int*)(actor + 0x55c));
        ZeroMemory(s, sizeof *s);
        return;
    default:
        original(ai);
        if (s && *(int*)(ai + 0x1c) == 0) s->phase = 0;
        return;
    }
}
// A battle can reuse the native module: put the original CALL back before the other installers check it.
static void ResetMmTackleGate(HMODULE module)
{
    BYTE* b = (BYTE*)module;
    tgEnabled = false;
    ZeroMemory(tgStates, sizeof tgStates);
    if (!b) return;
    BYTE* call = b + 0x6078f;
    LONG relative;
    memcpy(&relative, call + 1, sizeof relative);
    if (*call == 0xe8 && call + 5 + relative == (BYTE*)TgSpecial)
        MmRedirectCall(call, (BYTE*)TgSpecial, b + 0x601f0, true);
}
static void ApplyMmTackleGate(HMODULE module, bool duel)
{
    tgEnabled = false;
    ZeroMemory(tgStates, sizeof tgStates);
    if (!duel || !module) return;
    BYTE* b = (BYTE*)module;
    const BYTE head[] = {0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x1c, 0x57, 0x33, 0xff};   // 0x601f0 special
    const BYTE request[] = {0x6a, 0x04, 0x8b, 0xce, 0xe8};                         // 0x60262 push 4 (B_ATTACK); call
    const BYTE dispatch[] = {0x8b, 0xcb, 0xe8};                                     // 0x6078d action 5 -> special
    if (memcmp(b + 0x601f0, head, sizeof head) || memcmp(b + 0x60262, request, sizeof request) ||
        memcmp(b + 0x6078d, dispatch, sizeof dispatch)) {
        Log("MM_TACKLE_GATE disabled: native bytes differ"); return;
    }
    if (!MmRedirectCall(b + 0x6078f, b + 0x601f0, (void*)TgSpecial, false)) {
        Log("MM_TACKLE_GATE skipped: the action-5 call is held by another hook"); return;
    }
    tgModule = b;
    if (MmRedirectCall(b + 0x6078f, b + 0x601f0, (void*)TgSpecial, true)) {
        tgEnabled = true;
        Log("MM_TACKLE_GATE enabled base=%p range=%.0f height=%.0f approach=%lums fallback=RIFLE1", b, TG_RANGE, TG_HEIGHT, TG_APPROACH_MS);
    } else Log("MM_TACKLE_GATE disabled: protection failure");
}
