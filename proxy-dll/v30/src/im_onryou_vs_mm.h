// 11-03 사용자 「로드 비야키랑 벨제뷰트 싸워봤는데 벨제뷰트가 후반에 폭식 돼지를 쓰거든? 로드 비야키 상대로는 그거 대신 원령 주탄을
// 쓰게 해줄래?」 - in a DUEL fight Beelzebub's AI asks for ONRYOU (원령 주탄, 80 MP) where it asked for TAIZAI (폭식 대죄, 70 MP)
// when the robot in front of it is Lord Byakhee, on either side. Only an AI asks by name - a person playing Beelzebub keeps
// TAIZAI on the keys. The native AI asks TAIZAI as its last stand (own HP about 19%) at any MP, and the ask (0x307f0) only
// finds the motion by name - TAIZAI started at 0 MP and the gauge stays at 0 (11-03 test: three TAIZAI, none hit the flying
// Lord Byakhee). So ONRYOU is asked at any MP too, at the same moments. A first build refused below 80 MP: no ONRYOU at all.
// The ways in: the native AI's ask-by-name (0x62000 -> 0x351b0 at 0x6201a: HoRequest, ImMixRequest or the hook here, whichever
// sits there, all ask through AiAsk) and the hard mode's own chooser for PLAYER2 (HvThink: the hvSkills rows with flag 5 / 6).
#pragma once
static bool __fastcall HoRequest(BYTE* actor, void*, const char* name, int index);    // duel_handover.h
static bool __fastcall ImMixRequest(BYTE* a, void*, const char* name, int slot);      // tiberius_mix.h
static void __fastcall HvUpdate(BYTE* a, void*);                                      // hard_v11.h
static BYTE* imOnryouModule;         // this fight's battle module while it is a DUEL fight (InstallImOnryou), NULL otherwise
static int imOnryouLogs;             // the first asks of a fight go to the log
static int aiAskTrace;               // autobattle.ini ai_ask_trace=N (test only): the first N asks of a DUEL fight, any AI
static DWORD aiAskTraceStart;

// a robot's definition path in lower case with forward slashes - "" when there is none
static void ImDefPath(BYTE* a, char* out, size_t n)
{
    out[0] = 0;
    BYTE* d = a ? *(BYTE**)(a + 0x16c) : NULL;
    const char* p = d ? *(const char**)(d + 0x34) : NULL;
    if (!p || strcpy_s(out, n, p)) { out[0] = 0; return; }
    _strlwr_s(out, n);
    for (char* c = out; *c; ++c) if (*c == '\\') *c = '/';
}
static bool ImDefIn(BYTE* a, const char* dir)       // "/character/mm/": every DUEL copy of that robot lives there
{
    char path[512];
    ImDefPath(a, path, sizeof path);
    return strstr(path, dir) != NULL;
}

// Beelzebub with Lord Byakhee as its target
static bool ImVsMm(BYTE* a)
{
    if (!imOnryouModule || !a) return false;
    __try
    {
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        if (!manager || !ImDefIn(a, "/character/im/")) return false;
        BYTE* target = ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, a);
        return target && ImDefIn(target, "/character/mm/");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- 11-03 사용자 「공중전은 그냥 직접 타격이 가능할거 같은데 그냥 접근만 하더라고 체크좀」: the air Demonbane (AM014) borrows the
// D-Two-Sword AI, a ground AI. Against Nero it closed in flat (FORWARD_G = FORWARD_A keeps its height) to right over Nero's skirt
// (xz about 20, Nero's lock node 250 above its own) and, never in reach, never asked for an attack (11-03 trace: 591 asks, all
// moves). In a DUEL fight an AI-driven air Demonbane (a definition under /character/am/ with UP_A - the ground one has none)
// facing Nero (/character/qm/) gets UP_A / DOWN_A for its move asks while the two lock nodes are more than kA2NeroDy apart in
// height. Coming level was not enough (second trace: the lock nodes were mostly level, still no attack ask - the borrowed AI
// measures to Nero's root, 300 below her body, and that never comes into reach; 9-second picture: right at Nero's chest).
// So once the lock nodes are within kA2NeroReach, a move ask becomes the scimitar combo (S_COMBO1 -> 2 -> 3 by the motion it
// is in; the copies of KEN_COMBO1..3 the alias table already put in the definition).
static const float kA2NeroDy = 120.0f;
static const float kA2NeroReach = 300.0f;
static int a2NeroLogs;
static bool ImHasMotion(BYTE* a, const char* name)     // the definition's motion list, walked as 0x307f0 does
{
    BYTE* d = *(BYTE**)(a + 0x16c);
    int n = 0;
    for (BYTE* m = d ? *(BYTE**)d : NULL; m && n < 1000; m = *(BYTE**)(m + 0x7c), ++n)
        if (!_stricmp(*(const char**)m, name)) return true;
    return false;
}
static const char* A2NeroHeight(BYTE* module, BYTE* a, const char* name)   // the move to ask instead, or NULL
{
    static const char* const moves[] = {"FORWARD_G", "FORWARD_B", "LFORWARD_G", "RFORWARD_G", "LFORWARD_B", "RFORWARD_B", "STOP_G"};
    bool move = false;
    for (const char* m : moves) move = move || !_stricmp(name, m);
    if (!move || !imOnryouModule || module != imOnryouModule) return NULL;
    __try
    {
        BYTE* manager = *(BYTE**)(module + 0x11e3b0);
        BYTE* target = manager ? ((MmTarget_t)(module + 0x39d60))(manager, a) : NULL;
        if (!target || !ImDefIn(a, "/character/am/") || !ImDefIn(target, "/character/qm/") || !ImHasMotion(a, "UP_A")) return NULL;
        BYTE* an = *(BYTE**)(a + 0xb84);
        BYTE* bn = *(BYTE**)(target + 0xb84);
        if (!an || !bn) return NULL;
        MmWorld_t world = (MmWorld_t)(module + 0x15930);
        const float* p = world(an);
        const float* q = world(bn);
        float dx = q[12] - p[12], dy = q[13] - p[13], dz = q[14] - p[14];
        float reach = sqrtf(dx * dx + dy * dy + dz * dz);
        if (!_finite(reach)) return NULL;
        const char* to = NULL;
        if (fabsf(dy) > kA2NeroDy) to = dy > 0 ? "UP_A" : "DOWN_A";
        else if (reach < kA2NeroReach)
        {
            BYTE* m = *(BYTE**)(a + 0x398);
            const char* now = m ? *(const char**)m : "";
            to = !_stricmp(now, "S_COMBO1") || !_stricmp(now, "KEN_COMBO1") ? "S_COMBO2"
               : !_stricmp(now, "S_COMBO2") || !_stricmp(now, "KEN_COMBO2") ? "S_COMBO3" : "S_COMBO1";
        }
        if (to && a2NeroLogs > 0)
        {
            --a2NeroLogs;
            Log("A2_NERO side %d asked %s, Nero's lock node %.0f away (%.0f %s) -> %s", *(int*)(a + 0x724), name, reach,
                fabsf(dy), dy > 0 ? "above" : "below", to);
        }
        return to;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

// ---- test only (autobattle.ini ice_trace=1, 11-03 사용자 「칼리굴라의 빙격이 … 로드 비야키 상대일경우 빙격의 위치가 칼리굴라의 머리
// 위가 아니라 엉뚱한데 생기는거 같아」): every 100 ms of AI asks, each live Kraken ice missile (LM044) with its owner's ice_Null
// node and the target's lock node, world positions
static bool iceTrace;
static bool kuzuTestAsk;             // autobattle.ini kuzu_test_ask=1 (test only): the hard PLAYER2 Kraken's A_ARM2 asks become 9HeadDragon
static DWORD iceTraceLast;
typedef BYTE* (__thiscall* ImFindNodeFn)(BYTE*, const char*);
static int iceTraceFrame = -1;                     // the first 40 frames of a shot: every frame an AI asks in
static void IceTrace(BYTE* module, BYTE* asker)
{
    DWORD now = GetTickCount();
    if (!iceTrace) return;
    bool dense = iceTraceFrame >= 0 && iceTraceFrame <= 40;
    if (!dense && now - iceTraceLast < 100) return;
    iceTraceLast = now;
    iceTraceFrame = -1;
    __try
    {
        BYTE* manager = *(BYTE**)(module + 0x11e3b0);
        BYTE* other = manager ? ((MmTarget_t)(module + 0x39d60))(manager, asker) : NULL;
        MmWorld_t world = (MmWorld_t)(module + 0x15930);
        BYTE* robots[2] = {asker, other};
        for (BYTE* owner : robots)
        {
            if (!owner) continue;
            BYTE* mats = *(BYTE**)(owner + 0x6f4);
            BYTE* begin = mats ? *(BYTE**)(mats + 8) : NULL;
            BYTE* end = mats ? *(BYTE**)(mats + 12) : NULL;
            if (!begin || end < begin || (end - begin) % 0x9c || end - begin > 0x9c * 256) continue;
            BYTE* target = ((MmTarget_t)(module + 0x39d60))(manager, owner);
            BYTE* node = ((ImFindNodeFn)(module + 0x15a00))(owner, "ice_Null");
            BYTE* lock = target ? *(BYTE**)(target + 0xb84) : NULL;
            for (BYTE* mat = begin; mat < end; mat += 0x9c)
            {
                BYTE** lo = *(BYTE***)(mat + 0x90);
                BYTE** hi = *(BYTE***)(mat + 0x94);
                if (!lo || hi < lo || hi - lo > 256) continue;
                for (BYTE** p = lo; p < hi; ++p)
                {
                    BYTE* n = *p;
                    if (!n) continue;
                    char path[512];
                    ImDefPath(n, path, sizeof path);
                    BYTE* mo = *(BYTE**)(n + 0x398);
                    if (!strstr(path, "/lm044/") || !mo || *(int*)(mo + 0x74) == 0) continue;
                    static int lastFrame = -1;
                    int frame = *(int*)(mo + 0x54);
                    if (!_stricmp(*(const char**)mo, "SHOOT")) iceTraceFrame = frame;
                    if (frame == lastFrame) continue;
                    lastFrame = frame;
                    const float* i = world(n);
                    const float* o = world(owner);
                    const float* h = node ? world(node) : o;
                    const float* t = lock ? world(lock) : o;
                    Log("ICE_TRACE side %d %s@%d ice %.0f,%.0f,%.0f | ice_Null %.0f,%.0f,%.0f | owner %.0f,%.0f,%.0f | target lock %.0f,%.0f,%.0f",
                        *(int*)(owner + 0x724), *(const char**)mo, *(int*)(mo + 0x54), i[12], i[13], i[14], h[12], h[13], h[14],
                        o[12], o[13], o[14], t[12], t[13], t[14]);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// the same, from the per-actor update (HvUpdate, hard fights): every frame of an ice missile's first 120 frames of each motion,
// with its owner (+0x6f8) and the owner's motion
static void IceTraceActor(BYTE* n)
{
    if (!iceTrace || !imOnryouModule) return;
    __try
    {
        char path[512];
        ImDefPath(n, path, sizeof path);
        if (strstr(path, "/lm046/"))                          // the bind arm (ANA_shot): its tip node "Null" against the target
        {
            BYTE* am = *(BYTE**)(n + 0x398);
            BYTE* armOwner = *(BYTE**)(n + 0x6f8);
            if (!am || !armOwner || *(int*)(am + 0x74) == 0) return;
            BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
            BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, armOwner) : NULL;
            BYTE* tip = ((ImFindNodeFn)(imOnryouModule + 0x15a00))(n, "Null");
            BYTE* lock = target ? *(BYTE**)(target + 0xb84) : NULL;
            MmWorld_t w = (MmWorld_t)(imOnryouModule + 0x15930);
            const float* r = w(n);
            const float* p = tip ? w(tip) : r;
            const float* q = lock ? w(lock) : r;
            const float* g = target ? w(target) : r;
            const float* raw = (const float*)(n + 4);
            const float* k = w(armOwner);                     // Kraken's root and axes: the arm's path in its own frame
            BYTE* km = *(BYTE**)(armOwner + 0x398);
            Log("ARM_FRAME %s@%d root %.0f,%.0f,%.0f tip %.0f,%.0f,%.0f | target lock %.0f,%.0f,%.0f root %.0f,%.0f,%.0f | +4 %.0f,%.0f,%.0f +14 %.3f,%.3f,%.3f +94 %02x parent %p | kraken %.0f,%.0f,%.0f x %.2f,%.2f,%.2f z %.2f,%.2f,%.2f %s@%d",
                *(const char**)am, *(int*)(am + 0x54), r[12], r[13], r[14], p[12], p[13], p[14], q[12], q[13], q[14], g[12], g[13], g[14],
                raw[0], raw[1], raw[2], raw[4], raw[5], raw[6], n[0x94], *(void**)(n + 0x98), k[12], k[13], k[14], k[0], k[1], k[2],
                k[8], k[9], k[10], km ? *(const char**)km : "-", km ? *(int*)(km + 0x54) : -1);
            return;
        }
        if (strstr(path, "/lm047/"))                          // the 9-head dragon arm (LM047 ANA_shot)
        {
            BYTE* am = *(BYTE**)(n + 0x398);
            BYTE* armOwner = *(BYTE**)(n + 0x6f8);
            if (!am || !armOwner || *(int*)(am + 0x74) == 0) return;
            BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
            BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, armOwner) : NULL;
            BYTE* lock = target ? *(BYTE**)(target + 0xb84) : NULL;
            MmWorld_t w = (MmWorld_t)(imOnryouModule + 0x15930);
            float r[16], k[16];
            memcpy(r, w(n), sizeof r);
            memcpy(k, w(armOwner), sizeof k);
            const float* q = lock ? w(lock) : r;
            BYTE* km = *(BYTE**)(armOwner + 0x398);
            BYTE* tm = target ? *(BYTE**)(target + 0x398) : NULL;
            float ax = r[12] - k[12], az = r[14] - k[14], tx = q[12] - k[12], tz = q[14] - k[14];
            Log("KUZU_ARM %s@%d fwd %.0f side %.0f y %.0f | target fwd %.0f side %.0f y %.0f gap %.0f | %s@%d hp %d | kraken %s@%d",
                *(const char**)am, *(int*)(am + 0x54), ax * k[8] + az * k[10], ax * k[0] + az * k[2], r[13] - k[13],
                tx * k[8] + tz * k[10], tx * k[0] + tz * k[2], q[13] - k[13],
                sqrtf((q[12] - r[12]) * (q[12] - r[12]) + (q[13] - r[13]) * (q[13] - r[13]) + (q[14] - r[14]) * (q[14] - r[14])),
                tm ? *(const char**)tm : "-", tm ? *(int*)(tm + 0x54) : -1, target ? *(int*)(target + 0x558) : -1,
                km ? *(const char**)km : "-", km ? *(int*)(km + 0x54) : -1);
            return;
        }
        if (strstr(path, "/character/lm/") && !strstr(path, "/lm04") && *(int*)(n + 0x724) == 0)   // PLAYER1 Kraken's 9-head dragon
        {
            BYTE* km = *(BYTE**)(n + 0x398);
            if (!km || _stricmp(*(const char**)km, "9HeadDragon")) return;
            BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
            BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, n) : NULL;
            BYTE* lock = target ? *(BYTE**)(target + 0xb84) : NULL;
            BYTE* hand = ((ImFindNodeFn)(imOnryouModule + 0x15a00))(n, "catch_Null");
            MmWorld_t w = (MmWorld_t)(imOnryouModule + 0x15930);
            float k[16];
            memcpy(k, w(n), sizeof k);
            const float* q = lock ? w(lock) : k;
            const float* h = hand ? w(hand) : k;
            const float* raw = (const float*)(n + 4);
            BYTE* tm = target ? *(BYTE**)(target + 0x398) : NULL;
            float tx = q[12] - k[12], tz = q[14] - k[14], hx = h[12] - k[12], hz = h[14] - k[14];
            Log("KUZU_K 9HeadDragon@%d hp %d keys %p | z %.3f,%.3f +14 %.4f,%.4f,%.4f | to target %.1f deg fwd %.0f side %.0f | hand fwd %.0f side %.0f y %.0f | %s@%d hp %d",
                *(int*)(km + 0x54), *(int*)(n + 0x558), *(void**)(n + 0x700), k[8], k[10], raw[4], raw[5], raw[6],
                atan2f(tx * k[0] + tz * k[2], tx * k[8] + tz * k[10]) * 57.2957795f, tx * k[8] + tz * k[10], tx * k[0] + tz * k[2],
                hx * k[8] + hz * k[10], hx * k[0] + hz * k[2], h[13] - k[13],
                tm ? *(const char**)tm : "-", tm ? *(int*)(tm + 0x54) : -1, target ? *(int*)(target + 0x558) : -1);
            return;
        }
        if (!strstr(path, "/lm044/")) return;
        BYTE* mo = *(BYTE**)(n + 0x398);
        if (!mo) return;
        int frame = *(int*)(mo + 0x54);
        if (frame > 120 && frame % 10) return;
        BYTE* owner = *(BYTE**)(n + 0x6f8);
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        BYTE* node = owner ? ((ImFindNodeFn)(imOnryouModule + 0x15a00))(owner, "ice_Null") : NULL;
        BYTE* om = owner ? *(BYTE**)(owner + 0x398) : NULL;
        const float* i = world(n);
        const float* o = owner ? world(owner) : i;
        const float* h = node ? world(node) : o;
        const float* raw = (const float*)(n + 4);
        Log("ICE_FRAME %s@%d ice %.0f,%.0f,%.0f | ice_Null %.0f,%.0f,%.0f | owner %.0f,%.0f,%.0f %s@%d | +4 %.0f,%.0f,%.0f,%.2f +14 %.3f,%.3f,%.3f,%.3f +94 %02x parent %p",
            *(const char**)mo, frame, i[12], i[13], i[14], h[12], h[13], h[14], o[12], o[13], o[14], om ? *(const char**)om : "-",
            om ? *(int*)(om + 0x54) : -1, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], n[0x94], *(void**)(n + 0x98));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- 11-03 사용자 「칼리굴라의 빙격이 플레이어 사이드일떄 로드 비야키 상대일경우 빙격의 위치가 칼리굴라의 머리 위가 아니라 엉뚱한데
// 생기는거 같아」: the ice (LM044) is made at the caster's ice_Node and, once shot, keeps its world position (+4, no parent at +0x98)
// until it flies at frame 230. Kraken as PLAYER1 fights Lord Byakhee on Byakhee's stage (BATTLE-06, gravity 0), where the caster
// keeps sliding after its walk or punch - 300 away by the 44th frame (11-03 trace), so the ice hung far from Caligula; on the ground
// stages it stands still and the ice stays over its head. For Kraken as PLAYER1 against Lord Byakhee, the forming ice keeps to the
// caster's ice_Null until frame 230, then flies by its own guidance as before. The +0x94 bits and 0x15540 as nero_ground.h does.
typedef void(__cdecl* ImDirtyFn)(BYTE*);
static BYTE* iceFollowLast;
static void IceFollow(BYTE* n)
{
    if (!imOnryouModule) return;
    __try
    {
        BYTE* mo = *(BYTE**)(n + 0x398);
        if (!mo || _stricmp(*(const char**)mo, "SHOOT") || *(int*)(mo + 0x54) >= 230 || *(void**)(n + 0x98)) return;
        char path[512];
        ImDefPath(n, path, sizeof path);
        if (!strstr(path, "/lm044/")) return;
        BYTE* owner = *(BYTE**)(n + 0x6f8);
        if (!owner || *(int*)(owner + 0x724) != 0 || !ImDefIn(owner, "/character/lm/")) return;
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, owner) : NULL;
        BYTE* node = ((ImFindNodeFn)(imOnryouModule + 0x15a00))(owner, "ice_Null");
        if (!target || !node || !ImDefIn(target, "/character/mm/")) return;
        const float* h = ((MmWorld_t)(imOnryouModule + 0x15930))(node);
        if (!_finite(h[12]) || !_finite(h[13]) || !_finite(h[14])) return;
        float* pos = (float*)(n + 4);
        if (iceFollowLast != n)
        {
            iceFollowLast = n;
            Log("ICE_FOLLOW Kraken PLAYER1 vs Lord Byakhee: the forming ice keeps over the caster until frame 230 (it was %.0f away)",
                sqrtf((pos[0] - h[12]) * (pos[0] - h[12]) + (pos[1] - h[13]) * (pos[1] - h[13]) + (pos[2] - h[14]) * (pos[2] - h[14])));
        }
        pos[0] = h[12];
        pos[1] = h[13];
        pos[2] = h[14];
        n[0x94] &= 0xfc;
        ((ImDirtyFn)(imOnryouModule + 0x15540))(n);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// ---- 11-03 사용자 「데몬베인 블러드가 크라켄의 그랩에 잘 안잡히는데 이유가 뭘까?」→「고쳐 줘」, 「크라켄이 조금 멀리 있어도 그랩을
// 써서 헛손질하는 경우가 있는데 사거리가 부족하면 조금 더 다가가서 쓰게 할 수 있을까?」: Kraken's bind arm (LM046 ANA_shot, a bullet)
// flies straight on from the caster's catch_Null at chest height (about 80); against D Blood it went some 50 over D Blood's lock
// node (about 30) and caught nothing (0 of 2, 11-03 trace). For Kraken as PLAYER1 against D Blood, in the balance mode only (a hit
// fix - 사용자 10-20 「판정 고침도 균형 모드에서만」): ① through the arm's catch frames (SHOOT 1~60) it rides at D Blood's lockOn height -
// the arm stays on its parent node, whose animation carries it straight on (its own +4/+0x14 stay 0; turning it did nothing), so
// the height gap is put into its local position (+4, the world gap solved on the parent's axes; at most kArmMaxDrop); ② the AI's
// A_ARM2 ask becomes FORWARD_G (AI lock-on: it turns to the target as it goes) while D Blood's lockOn is out of the arm's reach -
// 사용자 「정확한 사거리를 재서 쓰게 하자」, measured 11-03 (logs duel_1103_170834, 5 shots; 165006, the catch): the arm leaves 16 ahead
// of Kraken's root and goes straight on 18.7 a frame to 1119 ahead at its last catch frame (SHOOT@60), side ±5; it caught with
// the lockOn 40 off and missed at 53 - so 1119 + 40, flat from Kraken's root. Kraken does not move or turn through A_ARM2.
static const int kArmAimFrames = 60;
static const float kArmMaxDrop = 200.0f;
static const float kKrakenGrabReach = 1159.0f;
static BYTE* armAimLast;
static int krakenGrabLogs;
static float krakenGrabReach;                                           // kKrakenGrabReach, or the test key kraken_grab_reach
static BYTE* KrakenVsDBlood(BYTE* owner)                                // D Blood, when owner is Kraken as PLAYER1 against it
{
    if (!imOnryouModule || !duelBalance || !owner || *(int*)(owner + 0x724) != 0 || !ImDefIn(owner, "/character/lm/")) return NULL;
    BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
    BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, owner) : NULL;
    return target && ImDefIn(target, "/character/gm/") ? target : NULL;
}
static void ArmAim(BYTE* n)
{
    if (!imOnryouModule || !duelBalance) return;
    __try
    {
        BYTE* mo = *(BYTE**)(n + 0x398);
        BYTE* parent = *(BYTE**)(n + 0x98);
        if (!mo || !parent || _stricmp(*(const char**)mo, "SHOOT") || *(int*)(mo + 0x54) > kArmAimFrames) return;
        char path[512];
        ImDefPath(n, path, sizeof path);
        if (!strstr(path, "/lm046/")) return;
        BYTE* target = KrakenVsDBlood(*(BYTE**)(n + 0x6f8));
        BYTE* aim = target ? ((NgFindFn)(imOnryouModule + 0x15a00))(target, "lockOn") : NULL;
        if (!aim) return;
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        float p[16];
        memcpy(p, world(parent), sizeof p);
        float drop = world(aim)[13] - p[13];                   // the world height gap, put on the parent's axes
        if (!_finite(drop)) return;
        drop = drop < -kArmMaxDrop ? -kArmMaxDrop : drop > kArmMaxDrop ? kArmMaxDrop : drop;
        float local[3];
        for (int i = 0; i < 3; ++i)
        {
            const float* axis = p + 4 * i;
            float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
            local[i] = len2 > 1e-6f ? drop * axis[1] / len2 : 0.0f;
        }
        if (!_finite(local[0]) || !_finite(local[1]) || !_finite(local[2])) return;
        float* pos = (float*)(n + 4);
        pos[0] = local[0];
        pos[1] = local[1];
        pos[2] = local[2];
        n[0x94] &= 0xfc;
        ((NgDirtyFn)(imOnryouModule + 0x15540))(n);
        if (armAimLast != n)
        {
            armAimLast = n;
            Log("KRAKEN_ARM_AIM the bind arm rides at D Blood's lockOn height (%.0f %s the arm's path)", fabsf(drop),
                drop < 0 ? "below" : "above");
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- 11-08 사용자 「우리 네로가 적에게 쓰면 하나도 안 맞았어」「우리 네로의 인공지능 문제인가?」: not the AI - the hit of Nero's sword spell
// (MAKEN) rides on two bullets only, one on maken_Null near her hand and one on maken_Null2 at the tip, while the drawn sword between
// them sweeps the whole front (11-08 sword_trace, logs duel_1108_054607: the slash crosses her front at bullet frames 66~90, the hand
// bullet at 120~430 out, the tip at 930~1,170; hit at 127 from the hand bullet, missed at 264 / 419 / 145 - a DUEL enemy mostly
// stands in the 430~900 between). Lightbringer (KENTEI) is the same with four bullets along each blade (Ken_L/R_Null..Null4), some
// 1,300~1,500 apart. BladeHit, in a DUEL fight in the balance mode (사용자 10-20 「판정 고침도 균형 모드에서만」): while the bullet on
// the first node of a blade is in SHOOT (its hit frames 1~130), it rides at the point of that blade (first node -> last node)
// nearest its owner's target's lock node - put into its local position on its parent node's axes, as ArmAim does - so wherever
// the drawn blade crosses the enemy, it hits there; each bullet still hits once (BREAK / BREAK2 as before, guard still blocks).
// autobattle.ini blade_hit: 0 off, 1 PLAYER1's Nero (our side), 2 both sides.
static int bladeHit = 0;               // 11-08: moving the hit bullet killed its hits (0/4, 0/4) - off; kept only for the record
static BYTE* bladeHitLast;
static int bladeHitLogs;
static void BladeHit(BYTE* n)
{
    if (!bladeHit || !imOnryouModule || !duelBalance) return;
    __try
    {
        BYTE* mo = *(BYTE**)(n + 0x398);
        BYTE* parent = *(BYTE**)(n + 0x98);
        if (!mo || !parent || _stricmp(*(const char**)mo, "SHOOT")) return;
        const int frame = *(int*)(mo + 0x54);
        if (frame < 1 || frame > 130) return;
        BYTE* owner = *(BYTE**)(n + 0x6f8);
        if (!owner || !ImDefIn(owner, "/character/qm/")) return;
        const int side = *(int*)(owner + 0x724);
        if (side != 0 && (bladeHit != 2 || side != 1)) return;
        NgFindFn find = (NgFindFn)(imOnryouModule + 0x15a00);
        static const char* const kBlades[3][2] = {{"maken_Null", "maken_Null2"}, {"Ken_L_Null", "Ken_L_Null4"}, {"Ken_R_Null", "Ken_R_Null4"}};
        int b = 0;
        for (; b < 3; ++b)
            if (find(owner, kBlades[b][0]) == parent) break;
        if (b == 3) return;
        BYTE* tip = find(owner, kBlades[b][1]);
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, owner) : NULL;
        BYTE* tlock = target ? *(BYTE**)(target + 0xb84) : NULL;
        // 11-08 second try: toward the lock node the hit sat 41~65 from it for 20 frames and hit nothing (logs duel_1108_055352) -
        // the target's hit volumes (collid_body, collid_ba_*, ... SEPARATER&HIT in its definition) are elsewhere on its body; aim at
        // its collid_body node, the lock node only when it has none
        BYTE* body = target ? find(target, "collid_body") : NULL;
        BYTE* aimAt = body ? body : tlock;
        if (!tip || !aimAt) return;
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        float p[16], q[16], t[16];
        memcpy(p, world(parent), sizeof p);
        memcpy(q, world(tip), sizeof q);
        memcpy(t, world(aimAt), sizeof t);
        float ab[3], at[3], len2 = 0.0f, dot = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            ab[i] = q[12 + i] - p[12 + i];
            at[i] = t[12 + i] - p[12 + i];
            len2 += ab[i] * ab[i];
            dot += ab[i] * at[i];
        }
        if (!_finite(len2) || !_finite(dot) || len2 < 1.0f) return;
        float u = dot / len2;
        u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
        float local[3];
        for (int i = 0; i < 3; ++i)
        {
            const float* axis = p + 4 * i;
            const float a2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
            local[i] = a2 > 1e-6f ? u * (ab[0] * axis[0] + ab[1] * axis[1] + ab[2] * axis[2]) / a2 : 0.0f;
        }
        if (!_finite(local[0]) || !_finite(local[1]) || !_finite(local[2])) return;
        float* pos = (float*)(n + 4);
        pos[0] = local[0];
        pos[1] = local[1];
        pos[2] = local[2];
        n[0x94] &= 0xfc;
        ((NgDirtyFn)(imOnryouModule + 0x15540))(n);
        if (bladeHitLast != n && bladeHitLogs > 0)
        {
            bladeHitLast = n;
            --bladeHitLogs;
            Log("BLADE_HIT side %d %s: the hit rides the blade at the point nearest the target (%.0f of %.0f out from %s)", side,
                kBlades[b][0], u * sqrtf(len2), sqrtf(len2), kBlades[b][0]);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- 11-08 사용자 「하드 모드에서 네로가 각인 666을 쓸때는 자동 가드가 걸리고 기절에 면역이 되게 할 수 있어?」: KOKUIN666 is category 8,
// whose interrupt has no stagger jump (#210 / #212); the only thing that stops it before its end is a DUEL grab - the grabber's copy
// writes the shared #380 = 1 and interrupt 8's `jump COMP(#380==1) -1 0 MOTION(DUEL_HELD)` takes her (11-08 test, LM:QM hard,
// kokuin_test=1: Kraken's A_ARM2 stopped it at frame 472, and at 589 with pack 541's guard on). The param getter 0x30e90: #0~199 per
// robot (+0x3c8 + 4n), #200~399 one shared table - [demonbane+0x11e3c8] + 4 + 4(n-200) (0x41ce0 reads, 0x41d00 writes); a first
// try on the robot's own +0x3c8 + 4*380 never saw it (that is past the 200 own slots). Then, clearing the shared #380 just before
// her update did not help either: grab_trace (logs duel_1108_075154) saw #380 0->1 first before the stage's update and Nero already
// in DUEL_HELD@1 before hers - the grab is resolved after all the robot updates (the hit phase), #380 written and her interrupt taken
// there. Both shared writers (set_param 0x30e50 and the motion command at 0x35b7a) go through 0x41d00(table, n-200, value), so that
// function jumps to SharedSet: while the hard-mode enemy Nero (PLAYER2, DUEL difficulty 2, balance mode) is in KOKUIN666, a write of
// #380 = 1 is dropped - the grab does not take her; every other write is done as 0x41d00 does. The grabber's copy (PLAYER1, a pilot
// copy) only writes #380 and reads #384. The guard itself is pack 541 (KOKUIN666's original GUARD(50) block, hard-mode enemy copies).
static int kokuinNoGrabLogs;
static BYTE* kokuinNero;                 // the hard-mode enemy Nero while she is in KOKUIN666 (set before her update)
static bool kokuinDump;                  // test only (autobattle.ini kokuin_dump=1): her struct before her update vs at the catch
static BYTE kokuinSnap[0x2000];
static bool kokuinSnapOk;
// test only (kokuin_dump=1): every actor the updates pass, and where in their first 0x1400 bytes Nero's address sits - the catching
// bullet should take it into some field at the catch (the arrest link)
static BYTE* kokuinSeen[96];
static int kokuinSeenCount;
static struct { BYTE* actor; int off; } kokuinRefs[64];
static int kokuinRefCount;
static struct { BYTE* actor; BYTE bytes[0x1400]; } kokuinArm[2];
static int kokuinArmCount;
static int KokuinFindRefs(BYTE* nero, decltype(kokuinRefs)& out, int cap)
{
    int n = 0;
    for (int i = 0; i < kokuinSeenCount && n < cap; ++i)
    {
        BYTE* x = kokuinSeen[i];
        if (x == nero) continue;
        __try
        {
            for (int off = 0; off < 0x1400 && n < cap; off += 4)
                if (*(BYTE**)(x + off) == nero) { out[n].actor = x; out[n].off = off; ++n; }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return n;
}
// 11-08 사용자 「잡기도 안 붙게」: with #380 dropped she stays in KOKUIN666, but the engine's catch still carried her (logs
// duel_1108_075526: pulled 600 out and lifted to 1,000 by Kraken's arm, squeezed 16 x 9). No field of hers (first 0x2000 bytes) links
// to the catcher at the catch (kokuin_dump, logs duel_1108_083332); the catching bullet (ana_shotvs) holds her at +0xbe4 - its target,
// set when it is shot. Emptying that target at CATCH@1 stopped the squeeze but not the carry, and the game fell over later (logs
// duel_1108_083621) - not used. A shot catching bullet is not updated through these hooks any more (only while it waits, ACTIVE -
// logs duel_1108_084531), so every actor these updates pass in a hard-mode DUEL fight is kept in kokuinActors, and before the
// KOKUIN666 Nero's own update each one homing at her (+0xbe4 = her) in a CATCH... motion is put into its own BREAK - the motion its
// miss runs at 61 (its three motion pointers +0x398/+0x39c/+0x3a0, motion structs 0x80 apart, BREAK found by name, frame 0): it lets
// go as on a miss, and its robot takes its no-catch path.
static BYTE* kokuinActors[128];
static int kokuinActorCount;
static float kokuinPos[3];               // where the KOKUIN666 Nero stood before a let-go
static bool kokuinPosOk;
static int kokuinPin;                    // after a let-go: keep her at kokuinPos until KOKUIN666 ends
// 11-08 사용자 「바인드 스펠을 써서 그런지 취소됐네」 (폭식 대죄 등에 가드만 넣었을 때, 화면 시연): the same no-grab for the other long moves
// the hard-mode enemy guards through (pack 542). Nero's bind spell (QM047, PLAYER1) is a DUEL grab too: #380 = 1, and its bullet's
// `jump ISCATCH -1 0 MOTION(BREAK)` - its BREAK is the hold - so a bind bullet holding her goes to its ABORT; a catcher in CATCH...
// (Kraken's arm) to its BREAK. 원령 주탄 stays open to binds and grabs (only its charge was made shorter). KOKUIN666 does not move:
// after a let-go she keeps her place to its end; the others may move, so only for 30 updates.
// 11-08 사용자 「아 잡기 무효는 네로한테만 넣어 다른거엔 가드만 넣도록」: only KOKUIN666 here - 폭식 대죄 / 비검·비룡 / 금기의 피 해방
// keep only pack 542's guard (a bind or a grab still stops them).
struct KokuinMove { const char* dir; const char* motion; bool untilEnd; };
static const KokuinMove kKokuinMoves[] = {
    {"/character/qm/", "KOKUIN666", true}};
static int kokuinMoveIndex = -1;          // which of them the caster is in
static BYTE* kokuinCasterWas;
static int KokuinProtected(BYTE* a, BYTE* mo)
{
    if (!mo) return -1;
    const char* n = *(const char**)mo;
    for (int i = 0; i < (int)_countof(kKokuinMoves); ++i)
        if (!_stricmp(n, kKokuinMoves[i].motion) && ImDefIn(a, kKokuinMoves[i].dir)) return i;
    return -1;
}
static int KokuinPinFrames() { return kokuinMoveIndex >= 0 && kKokuinMoves[kokuinMoveIndex].untilEnd ? 1000000 : 30; }
static bool KokuinMiss(BYTE* x)
{
    __try
    {
        if (!kokuinNero || !x || x == kokuinNero) return false;
        BYTE* m = *(BYTE**)(x + 0x398);
        const char* n = m ? *(const char**)m : "";
        if (kokuinDump && !_strnicmp(n, "CATCH", 5))
        {
            static int told;
            if (told++ < 6) Log("KOKUIN_MISS_DEBUG %p %s +be4 %p (Nero %p) motion %p", x, n, *(BYTE**)(x + 0xbe4), kokuinNero, m);
        }
        if (*(BYTE**)(x + 0xbe4) != kokuinNero) return false;
        char xpath[512];
        ImDefPath(x, xpath, sizeof xpath);
        const bool bind = strstr(xpath, "/qm047/") != NULL;                 // Nero's bind spell: its BREAK holds
        const char* want = bind ? (_stricmp(n, "BREAK") ? NULL : "ABORT") : (_strnicmp(n, "CATCH", 5) ? NULL : "BREAK");
        if (!want) return false;
        BYTE* miss = NULL;
        for (int k = -16; k <= 16 && !miss; ++k)
        {
            BYTE* p = m + 0x80 * k;
            const char* pn = NULL;
            __try { pn = *(const char**)p; if (pn && strcmp(pn, want)) pn = NULL; } __except (EXCEPTION_EXECUTE_HANDLER) { pn = NULL; }
            if (pn) miss = p;
        }
        if (!miss)
        {
            if (kokuinDump) Log("KOKUIN_MISS_DEBUG %p no %s within 16 motions of %p", x, want, m);
            return false;
        }
        const int fr = *(int*)(m + 0x54);
        *(int*)(miss + 0x54) = 0;
        *(BYTE**)(x + 0x398) = miss;
        *(BYTE**)(x + 0x39c) = miss;
        *(BYTE**)(x + 0x3a0) = miss;
        if (kokuinNoGrabLogs > 0)
        {
            --kokuinNoGrabLogs;
            char path[512];
            ImDefPath(x, path, sizeof path);
            const char* f = strrchr(path, '/');
            Log("KOKUIN_NO_GRAB %s %s@%d -> %s: it lets go", f ? f + 1 : path, n, fr, want);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void KokuinNoGrab(BYTE* a)
{
    __try
    {
        if (kokuinDump && kokuinNero && a != kokuinNero)
        {
            static BYTE* told[16];
            char path[512];
            ImDefPath(a, path, sizeof path);
            bool known = false;
            for (BYTE* t : told) if (t == a) { known = true; break; }
            if (strstr(path, "ana_shot") && !known)
            {
                for (BYTE*& t : told) if (!t) { t = a; break; }
                BYTE* m = *(BYTE**)(a + 0x398);
                Log("KOKUIN_CATCHER_SEEN %p %s motion %s +be4 %p (Nero %p)", a, strrchr(path, '/') ? strrchr(path, '/') + 1 : path,
                    m ? *(const char**)m : "-", *(BYTE**)(a + 0xbe4), kokuinNero);
            }
        }
        if (imOnryouModule && duelBalance && duelDifficulty == 2)
        {
            bool known = false;
            for (int i = 0; i < kokuinActorCount; ++i) if (kokuinActors[i] == a) { known = true; break; }
            if (!known && kokuinActorCount < (int)_countof(kokuinActors)) kokuinActors[kokuinActorCount++] = a;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try
    {
        if (kokuinDump)
        {
            bool known = false;
            for (int i = 0; i < kokuinSeenCount; ++i) if (kokuinSeen[i] == a) { known = true; break; }
            if (!known && kokuinSeenCount < (int)_countof(kokuinSeen)) kokuinSeen[kokuinSeenCount++] = a;
        }
        if (kokuinNero == a) kokuinNero = NULL;
        if (!imOnryouModule || !duelBalance || duelDifficulty != 2 || *(int*)(a + 0x724) != 1) return;
        BYTE* mo = *(BYTE**)(a + 0x398);
        const int protectedMove = KokuinProtected(a, mo);
        if (protectedMove >= 0)
        {
            kokuinNero = a;
            kokuinCasterWas = a;
            kokuinMoveIndex = protectedMove;
            bool released = false;
            for (int i = 0; i < kokuinActorCount; ++i) released |= KokuinMiss(kokuinActors[i]);   // a catcher holding her lets go first
            // let go that way she was left at the stage's origin (0,0,0) for the rest of KOKUIN666 (logs duel_1108_085128) - she does
            // not move in it, so after a let-go she is kept where she was just before (her root +4, as AltLift writes it)
            float* pos = (float*)(a + 4);
            if (released) kokuinPin = KokuinPinFrames();
            if (kokuinPin > 0 && kokuinPosOk &&
                (fabsf(pos[0] - kokuinPos[0]) > 1.0f || fabsf(pos[1] - kokuinPos[1]) > 1.0f || fabsf(pos[2] - kokuinPos[2]) > 1.0f))
            {
                if (kokuinNoGrabLogs > 0)
                {
                    --kokuinNoGrabLogs;
                    Log("KOKUIN_NO_GRAB %s: kept at %.0f,%.0f,%.0f (was moved to %.0f,%.0f,%.0f)", kKokuinMoves[protectedMove].motion,
                        kokuinPos[0], kokuinPos[1], kokuinPos[2], pos[0], pos[1], pos[2]);
                }
                pos[0] = kokuinPos[0];
                pos[1] = kokuinPos[1];
                pos[2] = kokuinPos[2];
                a[0x94] &= 0xfc;
                ((ImDirtyFn)(imOnryouModule + 0x15540))(a);
            }
            else if (kokuinPin <= 0)
            {
                kokuinPos[0] = pos[0];
                kokuinPos[1] = pos[1];
                kokuinPos[2] = pos[2];
                kokuinPosOk = true;
            }
            if (kokuinPin > 0) --kokuinPin;
            if (kokuinDump)
            {
                memcpy(kokuinSnap, a, sizeof kokuinSnap);
                kokuinSnapOk = true;
                kokuinRefCount = KokuinFindRefs(a, kokuinRefs, (int)_countof(kokuinRefs));
                kokuinArmCount = 0;                       // the catching bullets' structs too (any ana_shot in the updates)
                for (int i = 0; i < kokuinSeenCount && kokuinArmCount < (int)_countof(kokuinArm); ++i)
                {
                    char path[512];
                    ImDefPath(kokuinSeen[i], path, sizeof path);
                    if (!strstr(path, "ana_shot")) continue;
                    kokuinArm[kokuinArmCount].actor = kokuinSeen[i];
                    memcpy(kokuinArm[kokuinArmCount].bytes, kokuinSeen[i], sizeof kokuinArm[0].bytes);
                    ++kokuinArmCount;
                }
            }
        }
        else if (a == kokuinCasterWas && kokuinPosOk)                          // the caster left the move: no more keeping
        {
            kokuinPin = 0;
            kokuinPosOk = false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void KokuinDumpDiff(BYTE* a)
{
    if (!kokuinDump || !kokuinSnapOk) return;
    kokuinSnapOk = false;
    decltype(kokuinRefs) refs;
    const int nNow = KokuinFindRefs(a, refs, (int)_countof(refs));
    for (int i = 0; i < nNow; ++i)
    {
        bool before = false;
        for (int j = 0; j < kokuinRefCount; ++j)
            if (kokuinRefs[j].actor == refs[i].actor && kokuinRefs[j].off == refs[i].off) { before = true; break; }
        char path[512] = "";
        const char* mn = "-";
        int fr = -1;
        __try
        {
            ImDefPath(refs[i].actor, path, sizeof path);
            BYTE* m = *(BYTE**)(refs[i].actor + 0x398);
            if (m) { mn = *(const char**)m; fr = *(int*)(m + 0x54); }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        const char* f = strrchr(path, '/');
        Log("KOKUIN_REF %s actor %p (%s %.24s@%d) +%03x holds Nero %p", before ? "old" : "NEW", refs[i].actor, f ? f + 1 : path, mn, fr,
            refs[i].off, a);
    }
    for (int k = 0; k < kokuinArmCount; ++k)                // what changed in the catching bullet at the catch
    {
        int shown = 0;
        for (int off = 0; off < (int)sizeof kokuinArm[0].bytes && shown < 80; off += 4)
        {
            DWORD was = *(DWORD*)(kokuinArm[k].bytes + off), cur = 0;
            __try { cur = *(DWORD*)(kokuinArm[k].actor + off); } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            if (was == cur) continue;
            ++shown;
            Log("KOKUIN_ARM %p +%03x %08lx -> %08lx (as float %.2f -> %.2f)%s", kokuinArm[k].actor, off, was, cur, *(float*)&was,
                *(float*)&cur, (BYTE*)(uintptr_t)cur == a ? " = Nero" : "");
        }
    }
    for (int off = 0; off < (int)sizeof kokuinSnap; off += 4)       // any seen actor's address newly in Nero's struct
    {
        BYTE* cur = NULL;
        __try { cur = *(BYTE**)(a + off); } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (!cur || cur == *(BYTE**)(kokuinSnap + off)) continue;
        for (int i = 0; i < kokuinSeenCount; ++i)
            if (kokuinSeen[i] == cur) { Log("KOKUIN_NERO_LINK +%03x now holds actor %p", off, cur); break; }
    }
    int lines = 0;
    for (int off = 0; off < (int)sizeof kokuinSnap && lines < 120; off += 4)
    {
        DWORD was = *(DWORD*)(kokuinSnap + off), now = 0;
        __try { now = *(DWORD*)(a + off); } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (was == now) continue;
        char what[160] = "";
        if (now >= 0x01000000 && now < 0x7fff0000)
        {
            __try
            {
                BYTE* d = *(BYTE**)((BYTE*)(uintptr_t)now + 0x16c);
                const char* p = d ? *(const char**)(d + 0x34) : NULL;
                BYTE* m = *(BYTE**)((BYTE*)(uintptr_t)now + 0x398);
                if (p) sprintf_s(what, " -> actor? def %.80s motion %.24s", p, m ? *(const char**)m : "-");
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { what[0] = 0; }
        }
        ++lines;
        Log("KOKUIN_DUMP +%03x %08lx -> %08lx (as float %.2f -> %.2f)%s", off, was, now, *(float*)&was, *(float*)&now, what);
    }
}
static bool __fastcall SharedSet(BYTE* table, void*, int n, int value)
{
    if (n < 0 || n >= 0x100) return false;
    if (n == 380 - 200 && value == 1 && kokuinNero)
    {
        __try
        {
            BYTE* mo = *(BYTE**)(kokuinNero + 0x398);
            const int pm = KokuinProtected(kokuinNero, mo);
            if (pm >= 0)
            {
                if (kokuinNoGrabLogs > 0)
                {
                    --kokuinNoGrabLogs;
                    Log("KOKUIN_NO_GRAB side 1 %s@%d: a grab's / bind's #380 = 1 dropped - not taken (hard mode); %p",
                        kKokuinMoves[pm].motion, *(int*)(mo + 0x54), kokuinNero);
                }
                KokuinDumpDiff(kokuinNero);
                kokuinMoveIndex = pm;
                kokuinPin = KokuinPinFrames();                   // any grab (an arm, a hand, a bind): from here it keeps its place
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    *(int*)(table + 4 + 4 * n) = value;
    return true;
}
// on: 0x41d00 jumps to SharedSet (only over the expected code); off: its first bytes back
static void SetSharedHook(BYTE* m, bool on)
{
    static const BYTE head[8] = {0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0, 0x7c, 0x14};   // mov eax,[esp+4]; test eax,eax; jl +14h
    BYTE* at = m + 0x41d00;
    bool ours = at[0] == 0xe9 && at + 5 + *(LONG*)(at + 1) == (BYTE*)SharedSet;
    if (on && !ours)
    {
        if (memcmp(at, head, sizeof head)) { Log("KOKUIN_NO_GRAB off: demonbane.dll+0x41d00 is not the expected code"); return; }
        BYTE jmp[5] = {0xe9};
        *(LONG*)(jmp + 1) = (LONG)((BYTE*)SharedSet - (at + 5));
        if (!WriteCode(at, jmp, 5)) Log("KOKUIN_NO_GRAB off: could not write demonbane.dll+0x41d00 (error %lu)", GetLastError());
        else Log("KOKUIN_NO_GRAB on: the hard-mode enemy Nero's KOKUIN666 is not taken by a DUEL grab");
    }
    else if (!on && ours) WriteCode(at, head, 5);
}
// test only (autobattle.ini grab_trace=1): every change of the shared #380 / #384 seen before or after a robot's update, and every
// motion change of a PLAYER2 Nero while it leaves KOKUIN666 - to see when a grab takes her
static bool grabTrace;
static int grabTraceLines;
static int grabSeen380 = -99, grabSeen384 = -99;
static void GrabTrace(BYTE* a, const char* when)
{
    if (!grabTrace || !imOnryouModule || grabTraceLines <= 0) return;
    __try
    {
        BYTE* table = *(BYTE**)(imOnryouModule + 0x11e3c8);
        if (!table) return;
        const int v380 = *(int*)(table + 4 + 4 * 180), v384 = *(int*)(table + 4 + 4 * 184);
        char path[512];
        ImDefPath(a, path, sizeof path);
        const char* f = strrchr(path, '/');
        BYTE* mo = *(BYTE**)(a + 0x398);
        const char* mn = mo ? *(const char**)mo : "-";
        const int fr = mo ? *(int*)(mo + 0x54) : -1;
        if (v380 != grabSeen380 || v384 != grabSeen384)
        {
            --grabTraceLines;
            Log("GRAB_TRACE #380 %d->%d #384 %d->%d seen %s update of side %d %s %s@%d", grabSeen380, v380, grabSeen384, v384, when,
                *(int*)(a + 0x724), f ? f + 1 : path, mn, fr);
            grabSeen380 = v380;
            grabSeen384 = v384;
        }
        static BYTE* neroWas;
        static char neroMotion[32];
        if (*(int*)(a + 0x724) == 1 && strstr(path, "/character/qm/") && !strchr(strstr(path, "/character/qm/") + 14, '/'))
        {
            if (neroWas == a && _stricmp(neroMotion, mn) && !_stricmp(neroMotion, "KOKUIN666"))
            {
                --grabTraceLines;
                Log("GRAB_TRACE Nero leaves KOKUIN666 for %s@%d (%s update) - #380 %d #384 %d", mn, fr, when, v380, v384);
            }
            neroWas = a;
            strncpy_s(neroMotion, mn, _TRUNCATE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static const char* KrakenGrabRange(BYTE* module, BYTE* a, const char* name)   // FORWARD_G for an A_ARM2 ask out of reach, or NULL
{
    if (_stricmp(name, "A_ARM2") || module != imOnryouModule) return NULL;
    __try
    {
        BYTE* target = KrakenVsDBlood(a);
        BYTE* to = target ? ((NgFindFn)(module + 0x15a00))(target, "lockOn") : NULL;
        if (!to) return NULL;
        MmWorld_t world = (MmWorld_t)(module + 0x15930);
        float p[16];
        memcpy(p, world(a), sizeof p);
        const float* q = world(to);
        float d = sqrtf((q[12] - p[12]) * (q[12] - p[12]) + (q[14] - p[14]) * (q[14] - p[14]));
        if (!_finite(d) || d <= krakenGrabReach) return NULL;
        if (krakenGrabLogs > 0)
        {
            --krakenGrabLogs;
            Log("KRAKEN_GRAB_RANGE asked A_ARM2 with D Blood %.0f away -> FORWARD_G (the arm reaches %.0f)", d, krakenGrabReach);
        }
        return "FORWARD_G";
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

// ---- 11-08 사용자 「AI가 반드시 쓰는 거리에서만 쓰는게 옳겠지」 (11-03 크라켄 잡기 「정확한 사거리를 재서 쓰게 하자」와 같은 길): Nero's
// sword spell (MAKEN) hits only where its two blade bullets sweep her front, about 2.8 s after it starts (hand bullet ~120~430 out,
// middle ~850~1,170; sword_trace 11-08). By the flat distance between the two lock nodes when it starts, 37 casts (logs
// duel_1108_044234 ~ 061550): 100~400 hit 7 of 9, under 90 1 of 6, 400~650 1 of 5, over 650 0 of 12 (or cut by a hit in its long
// start). Lightbringer (KENTEI) hit 8 of 10 from 300 to 1,800 and missed only at 70 / 193 / 1,725. So in a DUEL fight in the balance
// mode a Nero AI's ask for MAKEN outside 100~400, or KENTEI outside 250~1,800, becomes: too close - BQUICK_G (she backs off and the
// AI asks again from there); too far - M_SHOT, her bullet spell, which reaches from there (aimed by NgApply). First try was FORWARD_G
// when too far (as KrakenGrabRange does): 4 fights (logs duel_1108_063802 ~ 064653) - one MAKEN in 120 s (hit), but walking in, our
// Nero dealt 461 in 120 s against 651 in 100 s unchanged, and the enemy Nero took 1,034 from Beelzebub's kicks against 330 before.
// Keys (a person piloting) unchanged. autobattle.ini nero_sword_range: 0 off, 1 PLAYER1's Nero only, 2 both sides (default).
static int neroSwordRange = 2;
static int neroSwordLogs;

// 11-08 사용자 「그럼 소드 스펠 쓰기 전에 이동으로 사거리 안으로 들어가 휘두르게 할까?」: walking in first was tried in v21 (FORWARD_G
// whenever MAKEN was asked too far): against Beelzebuth our Nero dealt 461 in 120 s (651 in 100 s unchanged) and took 1,034 from its
// kicks (330 before) - a free robot hits back while she walks in and in the 2.8 s before her sweep. A robot held by her bind spell
// (DUEL_HELD, about 4 s) cannot: so for a held target out of the 140~210 band she steps in (FORWARD_G) and as soon as the lock nodes
// are 210 or nearer she asks MAKEN herself (the native ask 0x351b0 - FORWARD_G takes a request at any frame). Called off when the
// target is let go, she leaves FORWARD_G, or 90 updates pass. autobattle.ini nero_sword_approach: 0 off (default), 1 a held target
// only, 2 any target (the v21 way). A/B 11-08 (run_v23_sword_ab.py, 4 fights a build): she did step in (273 -> 205 in 20 updates,
// 284 -> 204 in 23) but both sweeps missed - under her bind the robot sinks about 100 and drifts about 50 away in the 2.8 s before
// the sweep (swept at 260 with it 167 below her lock node; the other let go at 277). So off: a held robot gets her bullet spell.
static int neroSwordApproach = 0;
static BYTE* neroApproach;                  // the Nero stepping in
static BYTE* neroApproachTarget;
static int neroApproachFrames, neroApproachAskFrames;
static bool neroApproachHeld, neroApproachWalked;
static float neroApproachHi, neroApproachFrom;
static const float kApproachMax = 600.0f;
static const int kApproachFrames = 90;
static float LockXz(BYTE* module, BYTE* a, BYTE* target)   // flat distance between the two lock nodes (+0xb84), or -1
{
    BYTE* mine = *(BYTE**)(a + 0xb84);
    BYTE* theirs = target ? *(BYTE**)(target + 0xb84) : NULL;
    if (!mine || !theirs) return -1.0f;
    MmWorld_t world = (MmWorld_t)(module + 0x15930);
    float p[16];
    memcpy(p, world(mine), sizeof p);
    const float* q = world(theirs);
    const float xz = sqrtf((q[12] - p[12]) * (q[12] - p[12]) + (q[14] - p[14]) * (q[14] - p[14]));
    return _finite(xz) ? xz : -1.0f;
}
static const char* MotionName(BYTE* x)
{
    BYTE* mo = x ? *(BYTE**)(x + 0x398) : NULL;
    return mo ? *(const char**)mo : NULL;
}
typedef bool(__thiscall* NeroAskFn)(BYTE*, const char*, int);
static void NeroApproachStep(BYTE* a)                 // after her update: in reach -> ask MAKEN; called off when it no longer fits
{
    if (!a || a != neroApproach) return;
    __try
    {
        const char* mine = MotionName(a);
        const char* theirs = MotionName(neroApproachTarget);
        const float xz = LockXz(imOnryouModule, a, neroApproachTarget);
        if (mine && !_stricmp(mine, "MAKEN"))
        {
            Log("NERO_APPROACH side %d: MAKEN at %.0f after stepping in from %.0f (%d updates)", *(int*)(a + 0x724), xz,
                neroApproachFrom, neroApproachFrames);
            neroApproach = NULL;
            return;
        }
        const bool walking = mine && !_stricmp(mine, "FORWARD_G");
        if (walking) neroApproachWalked = true;
        const char* why = NULL;
        if (++neroApproachFrames > kApproachFrames) why = "90 updates passed";
        else if (neroApproachWalked && !walking && !neroApproachAskFrames) why = "she left FORWARD_G";
        else if (neroApproachHeld && (!theirs || _stricmp(theirs, "DUEL_HELD"))) why = "the target was let go";
        else if (xz < 0) why = "no lock node";
        if (why)
        {
            Log("NERO_APPROACH side %d called off at %.0f (%s; she is in %s)", *(int*)(a + 0x724), xz, why, mine ? mine : "?");
            neroApproach = NULL;
            return;
        }
        if (xz <= neroApproachHi || neroApproachAskFrames)
        {
            if (++neroApproachAskFrames > 20)
            {
                Log("NERO_APPROACH side %d: MAKEN asked 20 times at %.0f but she stays in %s - called off", *(int*)(a + 0x724), xz,
                    mine ? mine : "?");
                neroApproach = NULL;
                return;
            }
            ((NeroAskFn)(imOnryouModule + 0x351b0))(a, "MAKEN", 0);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { neroApproach = NULL; }
}
static const char* NeroApproachAsk(BYTE* module, BYTE* a, const char* name)   // while stepping in: the AI's own asks keep her at it
{
    if (!neroApproach || a != neroApproach || module != imOnryouModule) return NULL;
    if (!_stricmp(name, "MAKEN") || !_stricmp(name, "FORWARD_G")) return neroApproachAskFrames ? "MAKEN" : NULL;
    return neroApproachAskFrames ? "MAKEN" : "FORWARD_G";
}
static const char* NeroSwordRange(BYTE* module, BYTE* a, const char* name)
{
    if (!neroSwordRange || module != imOnryouModule || !duelBalance) return NULL;
    const bool maken = !_stricmp(name, "MAKEN"), kentei = !_stricmp(name, "KENTEI");
    if (!maken && !kentei) return NULL;
    __try
    {
        const int side = *(int*)(a + 0x724);
        if (side != 0 && (neroSwordRange != 2 || side != 1)) return NULL;
        if (!ImDefIn(a, "/character/qm/")) return NULL;
        BYTE* manager = *(BYTE**)(module + 0x11e3b0);
        BYTE* target = manager ? ((MmTarget_t)(module + 0x39d60))(manager, a) : NULL;
        BYTE* mine = *(BYTE**)(a + 0xb84);
        BYTE* theirs = target ? *(BYTE**)(target + 0xb84) : NULL;
        if (!mine || !theirs) return NULL;
        MmWorld_t world = (MmWorld_t)(module + 0x15930);
        float p[16];
        memcpy(p, world(mine), sizeof p);
        const float* q = world(theirs);
        const float xz = sqrtf((q[12] - p[12]) * (q[12] - p[12]) + (q[14] - p[14]) * (q[14] - p[14]));
        if (!_finite(xz)) return NULL;
        // 11-08 사용자 「소드 스펠 고치긴 했는데 여전히 못 맞추네」 (화면 시연, 1P 네로 대 서 있는 하드 적): 69 swings in the day's logs
        // (maken_band.py) by the distance when the blade sweeps her front (her frames 150~200), not when it starts - 183~276 hit a
        // standing or walking target, 306 and more missed, under 90 missed, 125~160 now and then. Every MAKEN miss in the demos was on
        // a target held by her own bind spell (DUEL_HELD): the held robot drifts 30~70 away (and down) in the 2.8 s before the sweep -
        // 282 -> 334, 305 -> 372, 289 -> 329, 248 -> 306~318 (all missed); held at 135 / 162 it was hit. Held robots do take her bullet
        // spell (M_SHOT -8 a bullet in the demos). So MAKEN only from 150 to 280 and never on a held target (it sinks and drifts out
        // of the sweep - stepping in first did not help, see neroSwordApproach): her bullet spell instead; KENTEI missed 3 of 6 at
        // 1,725~1,785 - up to 1,600. A/B 11-08 (4 fights a build; v22 = 100~400 at the start): her MAKEN 9 casts 4 hit -> 5 casts
        // 5 hit, her damage dealt 2,604 -> 3,161, taken 2,471 -> 1,969.
        BYTE* tmo = *(BYTE**)(target + 0x398);
        const bool held = maken && tmo && !_stricmp(*(const char**)tmo, "DUEL_HELD");
        const float lo = maken ? 150.0f : 250.0f, hi = maken ? 280.0f : 1600.0f;
        if (!held && xz >= lo && xz <= hi) return NULL;
        if (held && neroSwordApproach && xz >= 140.0f && xz <= 210.0f) return NULL;   // tests only: a held robot stepped in to
        const float reach = held ? 210.0f : hi;
        if (maken && xz > reach && xz <= kApproachMax && (neroSwordApproach == 2 || (neroSwordApproach == 1 && held)))
        {
            if (neroApproach == a) return "FORWARD_G";                  // already stepping in
            neroApproach = a;
            neroApproachTarget = target;
            neroApproachFrames = neroApproachAskFrames = 0;
            neroApproachHeld = held;
            neroApproachWalked = false;
            neroApproachHi = reach;
            neroApproachFrom = xz;
            Log("NERO_APPROACH side %d asked MAKEN at %.0f%s -> FORWARD_G, then MAKEN at %.0f or nearer", side, xz,
                held ? " (held by her bind)" : "", reach);
            return "FORWARD_G";
        }
        const char* swap = held || xz > hi ? "M_SHOT" : "BQUICK_G";
        if (neroSwordLogs > 0)
        {
            --neroSwordLogs;
            if (held)
                Log("NERO_SWORD_RANGE side %d asked MAKEN at %.0f, the target held by her bind (it sinks out of the sweep) -> %s",
                    side, xz, swap);
            else
                Log("NERO_SWORD_RANGE side %d asked %s at %.0f (it hits from %.0f to %.0f) -> %s", side, name, xz, lo, hi, swap);
        }
        return swap;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

// ---- 11-03 사용자 「40% 이하에 쓰는 구두룡격만 그렇게 할래? 그리고 구두룡격은 뻗는 속도도 2배, 대미지도 3배로」 (그렇게 = 팔을 내기 전
// 준비 동안 상대 쪽으로 돌기; 구속 A_ARM2 는 그대로): PLAYER1 Kraken's 9-head dragon, any opponent, in the balance mode only (사용자
// 10-20 「판정 고침도 균형 모드에서만」). Measured 11-03 (logs duel_1103_174744, three of them against D Blood): the arm (LM047, a bullet
// on catch_Null) goes out at Kraken's frame 210 and the hand runs 18.7 a frame from 16 to 1134~1139 at 270~275, then comes back to
// about 820, lifts and swings; it caught at 44 from the lockOn; the robot took 22 at the catch and 200 in its DUEL_THROWN_HARD
// (about frame 430). The definition turns the AI fast through frames 1~30, slowly (60) through 31~180, not at all through 181~209.
// ① AI-driven (no key device at +0x700): frames 1~210 Kraken turns to the target's lockOn at up to kKuzuTurn a frame - its first
//    lock-on's speed (LOCKON(360,720)); yaw at +0x18 in radians, z axis = (sin, cos);
// ② the arm goes out twice as fast: through Kraken's frames 211~266 and the arm's SHOOT 2~57 (arm frame = Kraken frame - 209)
//    each update moves both motion frames one more, so the catch window still ends with the arm all the way out and the arm's own
//    last frames 58~61 (its BREAK at 61) run one by one; in there the definitions have only frame ranges (the chain parts'
//    "on a b": K_waiyaR_4, on from the skipped 214, was drawn by 240 like K_waiyaR_3 from 213 - 11-03 node dump);
// ③ every HP the target loses while the arm holds it (CATCH / CATCH2) or in its DUEL_HELD / DUEL_THROWN_HARD comes off twice more.
// 11-08 사용자 「하드 모드에서 적이 쓰는 거에만 배율을 높이라 말해야 했는데 우리거에도 적용되고 있었어?」→「그렇게 해 줘」: the 「40%」 of
// the 11-03 request was the hard mode's PLAYER2 hand-over, so ①②③ belong to the hard-mode enemy - PLAYER2 Kraken while the DUEL
// difficulty is hard - not to PLAYER1 (our side), which had them in every difficulty until v19.
static inline bool KuzuKraken(BYTE* a) { return *(int*)(a + 0x724) == 1 && duelDifficulty == 2; }
static const float kKuzuTurn = 0.20943951f;                             // 12 degrees a frame (720 a second)
static BYTE* kuzuOwner;              // the hard-mode PLAYER2 Kraken while it is in 9HeadDragon
static BYTE* kuzuTarget;
static BYTE* kuzuArm;
static int kuzuHp, kuzuExtra, kuzuLogs;
static bool kuzuSpeed = true;        // autobattle.ini kuzu_speed=0 (test only): the arm at its own speed
static bool kuzuTestMiss;            // autobattle.ini kuzu_test_miss=1 (test only): the arm flies 3000 off, to see its miss
static bool ImRobotIn(BYTE* a, const char* dir)                       // the robot's own definition right in dir, not a part's
{
    char path[512];
    ImDefPath(a, path, sizeof path);
    const char* p = strstr(path, dir);
    return p && !strchr(p + strlen(dir), '/');
}
static void KuzuUpdate(BYTE* n)
{
    if (!imOnryouModule || !duelBalance) return;
    __try
    {
        BYTE* mo = *(BYTE**)(n + 0x398);
        if (!mo) return;
        const char* name = *(const char**)mo;
        int* frame = (int*)(mo + 0x54);
        if (KuzuKraken(n) && ImRobotIn(n, "/character/lm/"))
        {
            if (_stricmp(name, "9HeadDragon"))
            {
                if (kuzuOwner == n)
                {
                    if (kuzuLogs > 0) { --kuzuLogs; Log("KUZU_END extra %d off the target (HP %d)", kuzuExtra, kuzuTarget ? *(int*)(kuzuTarget + 0x558) : -1); }
                    kuzuOwner = kuzuTarget = kuzuArm = NULL;
                }
                return;
            }
            BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
            if (kuzuOwner != n)
            {
                kuzuOwner = n;
                kuzuTarget = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, n) : NULL;
                kuzuArm = NULL;
                kuzuHp = kuzuTarget ? *(int*)(kuzuTarget + 0x558) : 0;
                kuzuExtra = 0;
                if (kuzuLogs > 0) { --kuzuLogs; Log("KUZU_START at %d HP, %s, target HP %d", *(int*)(n + 0x558), *(void**)(n + 0x700) ? "keys" : "AI", kuzuHp); }
            }
            if (*frame <= 210 && !*(void**)(n + 0x700) && kuzuTarget)
            {
                BYTE* lock = ((NgFindFn)(imOnryouModule + 0x15a00))(kuzuTarget, "lockOn");
                MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
                float k[16];
                memcpy(k, world(n), sizeof k);
                const float* q = lock ? world(lock) : NULL;
                float tx = q ? q[12] - k[12] : 0, tz = q ? q[14] - k[14] : 0;
                float* yaw = (float*)(n + 0x18);
                if (q && tx * tx + tz * tz > 1.0f && _finite(*yaw))
                {
                    float d = atan2f(tx, tz) - *yaw;
                    while (d > 3.14159265f) d -= 6.28318531f;
                    while (d < -3.14159265f) d += 6.28318531f;
                    d = d > kKuzuTurn ? kKuzuTurn : d < -kKuzuTurn ? -kKuzuTurn : d;
                    if (_finite(d) && fabsf(d) > 1e-4f)
                    {
                        *yaw += d;
                        n[0x94] &= 0xfc;
                        ((NgDirtyFn)(imOnryouModule + 0x15540))(n);
                    }
                }
            }
            if (kuzuSpeed && *frame >= 211 && *frame <= 266) ++*frame;
        }
        else if (kuzuOwner && *(BYTE**)(n + 0x6f8) == kuzuOwner)
        {
            char path[512];
            ImDefPath(n, path, sizeof path);
            if (strstr(path, "/lm047/"))
            {
                kuzuArm = n;
                if (kuzuSpeed && !_stricmp(name, "SHOOT") && *frame >= 2 && *frame <= 57) ++*frame;
                if (kuzuTestMiss && !_stricmp(name, "SHOOT"))
                {
                    ((float*)(n + 4))[1] = 3000.0f;
                    n[0x94] &= 0xfc;
                    ((NgDirtyFn)(imOnryouModule + 0x15540))(n);
                }
            }
        }
        if (!kuzuOwner || !kuzuTarget) return;
        int hp = *(int*)(kuzuTarget + 0x558);
        BYTE* am = kuzuArm ? *(BYTE**)(kuzuArm + 0x398) : NULL;
        BYTE* tm = *(BYTE**)(kuzuTarget + 0x398);
        const char* an = am ? *(const char**)am : "";
        const char* tn = tm ? *(const char**)tm : "";
        bool held = !_stricmp(an, "CATCH") || !_stricmp(an, "CATCH2") || !_stricmp(tn, "DUEL_HELD") || !_stricmp(tn, "DUEL_THROWN_HARD");
        if (held && hp < kuzuHp)
        {
            int lost = kuzuHp - hp, now = hp - 2 * lost < 0 ? 0 : hp - 2 * lost;
            *(int*)(kuzuTarget + 0x558) = now;
            kuzuExtra += hp - now;
            if (kuzuLogs > 0) { --kuzuLogs; Log("KUZU_DAMAGE %d lost in %s/%s -> %d more (HP %d)", lost, an, tn, hp - now, now); }
            hp = now;
        }
        kuzuHp = hp;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- 11-08 사용자 「원령 주탄은 모으는 시간을 하드에서만 절반으로」: Beelzebub's ONRYOU (원령 주탄) gathers spirits through frames 10~371 (an
// onryo bullet made and shot every 20 frames, aborted at 400), makes its curse bullet at 401/402 and fires it at 560 (the bullet's
// MOTION:SHOOT message) - 560 frames, 9.3 s, before the shot (the same block in all 18 hard-mode copies). For the hard-mode enemy
// (PLAYER2, DUEL difficulty 2, balance mode) each update in 1~558 moves the motion one frame more, as KuzuUpdate does for the arm -
// unless the frame it would skip carries a command (1~3, 10/11 + 20n up to 370/371, 60, 400~402, 560), so nothing it does is lost
// and the charge takes about half the time; after the shot it runs at its own speed.
static bool OnryouCommandFrame(int f)
{
    if (f <= 3 || f == 60 || (f >= 400 && f <= 402) || f == 560) return true;
    return f >= 10 && f <= 371 && ((f - 10) % 20 == 0 || (f - 11) % 20 == 0);
}
static BYTE* onryouActor;
static int onryouUpdates, onryouLastFrame, onryouFastLogs;
static void OnryouFast(BYTE* a)
{
    if (!imOnryouModule || !duelBalance || duelDifficulty != 2) return;
    __try
    {
        if (*(int*)(a + 0x724) != 1) return;
        BYTE* mo = *(BYTE**)(a + 0x398);
        if (!mo || _stricmp(*(const char**)mo, "ONRYOU") || !ImRobotIn(a, "/character/im/")) return;
        int* frame = (int*)(mo + 0x54);
        if (onryouActor != a || *frame < onryouLastFrame) { onryouActor = a; onryouUpdates = 0; }
        ++onryouUpdates;
        if (*frame >= 1 && *frame <= 558 && !OnryouCommandFrame(*frame + 1)) ++*frame;
        if (onryouLastFrame < 560 && *frame >= 560 && onryouFastLogs > 0)
        {
            --onryouFastLogs;
            Log("ONRYOU_FAST hard-mode Beelzebub's curse bullet goes at frame 560 after %d updates (its own speed: 560)", onryouUpdates);
        }
        onryouLastFrame = *frame;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- 11-03 사용자 「대부분은 별 문제 없는데 대기는 네로,클락 웍 팬텀 VS 지상전처럼 고도가 안맞는 경우야 이거만 잡으면 대부분의 문제는
// 거의해결될거야」: Nero's Nameless One and the Clockwork Phantom stay where they spawn, up in the air - their own battles are air
// battles, the air Demonbane flying to their chest. A robot that cannot fly (no UP_A in its definition) stays on its floor with
// its lock node far under theirs (11-03 trace, hard: Nero's 197 above the ground Demonbane's, the Phantom's last form 251 above -
// the sword passed under it, 80 of its 2000 HP in 90 s). In a DUEL fight in the balance mode such a robot rises (or sinks) to the
// floater's lock node height, as the air Demonbane would: at most kAltSpeed a frame, starting when the gap is over kAltStart and
// stopping within kAltStop. The floater's lock node bobs with its animation (the Phantom's first form 34-159 over its root), so
// the height followed is a slow average of it. Robots that fly keep their own AI's height.
// Its AI still saw the floater far above (lifted level with the Phantom's last form, 55 apart, the ground Demonbane asked no
// sword combo in 120 s): the AI's target point (0x61d00, where its distance 0x62540, height 0x62150 and approach 0x622a0 start)
// is the floater's root. For that robot's AI the point becomes where its own root would stand with its lock node on the
// floater's - the floater's lock node less its own lock node's height over its root (AltAimPoint, a jump at 0x61d00 for the
// fight; every other AI gets the native point).
static const float kAltSpeed = 5.0f, kAltStart = 60.0f, kAltStop = 10.0f, kAltFollow = 0.03f;
static bool altLift = true;          // alt_lift=0 (test) leaves the heights as the game puts them
static struct { BYTE* actor; float floaterY; bool moving; } altState[2];
static struct { BYTE* actor; BYTE* target; bool pair; } altPair[2];
static int altLogs;
static bool ImFloater(BYTE* a)       // Nero's Nameless One and both forms of the Clockwork Phantom
{
    return ImRobotIn(a, "/character/qm/") || ImRobotIn(a, "/character/rm/") || ImRobotIn(a, "/character/sm/");
}
static BYTE* AltFloaterOf(BYTE* a)   // the floater a robot that cannot fly faces in a DUEL fight in the balance mode, or NULL
{
    if (!altLift || !imOnryouModule || !duelBalance || !a) return NULL;
    int side = *(int*)(a + 0x724);
    if ((side != 0 && side != 1) || !*(BYTE**)(a + 0xb84) || *(int*)(a + 0x558) <= 0 || *(void**)(a + 0x98)) return NULL;
    BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
    BYTE* t = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, a) : NULL;
    if (!t || !*(BYTE**)(t + 0xb84) || *(int*)(t + 0x558) <= 0) return NULL;   // a knocked-out floater is not followed
    auto& c = altPair[side];
    if (c.actor != a || c.target != t)                                  // worked out once for the two
    {
        c.actor = a;
        c.target = t;
        c.pair = ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, t) == a &&   // the two robots, not a part
                 ImFloater(t) && !ImFloater(a) && !ImHasMotion(a, "UP_A");
    }
    return c.pair ? t : NULL;
}
static void AltLift(BYTE* a)
{
    __try
    {
        BYTE* t = AltFloaterOf(a);
        if (!t) return;
        int side = *(int*)(a + 0x724);
        BYTE* lock = *(BYTE**)(a + 0xb84);
        BYTE* tlock = *(BYTE**)(t + 0xb84);
        BYTE* m = *(BYTE**)(a + 0x398);
        const char* now = m ? *(const char**)m : "";
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        float want = world(tlock)[13], have = world(lock)[13];
        if (!_finite(want) || !_finite(have)) return;
        auto& s = altState[side];
        if (s.actor != a) { s.actor = a; s.floaterY = want; s.moving = false; }
        // only the floater's own height: not while a grab carries it or a hit throws it about (11-03 trace: Kraken's 9-head
        // dragon lifted Nero 500 and Kraken followed her up to 700)
        BYTE* tm = *(BYTE**)(t + 0x398);
        const char* their = tm ? *(const char**)tm : "";
        if (_strnicmp(their, "DUEL_", 5) && _strnicmp(their, "DAMAGE", 6) && _strnicmp(their, "DOWN", 4) &&
            _strnicmp(their, "H_", 2) && _stricmp(their, "WAKE_UP"))
            s.floaterY += (want - s.floaterY) * kAltFollow;
        if (!_strnicmp(now, "START", 5) || !_strnicmp(now, "DUEL_", 5)) return;   // the opening, and a grab carrying it
        float gap = s.floaterY - have;
        if (!s.moving && fabsf(gap) > kAltStart)
        {
            s.moving = true;
            if (altLogs > 0)
            {
                --altLogs;
                char path[512];
                ImDefPath(a, path, sizeof path);
                const char* file = strrchr(path, '/');
                Log("ALT_LIFT side %d %s %s to the floater's lock node, %.0f %s", side, file ? file + 1 : path, gap > 0 ? "rises" : "sinks",
                    fabsf(gap), gap > 0 ? "above" : "below");
            }
        }
        if (!s.moving) return;
        if (fabsf(gap) < kAltStop) { s.moving = false; return; }
        float* pos = (float*)(a + 4);
        pos[1] += gap > kAltSpeed ? kAltSpeed : gap < -kAltSpeed ? -kAltSpeed : gap;
        a[0x94] &= 0xfc;
        ((ImDirtyFn)(imOnryouModule + 0x15540))(a);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// 0x61d00 in full (its target's root: (0,0,0,1) through the target's world matrix; (0,0,0,1) with no target), and the lifted
// robot's point as above
static BYTE* altAimModule;
static float* __fastcall AltAimPoint(BYTE* ai, void*, float* out)
{
    BYTE* m = altAimModule;
    BYTE* a = *(BYTE**)(ai + 4);
    BYTE* t = ((MmTarget_t)(m + 0x39d60))(*(BYTE**)(m + 0x11e3b0), a);
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    if (!t) return out;
    MmWorld_t world = (MmWorld_t)(m + 0x15930);
    const float* w = world(t);
    out[0] = w[12]; out[1] = w[13]; out[2] = w[14]; out[3] = w[15];
    __try
    {
        if (m == imOnryouModule && AltFloaterOf(a) == t)
        {
            const float* tl = world(*(BYTE**)(t + 0xb84));
            float sl[16], sr[16];
            memcpy(sl, world(*(BYTE**)(a + 0xb84)), sizeof sl);
            memcpy(sr, world(a), sizeof sr);
            for (int i = 0; i < 3; ++i) out[i] = tl[12 + i] - (sl[12 + i] - sr[12 + i]);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = w[12]; out[1] = w[13]; out[2] = w[14]; }
    return out;
}
// on: 0x61d00 jumps to AltAimPoint (only over the expected code); off: the jump is taken out again
static void SetAltAim(BYTE* m, bool on)
{
    static const BYTE head[6] = {0x83, 0xec, 0x20, 0x8b, 0x41, 0x04};   // sub esp,20h; mov eax,[ecx+4]
    BYTE* at = m + 0x61d00;
    bool ours = at[0] == 0xe9 && at + 5 + *(LONG*)(at + 1) == (BYTE*)AltAimPoint;
    if (on && !ours)
    {
        if (memcmp(at, head, sizeof head)) { Log("ALT_AIM off: demonbane.dll+0x61d00 is not the expected code"); return; }
        BYTE jmp[5] = {0xe9};
        *(LONG*)(jmp + 1) = (LONG)((BYTE*)AltAimPoint - (at + 5));
        altAimModule = m;
        if (!WriteCode(at, jmp, 5)) Log("ALT_AIM off: could not write demonbane.dll+0x61d00 (error %lu)", GetLastError());
    }
    else if (!on && ours) WriteCode(at, head, 5);
}

// ---- test only (autobattle.ini alt_trace=1, 11-03 사용자 「대개는 네로, 클락 웍 팬텀 VS 지상전처럼 고도가 안맞는 경우야」): twice a
// second, each robot of a DUEL fight - its root and lock node height, and how far the target's lock node is from its own.
static bool altTrace;
static DWORD altTraceAt[2];
static void AltTraceActor(BYTE* a)
{
    if (!altTrace || !imOnryouModule) return;
    __try
    {
        int side = *(int*)(a + 0x724);
        BYTE* lock = *(BYTE**)(a + 0xb84);
        if ((side != 0 && side != 1) || !lock || *(int*)(a + 0x558) <= 0) return;
        char path[512];
        ImDefPath(a, path, sizeof path);
        const char* c = strstr(path, "/character/");
        const char* slash = c ? strchr(c + 11, '/') : NULL;
        if (!slash || strchr(slash + 1, '/')) return;                   // the robot itself, not one of its parts
        DWORD now = GetTickCount();
        if (now - altTraceAt[side] < 500) return;
        altTraceAt[side] = now;
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        float p[16], l[16], tp[16] = {}, tl[16] = {};
        memcpy(p, world(a), sizeof p);
        memcpy(l, world(lock), sizeof l);
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        BYTE* t = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, a) : NULL;
        BYTE* tlock = t ? *(BYTE**)(t + 0xb84) : NULL;
        if (tlock)
        {
            memcpy(tp, world(t), sizeof tp);
            memcpy(tl, world(tlock), sizeof tl);
        }
        BYTE* m = *(BYTE**)(a + 0x398);
        Log("ALT side %d %s %s@%d hp %d | root %.0f,%.0f,%.0f lock %.0f (+%.0f) | target lock dy %.0f xz %.0f, root dy %.0f",
            side, slash + 1, m ? *(const char**)m : "-", m ? *(int*)(m + 0x54) : -1, *(int*)(a + 0x558), p[12], p[13], p[14],
            l[13], l[13] - p[13], tlock ? tl[13] - l[13] : 0.0f,
            tlock ? sqrtf((tl[12] - l[12]) * (tl[12] - l[12]) + (tl[14] - l[14]) * (tl[14] - l[14])) : -1.0f,
            tlock ? tp[13] - p[13] : 0.0f);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- test only (autobattle.ini sword_trace=1, 11-08 사용자 「우리 네로가 적에게 쓰면 하나도 안 맞았어」「우리 네로의 인공지능 문제인가?」):
// every other frame, each sword of Nero's sword spell (MAKEN: the ngp_maken / qm046 bullets on maken_Null and maken_Null2) and of
// Lightbringer (KENTEI: the qm050 bullets on Ken_L/R_Null..4) - its motion@frame and world position, its target robot's root and
// lock node, and the 3D / height / flat distance from the sword to that lock node; and every 10th frame of Nero's own MAKEN / KENTEI
// her root, yaw (+0x18) and the target - where the swords go against where the enemy is.
static bool swordTrace;
static int swordTraceLines;
static void SwordTraceActor(BYTE* a)
{
    if (!swordTrace || !imOnryouModule || swordTraceLines <= 0) return;
    __try
    {
        char path[512];
        ImDefPath(a, path, sizeof path);
        const char* f = strrchr(path, '/');
        f = f ? f + 1 : path;
        BYTE* m = *(BYTE**)(a + 0x398);
        const char* mn = m ? *(const char**)m : "-";
        int frame = m ? *(int*)(m + 0x54) : -1;
        const bool sword = !strncmp(f, "ngp_maken", 9) || !strncmp(f, "qm046", 5) || !strncmp(f, "qm050", 5);
        const bool nero = !sword && strstr(path, "/character/qm/") != NULL && (!_stricmp(mn, "MAKEN") || !_stricmp(mn, "KENTEI"));
        if (!sword && !nero) return;
        if (sword ? (frame & 1) != 0 : frame % 10 != 0) return;
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        float p[16];
        memcpy(p, world(a), sizeof p);
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        BYTE* t = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, a) : NULL;
        BYTE* tlock = t ? *(BYTE**)(t + 0xb84) : NULL;
        float tp[16] = {}, tl[16] = {};
        if (t) memcpy(tp, world(t), sizeof tp);
        if (tlock) memcpy(tl, world(tlock), sizeof tl);
        --swordTraceLines;
        if (nero)
        {
            Log("SWORD_NERO %p %s %s@%d side %d | root %.0f,%.0f,%.0f yaw %.3f | target %p root %.0f,%.0f,%.0f lock %.0f,%.0f,%.0f",
                a, f, mn, frame, *(int*)(a + 0x724), p[12], p[13], p[14], *(float*)(a + 0x18), t, tp[12], tp[13], tp[14],
                tl[12], tl[13], tl[14]);
            // 11-08: where the sword's nodes are (a third bullet on a node in the blade's middle would fill the 430~930 gap)
            static const char* const kNodes[] = {"maken_Null", "maken_Null2", "maken_Null3", "maken", "maken2", "Ken_L_Null",
                "Ken_L_Null2", "Ken_L_Null3", "Ken_L_Null4", "kenteiL", "kenteiL2", "kentei01", "kentei02", "kentei"};
            char line[1400];
            int at = 0;
            for (const char* name : kNodes)
            {
                BYTE* node = ((NgFindFn)(imOnryouModule + 0x15a00))(a, name);
                if (!node || at > (int)sizeof line - 80) continue;
                const float* w = world(node);
                at += sprintf_s(line + at, sizeof line - at, " %s %.0f,%.0f,%.0f", name, w[12], w[13], w[14]);
            }
            Log("SWORD_NODES %s@%d root %.0f,%.0f,%.0f yaw %.3f |%s", mn, frame, p[12], p[13], p[14], *(float*)(a + 0x18), at ? line : " none");
            return;
        }
        const float dx = tl[12] - p[12], dy = tl[13] - p[13], dz = tl[14] - p[14];
        BYTE* tbody = t ? ((NgFindFn)(imOnryouModule + 0x15a00))(t, "collid_body") : NULL;
        float tb[16] = {};
        if (tbody) memcpy(tb, world(tbody), sizeof tb);
        const float bx = tb[12] - p[12], by = tb[13] - p[13], bz = tb[14] - p[14];
        Log("SWORD %p %s %s@%d side %d | at %.0f,%.0f,%.0f | target %p root %.0f,%.0f,%.0f lock %.0f,%.0f,%.0f | to lock d3 %.0f dy %.0f xz %.0f"
            " | body %d %.0f,%.0f,%.0f d3 %.0f",
            a, f, mn, frame, *(int*)(a + 0x724), p[12], p[13], p[14], t, tp[12], tp[13], tp[14], tl[12], tl[13], tl[14],
            sqrtf(dx * dx + dy * dy + dz * dz), dy, sqrtf(dx * dx + dz * dz), tbody ? 1 : 0, tb[12], tb[13], tb[14],
            tbody ? sqrtf(bx * bx + by * by + bz * bz) : -1.0f);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- test only (autobattle.ini zyudan_trace=1 - every 3 updates of the Beelzebuth robot, 11-08 사용자 「내가 원령주탄 아이온에게 쓰는거 보니까 원령 주탄이 여기저기 돌면서 정작
// 아이온에게 닿지를 않더라」): every 6 updates of a Beelzebuth, its curse bullets (IM zyudan - the homing CATCH missile 원령 주탄 fires
// at frame 560) from its material lists as IceTrace walks them - motion@frame, the live word (+0x74 of the motion), world position,
// whom +0xbe4 points at - with the target robot's root and lock node and Beelzebuth's root
static bool zyudanTrace;
static int zyudanTraceLines;
static void ZyudanTraceActor(BYTE* a)
{
    static int tick;
    if (!zyudanTrace || zyudanTraceLines <= 0 || !imOnryouModule || !a) return;
    __try
    {
        if (!ImRobotIn(a, "/character/im/") || (++tick % 3)) return;   // the robot only (its bullets pass here while ACTIVE)
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, a) : NULL;
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        BYTE* mats = *(BYTE**)(a + 0x6f4);
        BYTE* begin = mats ? *(BYTE**)(mats + 8) : NULL;
        BYTE* end = mats ? *(BYTE**)(mats + 12) : NULL;
        if (!begin || end < begin || (end - begin) % 0x9c || end - begin > 0x9c * 256) return;
        for (BYTE* mat = begin; mat < end; mat += 0x9c)
        {
            BYTE** lo = *(BYTE***)(mat + 0x90);
            BYTE** hi = *(BYTE***)(mat + 0x94);
            if (!lo || hi < lo || hi - lo > 256) continue;
            for (BYTE** p = lo; p < hi; ++p)
            {
                BYTE* n = *p;
                if (!n) continue;
                char path[512];
                ImDefPath(n, path, sizeof path);
                BYTE* bm = *(BYTE**)(n + 0x398);
                if (!strstr(path, "/zyudan/") || !bm) continue;
                const float* b = world(n);
                BYTE* held = *(BYTE**)(n + 0xbe4);
                const float* tr = target ? world(target) : NULL;
                BYTE* tl = target ? *(BYTE**)(target + 0xb84) : NULL;
                const float* tp = tl ? world(tl) : NULL;
                const float* o = world(a);
                const float* raw = (const float*)(n + 4);
                BYTE* ballNode = ((NgFindFn)(imOnryouModule + 0x15a00))(n, "master_Null");   // v25: the ball one sees (DMO-pushed ahead)
                float bw[16] = {};
                if (ballNode) memcpy(bw, world(ballNode), sizeof bw);
                Log("ZYUDAN_TRACE %s@%d live %d bullet %.0f,%.0f,%.0f | +0xbe4 %s | target root %.0f,%.0f,%.0f lock %.0f,%.0f,%.0f | "
                    "owner %.0f,%.0f,%.0f | parent %p +4 %.0f,%.0f,%.0f | bal %d | ball %.0f,%.0f,%.0f", *(const char**)bm, *(int*)(bm + 0x54),
                    *(int*)(bm + 0x74), b[12], b[13], b[14],
                    held == target && held ? "target" : held ? "other" : "none", tr ? tr[12] : 0.0f, tr ? tr[13] : 0.0f,
                    tr ? tr[14] : 0.0f, tp ? tp[12] : 0.0f, tp ? tp[13] : 0.0f, tp ? tp[14] : 0.0f, o[12], o[13], o[14],
                    *(void**)(n + 0x98), raw[0], raw[1], raw[2], (int)duelBalance, bw[12], bw[13], bw[14]);
                --zyudanTraceLines;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- 11-08 v24 사용자 「원령 주탄이 여기저기 돌면서 정작 아이온에게 닿지를 않더라」→「고쳐 줘」(「원래 원령주탄은 데몬베인 전에서 쓰는 거라서
// 안 맞을 리가 없는데 이상하구나」): the curse bullet (IM zyudan - a CATCH missile) leaves Beelzebuth's zhyudan_Null level, about 270
// over its root, and its own guidance turns slowly. A DUEL fight starts the two 300 apart, so against a robot standing on the ground
// (lock node about 50 over its feet - Aion 50, Demonbane 54) it went over the head some 210 above the lock node, dived only past it
// and circled until its 500 frames ran out (zyudan_trace, logs duel_1108_112351); Nero (lock node 351, a big body) is caught at once.
// In a DUEL fight in the balance mode (a hit fix - 사용자 10-20 「판정 고침도 균형 모드에서만」), while the bullet flies (SHOOT, no parent)
// above the target's lock node, its height is pulled onto the straight line to that node: each update it drops (its height over the
// node) x (its flat step / its flat distance), at most kZyudanDive - as if it had been aimed at the lock node, not level. Its flat path,
// speed and catch stay the engine's. Called from Beelzebuth's own update (its material lists hold the bullet; a flying bullet does not
// pass the per-actor hook). autobattle.ini zyudan_steer: 1 (default) / 0.
static const float kZyudanDive = 12.0f;
static bool zyudanSteer = true;
static int zyudanSteerLogs;
static BYTE* zyudanSteerLast;
static float zyudanSteerXZ[2];
static int zyudanSteerShot;               // the SHOOT frame last seen (a new shot logs again)
static void ZyudanSteer(BYTE* a)
{
    if (!zyudanSteer || !imOnryouModule || !duelBalance || !a) return;
    __try
    {
        if (!ImRobotIn(a, "/character/im/")) return;
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        BYTE* target = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, a) : NULL;
        BYTE* tl = target ? *(BYTE**)(target + 0xb84) : NULL;
        if (!tl) return;
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        const float* tp = world(tl);
        if (!_finite(tp[12]) || !_finite(tp[13]) || !_finite(tp[14])) return;
        BYTE* mats = *(BYTE**)(a + 0x6f4);
        BYTE* begin = mats ? *(BYTE**)(mats + 8) : NULL;
        BYTE* end = mats ? *(BYTE**)(mats + 12) : NULL;
        if (!begin || end < begin || (end - begin) % 0x9c || end - begin > 0x9c * 256) return;
        for (BYTE* mat = begin; mat < end; mat += 0x9c)
        {
            BYTE** lo = *(BYTE***)(mat + 0x90);
            BYTE** hi = *(BYTE***)(mat + 0x94);
            if (!lo || hi < lo || hi - lo > 256) continue;
            for (BYTE** p = lo; p < hi; ++p)
            {
                BYTE* n = *p;
                if (!n) continue;
                BYTE* bm = *(BYTE**)(n + 0x398);
                if (!bm || _stricmp(*(const char**)bm, "SHOOT")) continue;
                char path[512];
                ImDefPath(n, path, sizeof path);
                if (!strstr(path, "/zyudan/")) continue;
                // hard fights (the shortened charge): the flying bullet stays on its parent node (+0x98, logs duel_1108_165338 - local +4
                // 0,0,-7 -> -29) - then the drop goes into its local position on the parent's axes, as ArmAim does
                BYTE* parent = *(BYTE**)(n + 0x98);
                float* pos = (float*)(n + 4);
                float w[16];
                memcpy(w, world(n), sizeof w);
                const int frame = *(int*)(bm + 0x54);
                const bool fresh = zyudanSteerLast != n || frame < zyudanSteerShot;
                float step = 7.3f;                                      // its flat speed a frame (zyudan_trace)
                if (!fresh)
                {
                    const float sx = w[12] - zyudanSteerXZ[0], sz = w[14] - zyudanSteerXZ[1];
                    step = sqrtf(sx * sx + sz * sz);
                    if (!_finite(step) || step > 40.0f) step = 7.3f;
                }
                zyudanSteerLast = n;
                zyudanSteerShot = frame;
                zyudanSteerXZ[0] = w[12];
                zyudanSteerXZ[1] = w[14];
                const float dy = w[13] - tp[13];
                if (!(dy > 0.0f)) continue;                              // at or under the lock node: the engine's own
                const float dx = tp[12] - w[12], dz = tp[14] - w[14];
                const float dxz = sqrtf(dx * dx + dz * dz);
                float drop = dxz > 1.0f ? dy * step / dxz : dy;
                if (drop > kZyudanDive) drop = kZyudanDive;
                if (drop > dy) drop = dy;
                if (fresh && zyudanSteerLogs > 0)
                {
                    --zyudanSteerLogs;
                    Log("ZYUDAN_STEER the curse bullet flies %.0f over the target's lock node, %.0f away flat - kept on the line to it%s",
                        dy, dxz, parent ? " (on its parent node)" : "");
                }
                if (!parent)
                    pos[1] -= drop;
                else
                {
                    float pm[16];
                    memcpy(pm, world(parent), sizeof pm);
                    for (int i = 0; i < 3; ++i)
                    {
                        const float* axis = pm + 4 * i;
                        const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
                        if (len2 > 1e-6f) pos[i] -= drop * axis[1] / len2;
                    }
                }
                n[0x94] &= 0xfc;
                ((ImDirtyFn)(imOnryouModule + 0x15540))(n);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- 11-09 v25 사용자 「원령 주탄은 데몬베인은 안에 푹 잠기게 하는데 다른 로봇은 표면에 닿고 끝나는 차이가 있어 고칠 수 있어?」: the curse
// bullet's ball is big (zhyudan1 radius 144 at scale 1, im/zyudan/zyudan.dmo) and it catches from about 200 off (logs duel_1108_170342:
// held at 178 over the ground and 178 in front of Aion's root - 219 from its lock node; Nero 214~255), and pack 544's BREAK keeps the
// ball where it caught - so it only touches the robot. Demonbane's own DAMAGE_onryou draws its model's curse sphere (radius 212, on
// tama_Null 56 over its root - by its lock node) around its body. The held robot does not follow the bullet (Aion and Nero stood where
// they were through BREAK), so while a DUEL curse bullet (zyudandv / zyudanvs - the ones pack 544 shows) is in BREAK holding a robot
// (+0xbe4), it is carried into that robot's lock node: a quarter of the way an update (at least kZyudanHoldStep), then kept on it (the
// held pose moves the node a little). Both modes - it goes with pack 544's visible hold, which both show. Demonbane's copies go to
// BREAK_PLAIN (invisible) in 2~8 frames and are left. autobattle.ini zyudan_hold: 1 (default) / 0.
static const float kZyudanHoldStep = 24.0f;
static bool zyudanHold = true;
static int zyudanHoldLogs;
static BYTE* zyudanHoldLast;
static void ZyudanHold(BYTE* a)
{
    if (!zyudanHold || !imOnryouModule || !a) return;
    __try
    {
        if (!ImRobotIn(a, "/character/im/")) return;
        MmWorld_t world = (MmWorld_t)(imOnryouModule + 0x15930);
        BYTE* mats = *(BYTE**)(a + 0x6f4);
        BYTE* begin = mats ? *(BYTE**)(mats + 8) : NULL;
        BYTE* end = mats ? *(BYTE**)(mats + 12) : NULL;
        if (!begin || end < begin || (end - begin) % 0x9c || end - begin > 0x9c * 256) return;
        for (BYTE* mat = begin; mat < end; mat += 0x9c)
        {
            BYTE** lo = *(BYTE***)(mat + 0x90);
            BYTE** hi = *(BYTE***)(mat + 0x94);
            if (!lo || hi < lo || hi - lo > 256) continue;
            for (BYTE** p = lo; p < hi; ++p)
            {
                BYTE* n = *p;
                if (!n) continue;
                BYTE* bm = *(BYTE**)(n + 0x398);
                if (!bm || _stricmp(*(const char**)bm, "BREAK")) continue;
                char path[512];
                ImDefPath(n, path, sizeof path);
                if (!strstr(path, "/zyudan/zyudandv") && !strstr(path, "/zyudan/zyudanvs")) continue;
                BYTE* held = *(BYTE**)(n + 0xbe4);
                BYTE* lock = held ? *(BYTE**)(held + 0xb84) : NULL;
                if (!lock) continue;
                const float* tp = world(lock);
                // the ball one sees (and its catch collision) is master_Null, which the bullet's DMO SHOOT pushes 14.4 a frame along its
                // local -z ahead of the bullet (zyudan.dmo: 0 -> -7200 over 500 frames) and which keeps that place through BREAK - so the
                // ball is brought to the node, not the bullet (11-09 first v25 3fed0833 carried the bullet: the ball went some 230 past the
                // robot, toward the camera - a red flash, then nothing to see)
                BYTE* ball = ((NgFindFn)(imOnryouModule + 0x15a00))(n, "master_Null");
                float w[16];
                memcpy(w, world(ball ? ball : n), sizeof w);
                float d[3] = {tp[12] - w[12], tp[13] - w[13], tp[14] - w[14]};
                const float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (!_finite(dist) || dist < 0.5f || dist > 3000.0f) continue;
                float len = dist * 0.25f;
                if (len < kZyudanHoldStep) len = kZyudanHoldStep;
                if (len > dist) len = dist;
                const float k = len / dist;
                for (float& v : d) v *= k;
                BYTE* parent = *(BYTE**)(n + 0x98);
                float* pos = (float*)(n + 4);
                if (zyudanHoldLast != n && zyudanHoldLogs > 0)
                {
                    --zyudanHoldLogs;
                    const float* nb = world(n);
                    Log("ZYUDAN_HOLD %s@%d the curse ball held a robot %.0f from its lock node (ball %.0f,%.0f,%.0f, node %.0f,%.0f,%.0f,"
                        " bullet %.0f,%.0f,%.0f%s) - carried into it%s", *(const char**)bm, *(int*)(bm + 0x54), dist, w[12], w[13], w[14],
                        tp[12], tp[13], tp[14], nb[12], nb[13], nb[14], ball ? "" : " - no master_Null", parent ? " (on its parent node)" : "");
                }
                zyudanHoldLast = n;
                if (!parent)
                    for (int i = 0; i < 3; ++i) pos[i] += d[i];
                else
                {
                    float pm[16];
                    memcpy(pm, world(parent), sizeof pm);
                    for (int i = 0; i < 3; ++i)
                    {
                        const float* axis = pm + 4 * i;
                        const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
                        if (len2 > 1e-6f) pos[i] += (axis[0] * d[0] + axis[1] * d[1] + axis[2] * d[2]) / len2;
                    }
                }
                n[0x94] &= 0xfc;
                ((ImDirtyFn)(imOnryouModule + 0x15540))(n);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- test only (autobattle.ini test_gap=N, 11-08 「원래 원령주탄은 데몬베인 전에서 쓰는 거라서」): when PLAYER2's robot first stands
// (STOP_G / STOP) in a DUEL fight, it is put N away from PLAYER1 on the starting line (the fights start 300 apart) - to see a move
// from farther off, as story fights start
static int testGap;
static bool testGapDone;
static void TestGapActor(BYTE* a)
{
    if (testGap <= 0 || testGapDone || !imOnryouModule || !a) return;
    __try
    {
        if (*(int*)(a + 0x724) != 1) return;
        char path[512];
        ImDefPath(a, path, sizeof path);
        const char* c = strstr(path, "/character/");
        const char* s = c ? strchr(c + 11, '/') : NULL;
        if (!s || strchr(s + 1, '/')) return;                           // the robot, not a bullet or a part
        const char* now = MotionName(a);
        if (!now || (_stricmp(now, "STOP_G") && _stricmp(now, "STOP"))) return;
        BYTE* manager = *(BYTE**)(imOnryouModule + 0x11e3b0);
        BYTE* other = manager ? ((MmTarget_t)(imOnryouModule + 0x39d60))(manager, a) : NULL;
        if (!other) return;
        float* pos = (float*)(a + 4);
        const float* o = (const float*)(other + 4);
        const float was = pos[2];
        pos[2] = o[2] + (pos[2] >= o[2] ? 1.0f : -1.0f) * (float)testGap;
        a[0x94] &= 0xfc;
        ((ImDirtyFn)(imOnryouModule + 0x15540))(a);
        testGapDone = true;
        Log("TEST_GAP PLAYER2 moved from z %.0f to %.0f (%d from PLAYER1)", was, pos[2], testGap);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

typedef void(__thiscall* ImUpdateFn)(BYTE*);
static void __fastcall ImActorUpdate(BYTE* a, void*)                  // the per-actor update when the hard mode has not hooked it
{
    GrabTrace(a, "before");
    KokuinNoGrab(a);                                                    // before the update reads her interrupts
    ((ImUpdateFn)(imOnryouModule + 0x333a0))(a);
    GrabTrace(a, "after");
    IceFollow(a);
    ArmAim(a);
    BladeHit(a);
    KuzuUpdate(a);
    OnryouFast(a);
    NeroApproachStep(a);
    AltLift(a);
    IceTraceActor(a);
    AltTraceActor(a);
    SwordTraceActor(a);
    ZyudanSteer(a);
    ZyudanHold(a);
    ZyudanTraceActor(a);
    TestGapActor(a);
}

// the native ask-by-name (0x351b0) with the swaps - the hooks at 0x6201a ask through this
typedef bool(__thiscall* ImAskFn)(BYTE*, const char*, int);
static bool AiAsk(BYTE* module, BYTE* a, const char* name, int slot)
{
    if (imOnryouModule == module) IceTrace(module, a);
    if (name && kuzuTestAsk && module == imOnryouModule && !_stricmp(name, "A_ARM2") && KuzuKraken(a) &&
        ImDefIn(a, "/character/lm/"))
        name = "9HeadDragon";                                           // test only: the 9-head dragon early in the fight
    if (name)
        if (const char* height = A2NeroHeight(module, a, name)) name = height;
    if (name)
        if (const char* closer = KrakenGrabRange(module, a, name)) name = closer;
    if (name)
        if (const char* keep = NeroApproachAsk(module, a, name)) name = keep;
    if (name)
        if (const char* toward = NeroSwordRange(module, a, name)) name = toward;
    if (name && !_stricmp(name, "TAIZAI") && ImVsMm(a))
    {
        if (imOnryouLogs > 0 && slot == 0)
        {
            --imOnryouLogs;
            Log("IM_ONRYOU side %d asked TAIZAI at %d MP, %d HP -> ONRYOU", *(int*)(a + 0x724), *(int*)(a + 0x55c), *(int*)(a + 0x558));
        }
        name = "ONRYOU";
    }
    bool ok = ((ImAskFn)(module + 0x351b0))(a, name, slot);
    if (aiAskTrace > 0 && imOnryouModule == module && name)
    {
        --aiAskTrace;
        float xz = -1, dy = 0;
        __try
        {
            BYTE* manager = *(BYTE**)(module + 0x11e3b0);
            BYTE* target = manager ? ((MmTarget_t)(module + 0x39d60))(manager, a) : NULL;
            if (target)
            {
                const float* p = ((MmWorld_t)(module + 0x15930))(a);
                const float* q = ((MmWorld_t)(module + 0x15930))(target);
                xz = sqrtf((q[12] - p[12]) * (q[12] - p[12]) + (q[14] - p[14]) * (q[14] - p[14]));
                dy = q[13] - p[13];
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        BYTE* m = *(BYTE**)(a + 0x398);
        Log("AI_ASK %lu side %d %s slot %d -> %d (now %s, xz %.0f dy %.0f, MP %d)", GetTickCount() - aiAskTraceStart,
            *(int*)(a + 0x724), name, slot, (int)ok, m ? *(const char**)m : "-", xz, dy, *(int*)(a + 0x55c));
    }
    return ok;
}
static bool __fastcall ImOnryouRequest(BYTE* a, void*, const char* name, int slot)
{
    return AiAsk(imOnryouModule, a, name, slot);
}

// each fight start, after InstallHandover: when nothing else sits at the ask-by-name call (handover=0, no mix), this does
static void InstallImOnryou(HMODULE module, bool duel)
{
    imOnryouModule = duel && module ? (BYTE*)module : NULL;
    imOnryouLogs = 8;
    a2NeroLogs = 12;
    krakenGrabLogs = 6;
    armAimLast = NULL;
    kuzuOwner = kuzuTarget = kuzuArm = NULL;
    kuzuLogs = 16;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    aiAskTrace = imOnryouModule ? GetPrivateProfileIntW(L"autobattle", L"ai_ask_trace", 0, ini) : 0;
    iceTrace = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"ice_trace", 0, ini) != 0;
    krakenGrabReach = (float)GetPrivateProfileIntW(L"autobattle", L"kraken_grab_reach", (int)kKrakenGrabReach, ini);
    kuzuTestAsk = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"kuzu_test_ask", 0, ini) != 0;
    kuzuSpeed = GetPrivateProfileIntW(L"autobattle", L"kuzu_speed", 1, ini) != 0;
    kuzuTestMiss = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"kuzu_test_miss", 0, ini) != 0;
    altTrace = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"alt_trace", 0, ini) != 0;
    altTraceAt[0] = altTraceAt[1] = 0;
    swordTrace = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"sword_trace", 0, ini) != 0;
    swordTraceLines = 9000;
    zyudanTrace = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"zyudan_trace", 0, ini) != 0;
    zyudanTraceLines = 4000;
    zyudanSteer = GetPrivateProfileIntW(L"autobattle", L"zyudan_steer", 1, ini) != 0;
    zyudanSteerLogs = 12;
    zyudanHold = GetPrivateProfileIntW(L"autobattle", L"zyudan_hold", 1, ini) != 0;
    zyudanHoldLogs = 12;
    zyudanSteerLast = NULL;
    testGap = imOnryouModule ? (int)GetPrivateProfileIntW(L"autobattle", L"test_gap", 0, ini) : 0;
    testGapDone = false;
    bladeHit = (int)GetPrivateProfileIntW(L"autobattle", L"blade_hit", 0, ini);
    bladeHitLast = NULL;
    bladeHitLogs = 40;
    neroSwordRange = (int)GetPrivateProfileIntW(L"autobattle", L"nero_sword_range", 2, ini);
    neroSwordLogs = 30;
    neroSwordApproach = (int)GetPrivateProfileIntW(L"autobattle", L"nero_sword_approach", 0, ini);
    neroApproach = NULL;
    kokuinNoGrabLogs = 20;
    kokuinDump = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"kokuin_dump", 0, ini) != 0;
    kokuinSnapOk = false;
    kokuinSeenCount = kokuinRefCount = 0;
    kokuinActorCount = 0;
    kokuinPin = 0;
    kokuinPosOk = false;
    kokuinMoveIndex = -1;
    kokuinCasterWas = NULL;
    onryouActor = NULL;
    onryouUpdates = onryouLastFrame = 0;
    onryouFastLogs = 6;
    grabTrace = imOnryouModule && GetPrivateProfileIntW(L"autobattle", L"grab_trace", 0, ini) != 0;
    grabTraceLines = 400;
    grabSeen380 = grabSeen384 = -99;
    altLift = GetPrivateProfileIntW(L"autobattle", L"alt_lift", 1, ini) != 0;
    altState[0].actor = altState[1].actor = NULL;
    altPair[0].actor = altPair[1].actor = NULL;
    altLogs = 6;
    if (module) SetAltAim((BYTE*)module, imOnryouModule && altLift && duelBalance);
    kokuinNero = NULL;
    if (module) SetSharedHook((BYTE*)module, imOnryouModule && duelBalance && duelDifficulty == 2);
    aiAskTraceStart = GetTickCount();
    if (!imOnryouModule) return;
    BYTE* m = imOnryouModule;
    BYTE* at = m + 0x6201a;
    BYTE* target = at[0] == 0xe8 ? at + 5 + *(LONG*)(at + 1) : NULL;
    const char* via = target == (BYTE*)HoRequest ? "the hand-over hook" : target == (BYTE*)ImMixRequest ? "the Tiberius mix" : NULL;
    if (!via && target == m + 0x351b0)
    {
        BYTE call[5] = {0xe8};
        *(LONG*)(call + 1) = (LONG)((BYTE*)ImOnryouRequest - (at + 5));
        via = WriteCode(at, call, 5) ? "its own hook" : NULL;
    }
    if (via) Log("IM_ONRYOU installed: Beelzebub's AI asks ONRYOU for TAIZAI against Lord Byakhee (via %s)", via);
    else Log("IM_ONRYOU off: the AI's ask-by-name call differs");
    BYTE* up = m + 0x393a8;                                    // the per-actor update: the hard mode's HvUpdate or ours (IceFollow)
    BYTE* upTarget = up[0] == 0xe8 ? up + 5 + *(LONG*)(up + 1) : NULL;
    iceFollowLast = NULL;
    if (upTarget == m + 0x333a0)
    {
        BYTE upCall[5] = {0xe8};
        *(LONG*)(upCall + 1) = (LONG)((BYTE*)ImActorUpdate - (up + 5));
        if (!WriteCode(up, upCall, 5)) Log("ICE_FOLLOW off: the actor update call could not be changed");
    }
    else if (upTarget != (BYTE*)HvUpdate) Log("ICE_FOLLOW off: the actor update call differs");
}
