// DUEL: PLAYER1 handed between the person and the AI in the middle of a fight (11-03).
// user 11-03 「자동전투 해제/실행을 듀얼 모드, 프리 파이트 모드, 전투 중에도 설정하게 하려고 해」 → DUEL 판 중간은 「그 자리에서 바로」.
// demonbane.dll sets each robot of the scene up in 0x3abd0 (one call, from 0x28e69): a PLAYER entry (attribute bit 1) gets
// the key device +0x700 (0x304d0, <kcf>), key timing +0x708 (0x305f0, <ktm>) and move table +0x704 (0x30560, <waz>); a
// NONPLAYER one (bit 2) gets one of fifteen AI objects (switch on the entry's AI type +0x68, jump table 0x3c70c) whose
// CAiBase (+0x714) goes into the manager's AI list (+0x48); at the end of the setup every AI of that list is registered with
// the frame updater [0x1011e3b8] (0x41c10(item, 0, 0)), which is what runs them. A robot reads keys only through
// +0x700/+0x704 (0x38550, whatever its attribute), and while it has those an AI's requests come to nothing (11-03 06:54: the
// D Blood AI went on running, yet only the test keys moved the robot). So when PLAYER1 changes hands it gets the half it
// lacks - an AI like its auto copy's (duel_auto.tsv; in the list and registered, as the setup does) or the three key objects
// of its character - and the AI is let in or kept out only by the key pointers: aside (NULL, as on a NONPLAYER robot) while
// the AI plays, back while the person does. The attribute (+0x720) stays the scene's, so its ISPLAYER / ISNONPLAYER lines
// (facing) and damage rows go on as in that scene. Hand-overs happen only while the fight is paused (the pause menu).
#pragma once
struct HoAiKind { const char* name; DWORD caseAt, size, ctor; int kind; };   // kind = what the case writes to robot+0xb70
static const HoAiKind kHoAi[15] = {
    {"LIVERREGUIS",   0x3b451, 0x720, 0x65510, 0}, {"DEMONBLOOD", 0x3b4c7, 0x724, 0x60f80, 1},
    {"KRAKEN",        0x3b543, 0x71c, 0x60840, 2}, {"LOAD",       0x3b5c4, 0x720, 0x5fb40, 3},
    {"LIVERSHIELD",   0x3b640, 0x71c, 0x5f090, 0}, {"WAIT",       0x3b6b6, 0x734, 0x5df20, 4},
    {"LEGACY",        0x3b732, 0x718, 0x5d460, 5}, {"BERUZE",     0x3b7ae, 0x730, 0x5c360, 6},
    {"OHGA",          0x3b82a, 0x718, 0x5b580, 7}, {"SAIKURA",    0x3b8a6, 0x73c, 0x5a780, 8},
    {"NAMELESSONE",   0x3b922, 0x718, 0x59990, 9}, {"DEMONTWOSWORD", 0x3b99e, 0x724, 0x58900, 10},
    {"CLOCKWORK2",    0x3ba1a, 0x718, 0x57f10, 11}, {"CLOCKWORK3", 0x3ba96, 0x71c, 0x57790, 13},
    {"DESTROYROBO",   0x3bb12, 0x71c, 0x56cb0, 12}};
static BYTE* hoModule;
static LONG hoArmScene = -1;
static int hoArmType = -1;                // this fight's AI type for PLAYER1 (-1: no hand-over)
static BYTE* hoManager;
static BYTE* hoActor;                     // PLAYER1 of this fight, once it has both halves
static BYTE* hoAi;
static char hoName[64];                  // PLAYER1's scene entry (its character)
static void* hoKeys[3];                   // +0x700 +0x704 +0x708 while the AI plays
static bool hoInList, hoOwnAi;
static int hoOldKind, hoOldAttr;
static DWORD hoTestMs, hoArmTick, hoTestLast;
static int hoAttrAi;                     // test: autobattle.ini handover_attr (0 keep, 2 NONPLAYER, 3 both)

// duel_auto.tsv: manual scene -> its auto copy's AI (the fourth column), kept as type + 1
static void HoNoteAi(int scene, const char* name)
{
    if (scene < 0 || scene >= 1000) return;
    for (int k = 0; k < 15; ++k)
        if (!strcmp(name, kHoAi[k].name)) { hoAiOf[scene] = (signed char)(k + 1); return; }
}

// fight start (BeforeFight, before the AI-copy swap): the source scene decides whether PLAYER1 can change hands
static void HoArm(LONG scene)
{
    hoArmScene = scene;
    hoArmType = (scene >= 0 && scene < 1000 && hoAiOf[scene] > 0) ? hoAiOf[scene] - 1 : -1;
    hoActor = hoAi = hoManager = NULL;
    hoKeys[0] = hoKeys[1] = hoKeys[2] = NULL; hoName[0] = 0;
    hoMode = -1; hoInList = hoOwnAi = false;
    hoArmTick = GetTickCount(); hoTestLast = 0;
}

static const char* HoStr(BYTE* s) { return *(DWORD*)(s + 0x18) >= 16 ? *(const char**)(s + 4) : (const char*)(s + 4); }

static BYTE** HoListBegin() { return *(BYTE***)(hoManager + 0x48 + 4); }
static BYTE** HoListEnd() { return *(BYTE***)(hoManager + 0x48 + 8); }

static void HoClearRequests()
{
    for (int i = 0; i < 3; ++i) { BYTE** p = (BYTE**)(hoActor + 0x378 + i * 8); *p = NULL; *(DWORD*)(p + 1) = 0; }
    for (int i = 0; i < 5; ++i) { BYTE** p = (BYTE**)(hoActor + 0x2b8 + i * 8); *p = NULL; *(DWORD*)(p + 1) = 0; }
}

__declspec(noinline) static void HoFind(BYTE* manager)
{
    BYTE* data = *(BYTE**)(manager + 4);
    BYTE** it = data ? *(BYTE***)(data + 0x28) : NULL;
    BYTE** end = data ? *(BYTE***)(data + 0x2c) : NULL;
    BYTE* entry = NULL;
    BYTE* actor = NULL;
    for (; it && it < end; ++it)
    {
        BYTE* e = *it;
        if (!e || *(int*)(e + 0x40) != 0) continue;           // group(): PLAYER1 0
        BYTE* a = *(BYTE**)(e + 0x64);
        if (!a || (*(int*)(a + 0x720) != 1 && *(int*)(a + 0x720) != 2)) continue;
        entry = e; actor = a; break;
    }
    if (!actor) { Log("HANDOVER off: no PLAYER1 robot in scene %ld", hoArmScene); return; }
    hoManager = manager;
    strcpy_s(hoName, HoStr(entry + 4));
    if (*(int*)(actor + 0x720) == 1)                           // the person's scene: the AI is made when it takes over
    {
        hoActor = actor; hoMode = 0;
        Log("HANDOVER ready: scene %ld, the person plays PLAYER1 (%s); a %s AI takes over on request", hoArmScene, hoName, kHoAi[hoArmType].name);
        return;
    }
    for (BYTE** p = HoListBegin(); p && p < HoListEnd(); ++p)  // the AI's scene: its own AI is in the list
        if (*p && *(BYTE**)(*p - 0x714 + 4) == actor) { hoAi = *p - 0x714; break; }
    if (!hoAi) { Log("HANDOVER off: PLAYER1's AI is not in the list (scene %ld)", hoArmScene); return; }
    hoActor = actor; hoInList = true; hoMode = 1;
    Log("HANDOVER ready: scene %ld, the %s AI plays PLAYER1 (%s); the keys are given on request", hoArmScene, kHoAi[hoArmType].name, hoName);
}

// an AI like PLAYER1's auto copy's, outside the list (the setup's case for that type, without the list)
__declspec(noinline) static bool HoMakeAi()
{
    const HoAiKind& k = kHoAi[hoArmType];
    typedef BYTE*(__cdecl* NewFn)(size_t);
    typedef BYTE*(__thiscall* CtorFn)(BYTE*);
    BYTE* ai = ((NewFn)(hoModule + 0xbb5ca))(k.size);
    if (!ai) { Log("HANDOVER: no memory for the %s AI", k.name); return false; }
    ((CtorFn)(hoModule + k.ctor))(ai);
    *(BYTE**)(ai + 4) = hoActor;
    typedef void(__thiscall* AddFn)(BYTE*, void**);
    typedef BYTE*(__thiscall* RunFn)(BYTE*, void*, int, int);
    void* item = ai + 0x714;
    ((AddFn)(hoModule + 0x3ab80))(hoManager + 0x48, &item);   // the engine's own AIs: freed with the scene
    BYTE* updater = *(BYTE**)(hoModule + 0x11e3b8);
    BYTE* node = updater ? ((RunFn)(hoModule + 0x41c10))(updater, item, 0, 0) : NULL;
    hoAi = ai; hoOwnAi = true; hoInList = true;
    Log("HANDOVER: %s AI made for PLAYER1 (%p, update node %p)", k.name, ai, node);
    return node != NULL;
}

// the key device, key timing and move table of PLAYER1's character (the setup's PLAYER branch)
__declspec(noinline) static bool HoMakeKeys()
{
    typedef BYTE*(__thiscall* KindFn)(BYTE*, const char*);
    BYTE* kinds = *(BYTE**)(hoModule + 0x11e3d8);
    BYTE* kind = kinds ? ((KindFn)(hoModule + 0x48540))(kinds, hoName) : NULL;
    if (!kind) { Log("HANDOVER: no character for %s", hoName); return false; }
    const char* base = HoStr(kind + 0x1c);
    char kcf[MAX_PATH], ktm[MAX_PATH], waz[MAX_PATH];
    sprintf_s(kcf, "%s.kcf", *HoStr(kind + 0x38) ? HoStr(kind + 0x38) : base);
    sprintf_s(ktm, "%s.ktm", *HoStr(kind + 0x54) ? HoStr(kind + 0x54) : base);
    sprintf_s(waz, "%s.waz", *HoStr(kind + 0x70) ? HoStr(kind + 0x70) : base);
    typedef void(__thiscall* InputFn)(BYTE*, const char*, int);
    typedef bool(__thiscall* TimingFn)(BYTE*, const char*);
    typedef void(__thiscall* MovesFn)(BYTE*, const char*);
    ((InputFn)(hoModule + 0x304d0))(hoActor, kcf, 0);
    bool timing = ((TimingFn)(hoModule + 0x305f0))(hoActor, ktm);
    ((MovesFn)(hoModule + 0x30560))(hoActor, waz);
    Log("HANDOVER: keys made for PLAYER1 (%s, %s%s, %s; device %p moves %p)", kcf, ktm, timing ? "" : " - none", waz,
        *(void**)(hoActor + 0x700), *(void**)(hoActor + 0x704));
    return *(void**)(hoActor + 0x700) && *(void**)(hoActor + 0x704);
}

// 0x3abd0 takes two stack arguments (ret 8; 0x28e57 pushes the second, 1, before the first) - with one, the fight stood
// at START for good (11-03 06:35-06:52)
typedef void(__thiscall* HoSetupFn)(BYTE*, void*, int);
static void __fastcall HoSetup(BYTE* manager, void*, void* arg, int arg2)
{
    ((HoSetupFn)(hoModule + 0x3abd0))(manager, arg, arg2);
    if (hoArmType < 0) return;
    __try { HoFind(manager); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("HANDOVER off: a fault while finding PLAYER1 (scene %ld)", hoArmScene);
        hoActor = NULL; hoMode = -1;
    }
}

// mode 1: the AI plays PLAYER1, 0: the person. Only while the fight is paused.
__declspec(noinline) static bool HoDecided()                  // a robot down: the win / loss sequence is the scene's own (11-03 07:03 - handing
{                                        // PLAYER1 back after PWIN held the fight past its end)
    typedef BYTE*(__thiscall* ActorFn)(BYTE*, int);
    BYTE* manager = *(BYTE**)(hoModule + 0x11e3b0);
    for (int i = 0; manager && i < 2; ++i)
    {
        BYTE* a = ((ActorFn)(hoModule + 0x39dc0))(manager, i);
        if (a && *(int*)(a + 0x558) <= 0) return true;
    }
    return false;
}

__declspec(noinline) static bool HoSwitch(int mode, const char* why)
{
    if (!hoActor || hoMode < 0 || mode == hoMode) return false;
    if (HoDecided()) { Log("HANDOVER refused (%s): the fight is decided", why); return false; }
    __try
    {
        if (mode == 1)
        {
            if (!hoAi && !HoMakeAi()) return false;
            for (int i = 0; i < 3; ++i) { hoKeys[i] = *(void**)(hoActor + 0x700 + 4 * i); *(void**)(hoActor + 0x700 + 4 * i) = NULL; }
            if (hoOwnAi)
            {
                hoOldKind = *(int*)(hoActor + 0xb70); *(int*)(hoActor + 0xb70) = kHoAi[hoArmType].kind;
                hoOldAttr = *(int*)(hoActor + 0x720); if (hoAttrAi) *(int*)(hoActor + 0x720) = hoAttrAi;
            }
        }
        else
        {
            if (hoKeys[0]) for (int i = 0; i < 3; ++i) *(void**)(hoActor + 0x700 + 4 * i) = hoKeys[i];
            else if (!*(void**)(hoActor + 0x700) && !HoMakeKeys()) Log("HANDOVER: PLAYER1 has no keys - it stands still");
            if (hoOwnAi) { *(int*)(hoActor + 0xb70) = hoOldKind; *(int*)(hoActor + 0x720) = hoOldAttr; }
        }
        HoClearRequests();
        hoMode = mode;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("HANDOVER: a fault while handing PLAYER1 over (%s) - hand-over off for this fight", why);
        hoMode = -1;
        return false;
    }
    Log("HANDOVER t=%lu PLAYER1 -> %s (%s) scene=%ld hp=%d mp=%d attribute=%d", GetTickCount() - hoArmTick, mode ? "the AI" : "the person", why,
        hoArmScene, *(int*)(hoActor + 0x558), *(int*)(hoActor + 0x3c8 + 4 * 101), *(int*)(hoActor + 0x720));
    return true;
}

// test only (autobattle.ini handover_test_ms): each pause after that time hands PLAYER1 over and closes the menu
static bool HoPauseTick(BYTE* menu)
{
    if (!hoTestMs || hoMode < 0 || *(int*)(menu + 0x144) != 1) return false;
    DWORD now = GetTickCount() - hoArmTick;
    if (now < hoTestMs || (hoTestLast && now - hoTestLast < 2000)) return false;
    hoTestLast = now;
    HoSwitch(1 - hoMode, "test");
    return true;
}

// the pause menu's 자동 전투 row (pause_menu.h), for this fight's mode: -1 when it cannot switch now; *shown 1 = the AI plays
// it now (the row says 끄기), 0 = the person does (켜기)
__declspec(noinline) static int FightAutoState(int* shown)
{
    int state;
    if (fightKind == 2 && duelAgentScene) state = duelAgentFight && (!shared || stoppedRun != shared->runs) ? 1 : 0;   // 11-05 the story agent
    else if (fightKind == 2) state = (hoModule && hoMode >= 0 && !HoDecided()) ? hoMode : -1;
    else state = AgentMode() && (!shared || stoppedRun != shared->runs) ? 1 : 0;
    *shown = state >= 0 ? state : (fightKind == 2 ? duelAutoOn != 0 : AgentMode());
    return state;
}

// the row chosen: this mode's switch (kept in autobattle.ini) and, at once, this fight
__declspec(noinline) static void ToggleFightAuto()
{
    int shown = 0, state = FightAutoState(&shown);
    if (state < 0) { Log("자동 전투 row: this fight cannot switch now (kind %d)", fightKind); return; }
    if (fightKind == 2 && duelAgentScene)            // 11-05 Ambrosius vs Lord Byakhee: the story agent on / off, the person's keys otherwise
    {
        duelAgentFight = state == 0; stoppedRun = 0; SetDuelAuto(state == 0);
        Log("자동 전투 row: the story agent %s PLAYER1 (Ambrosius vs Lord Byakhee)", duelAgentFight ? "plays" : "lets go of");
    }
    else if (fightKind == 2) { if (HoSwitch(1 - state, "pause menu")) SetDuelAuto(state == 0); }
    else if (fightKind == 1) SetFreeAuto(state == 0);
    else SetAutoBattle(state == 0);
}

// ---- while the person plays PLAYER1 its AI rests entirely (11-03 07:2x batch): the game's own AIs come to nothing then (the
// robot has keys), but the proxy's own hero AIs (HM, A2, F2) went on moving it. Every AI's per-frame call (slot 0 of the
// fifteen CAiBase tables - the ones hard_v11.h chains) goes through HoAiTick, outermost: installed last at each fight start and
// taken out first at the next (hard_v11 and the hero AIs check their slots before chaining).
struct HoSlot { DWORD at; MmTick_t previous; };
static HoSlot hoSlots[15] = {{0xe7818}, {0xe78cc}, {0xe79b8}, {0xe7ac8}, {0xe7b98}, {0xe7c50}, {0xe7d4c}, {0xe7df0},
                             {0xe7ef0}, {0xe7f8c}, {0xe807c}, {0xe80c8}, {0xe8158}, {0xe81c8}, {0xe867c}};
static BYTE* hoSlotModule;
static void __fastcall HoAiTick(BYTE* secondary, void*, DWORD dt, DWORD arg)
{
    MmTick_t previous = NULL;
    for (int i = 0; i < 15; ++i)
        if (*(void**)secondary == hoSlotModule + hoSlots[i].at) { previous = hoSlots[i].previous; break; }
    if (!previous) return;
    if (hoMode == 0 && hoActor && *(BYTE**)(secondary - 0x714 + 4) == hoActor) return;
    previous(secondary, dt, arg);
}

static void RestoreHandoverSlots(HMODULE module)
{
    if (!hoSlotModule || hoSlotModule != (BYTE*)module) return;
    for (int i = 0; i < 15; ++i)
    {
        void** slot = (void**)(hoSlotModule + hoSlots[i].at);
        if (*slot == (void*)HoAiTick && hoSlots[i].previous) MmSwapSlot(slot, (void*)HoAiTick, (void*)hoSlots[i].previous);
    }
}

static bool InstallHandoverSlots(BYTE* m)
{
    hoSlotModule = m;
    for (int i = 0; i < 15; ++i)
    {
        void** slot = (void**)(m + hoSlots[i].at);
        void* old = *slot;
        if (old == (void*)HoAiTick) continue;
        hoSlots[i].previous = (MmTick_t)old;
        if (!MmSwapSlot(slot, old, (void*)HoAiTick))
        {
            Log("HANDOVER: AI slot %lx not written - the hand-over is off for this fight", hoSlots[i].at);
            RestoreHandoverSlots((HMODULE)m);
            return false;
        }
    }
    return true;
}

// ---- the AI made for a person's scene drives that scene's definition, which lacks the names a borrowed AI asks for (the AI
// copies add them: duel_ai_alias.py, 「// DUEL 10-19: AI(X) = Y」) - a missing name holds the robot where it is (11-03 07:0x
// batch: the heroes stood or only walked). autobattle\duel_ai_alias.tsv (tools\build_ai_alias.py, from those copies; EMPTY ->
// CHARGE as the copies' ai_charge): the names are put right at the one place an AI asks for a motion by name (0x62000 ->
// 0x351b0 at 0x6201a), for that made AI only.
struct HoAlias { short scene; char from[28], to[28]; };
static HoAlias* hoAlias;
static int hoAliasN;
static void LoadHoAliases()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\duel_ai_alias.tsv", abDir);
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"rb") || !f) { Log("HANDOVER: no duel_ai_alias.tsv - the made AIs ask by their own names"); return; }
    int cap = 4096;
    hoAlias = (HoAlias*)calloc(cap, sizeof(HoAlias));
    char line[160];
    while (hoAlias && hoAliasN < cap && fgets(line, sizeof line, f))
    {
        int scene = -1;
        HoAlias a = {};
        if (line[0] == '#' || sscanf_s(line, "%d\t%27s\t%27s", &scene, a.from, (unsigned)sizeof a.from, a.to, (unsigned)sizeof a.to) != 3) continue;
        if (scene < 0 || scene >= 1000) continue;
        a.scene = (short)scene;
        hoAlias[hoAliasN++] = a;
    }
    fclose(f);
    Log("HANDOVER: %d AI name aliases for the person's scenes", hoAliasN);
}

typedef bool(__thiscall* HoRequestFn)(BYTE*, const char*, int);
static int hoRequestLogs;                // autobattle.ini handover_trace: the made AI's first asks, with the answer
static bool __fastcall HoRequest(BYTE* actor, void*, const char* name, int index)
{
    if (!(actor == hoActor && hoOwnAi && hoMode == 1 && name)) return AiAsk(hoModule, actor, name, index);   // 11-03 v13 the swap
    const char* asked = name;
    for (int i = 0; i < hoAliasN; ++i)
        if (hoAlias[i].scene == hoArmScene && !strcmp(hoAlias[i].from, name)) { name = hoAlias[i].to; break; }
    bool ok = AiAsk(hoModule, actor, name, index);
    if (hoRequestLogs > 0 && index == 0) { --hoRequestLogs; Log("HANDOVER ask %s%s%s -> %d", asked, name != asked ? " = " : "", name != asked ? name : "", (int)ok); }
    return ok;
}

// a call at `at` to `native` (or already to `hook`) becomes a call to `hook`
static bool HoCall(BYTE* at, BYTE* native, void* hook)
{
    BYTE* target = at[0] == 0xe8 ? at + 5 + *(LONG*)(at + 1) : NULL;
    if (target == (BYTE*)hook) return true;
    if (target != native) return false;
    BYTE call[5] = {0xe8};
    *(LONG*)(call + 1) = (LONG)((BYTE*)hook - (at + 5));
    return WriteCode(at, call, 5);
}

// each fight start, after every other hook (BeforeFight): the setup call (0x28e69), the AI's ask-by-name (0x6201a) and the
// fifteen AI slots
static void InstallHandover(HMODULE module)
{
    if (!module) return;
    BYTE* m = (BYTE*)module;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    hoTestMs = GetPrivateProfileIntW(L"autobattle", L"handover_test_ms", 0, ini);
    hoAttrAi = GetPrivateProfileIntW(L"autobattle", L"handover_attr", 0, ini);
    hoRequestLogs = GetPrivateProfileIntW(L"autobattle", L"handover_trace", 0, ini) ? 80 : 0;
    if (!GetPrivateProfileIntW(L"autobattle", L"handover", 1, ini)) { Log("HANDOVER off (autobattle.ini handover=0)"); return; }
    for (int k = 0; k < 15; ++k)                               // this build's AI names and setup cases
    {
        if (strcmp((const char*)(m + 0x107e9c + 36 * k), kHoAi[k].name) || *(int*)(m + 0x107e98 + 36 * k) != k ||
            *(DWORD*)(m + 0x3c70c + 4 * k) != (DWORD)(UINT_PTR)(m + kHoAi[k].caseAt))
        { Log("HANDOVER disabled: AI table differs at %d", k); return; }
    }
    if (!HoCall(m + 0x28e69, m + 0x3abd0, (void*)HoSetup)) { Log("HANDOVER disabled: setup call differs"); return; }
    if (!HoCall(m + 0x6201a, m + 0x351b0, (void*)HoRequest)) Log("HANDOVER: the AI's ask-by-name call differs - no name aliases");
    hoModule = m;
    if (!InstallHandoverSlots(m)) { hoModule = NULL; return; }
    Log("HANDOVER installed: PLAYER1 of a DUEL fight can change hands%s", hoTestMs ? " (test hand-over armed)" : "");
}
