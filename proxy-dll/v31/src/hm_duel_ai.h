// 11-05 user: 「암브로시우스의 AI를 주변을 계속 날아다니며 적의 공격을 회피하며 마력을 채우다가 에테르 라이더를 쓰는 걸로
// 다시 만들어줘」「지금은 비행체에 어울리지 않네」「그러다 체력이 40% 이하면 돌진 공격에 근접 공격으로 이어지는 콤보로」
// 「아 맞다 듀얼모드에서만」.
// In DUEL fights Ambrosius (HM, a flyer) only ever borrowed other robots' AIs: DEMONBLOOD (a ground brawler) in 15 of its 21
// automatic PLAYER1 scenes, LOAD in 6, and whatever its scene names as PLAYER2. Now its own policy, for every Ambrosius actor an
// AI drives in a DUEL fight (either side, every difficulty); the story and FREE FIGHT keep theirs:
//  - HP above 40% of max: circle the opponent in the air (the strafing *_A moves - their LOCKON keeps it facing the opponent) at
//    1100..2000, side-dash (LMOVE_B/RMOVE_B; LQUICK_G/RQUICK_G up close) the moment the opponent starts an attack, charge
//    (CHARGE, +9 MP a cycle) only while the opponent is far and quiet, in bursts between the circling, and at full MP (100) fly
//    the Ether Rider: E_LANCHAR (エーテルランチャー; 「에테르 라이더!」 - transforms and strafes in flight; command list MP >= 30
//    and #72 == 0, costs 40).
//  - HP at or below 40% (the native crisis line, STOP_V at #100 <= 400 of 1000): boost-dash in (FORWARD_B held; interrupt 3 takes
//    the combo request) and chain COMBO1 -> COMBO2 -> COMBO3 -> COMBO4 in their native request frames (55 / 40 / 40).
// Every slot of the fifteen AI tables goes through HdTick - inside the hand-over (a person taking PLAYER1 still takes the robot),
// outside hard_v11 and the hero AIs: an Ambrosius actor gets the native sensing and this policy instead of its borrowed AI,
// everyone else goes on to the previous pointer untouched (11-05: but Lord Byakhee's own AI against this Ambrosius gets a
// stall breaker for its altitude matching - HdLoadTick). (11-05 「다시 쓰게 해 줘」: except the hard-mode PLAYER2 Ambrosius at
// <= 40% HP, which goes back to hard_v11's hand-off and its 塵殺の魔翼 loop - HdHardPinch.) The DEF does every motion (asks
// by name through the AI's own ask, 0x62000 -> 0x307f0 fills the three direct request slots); no position/frame/HP/MP writes.
// Switch: autobattle.ini [autobattle] hm_duel_ai=1 turns this policy on (11-05: off by default - the user chose Lord
// Byakhee's own AI as the base). Data: dmpack532_hm_duel_ai.dat (7 HM
// definitions without the automatic RIFLE2/SHOT/KAMA1/COMBO3 jumps that spent MP or ignored requests).
#pragma once
struct HdSlot { DWORD at; MmTick_t previous; };
static HdSlot hdSlots[15] = {{0xe7818}, {0xe78cc}, {0xe79b8}, {0xe7ac8}, {0xe7b98}, {0xe7c50}, {0xe7d4c}, {0xe7df0},
                             {0xe7ef0}, {0xe7f8c}, {0xe807c}, {0xe80c8}, {0xe8158}, {0xe81c8}, {0xe867c}};
static BYTE* hdModule;
static bool hdEnabled;
enum HdPlan { HD_IDLE, HD_ORBIT, HD_CHARGE, HD_EVADE, HD_ETHER, HD_RUSH, HD_COMBO, HD_SPECIAL };
static const char* const hdPlanNames[] = {"idle", "orbit", "charge", "evade", "ether", "rush", "combo", "special"};
static const int HD_ETHER_MP = 100;                  // full gauge (the gauge stops at 100)
// 11-05 first test (8 fights): free Ambrosius was within 1100 of the opponent 96.5% of the time, so a 1300 charge line never
// came (0 CHARGE) and the circle was all back-diagonals; the hard-mode PLAYER2 copies (MP x10) chained the Ether Rider back to
// back (89% of the fight). Now: circle at 800..1700, charge from 800 in 1.5 s bursts, a back boost-dash inside 600 to make
// room, and at least 5 s of flying between two Ether Riders.
static const float HD_NEAR = 800.0f, HD_FAR = 1700.0f, HD_CHARGE_FROM = 800.0f, HD_LEVEL = 450.0f, HD_ROOM = 600.0f;
// 11-05 user 「에테르 라이더로 계속 날아다니기만 하는게 무적의 전략이네」: 5 s between two Ether Riders (7 s each) put the hard
// PLAYER2 (MP x10) in it 55% of the fight (14:38-15:00, 71 rides) - 15 s for a while (c64503df), then 「그게 에텔 라이더 사용 횟수를
// 줄였더니 질질 늘어지네」 → 「줄였던 에텔 라이더 사용횟수는 복구」: 5 s again; easy / normal mix melee in instead (HdMeleeMix).
static const DWORD HD_ETHER_GAP_MS = 5000, HD_CHARGE_MS = 1500, HD_BETWEEN_MS = 1000, HD_ROOM_MS = 2500;
struct HdFrame {
    BYTE *ai, *actor, *target, *motion;
    const char *name, *enemyName;
    int frame, enemyFrame, hp, maxHp, mp, enemyHp, eventFlag, side, flight;
    float x, z, distance, dy, yaw;
};
struct HdState {
    BYTE* actor; HdPlan plan; const char* held; const char* pending; const char* active; const char* enemyMotion;
    unsigned ticket, evades, ethers, combos, roomDashes, specials; DWORD pendingAt, planAt, lastLog, lastEvade, enemyAttackAt, nextFlip, stuckAt;
    DWORD etherEndAt, lastRoom, lastSpecial, nextMelee;
    int orbitDir; bool started, enemyAttacking, pinchLogged, hardHanded; float stuckX, stuckZ;
};
static HdState hdStates[2];
static unsigned hdRand = 0x2545f491u;
static unsigned HdRandom() { hdRand = hdRand * 1103515245u + 12345u; return (hdRand >> 16) & 0x7fff; }

static bool HdWanted()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    return GetPrivateProfileIntW(L"autobattle", L"hm_duel_ai", 0, ini) != 0;   // 11-05 off by default (user: Lord Byakhee's AI instead)
}
// 11-05 user 「스토리모드에 쓰인 로드비야키 AI부터 다시 시작」→「그렇게 해」: in DUEL Ambrosius runs the LOAD AI everywhere
// (dmpack532_hm_load_ai.dat), so the 10-26 Codex HM pilot (its own policy on the LOAD tick, three balanced pairs) stays off
// unless autobattle.ini hm_pilot=1.
static bool HmPilotWanted()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autobattle.ini", abDir);
    return GetPrivateProfileIntW(L"autobattle", L"hm_pilot", 0, ini) != 0;
}
static const char* HdName(BYTE* m) { return m ? *(const char**)m : ""; }
static bool HdIs(const char* a, const char* b) { return a && b && !_stricmp(a, b); }
static bool HdPrefix(const char* n, const char* p) { return n && !_strnicmp(n, p, strlen(p)); }
// Not an attack: idle, moves, steps, dashes, jumps, landings, guard / charge / empty, damage, wins, losses, waits, events.
// (Attack categories differ from robot to robot - cat 5 is GUARD for one, cat 17 a landing for another - names do not.)
static bool HdPassive(const char* n)
{
    static const char* const p[] = {"STOP", "START", "FORWARD", "BACK", "LMOVE", "RMOVE", "LFORWARD", "RFORWARD", "LBACK",
        "RBACK", "UP", "DOWN", "LTURN", "RTURN", "FQUICK", "BQUICK", "LQUICK", "RQUICK", "JUMP", "FJUMP", "BJUMP", "LJUMP",
        "RJUMP", "FLJUMP", "FRJUMP", "BLJUMP", "BRJUMP", "LANDING", "BLANDING", "LLANDING", "RLANDING", "GUARD", "CHARGE",
        "EMPTY", "DAMAGE", "WAKE_UP", "WIN", "PWIN", "LOSE", "WAIT", "AUTOEVENT", "AUTO_EVENT", "MODE_CHANGE", "RECOVERY",
        "DUEL_", "CHOHATSU", "Haster_Hit", "PLOSE"};
    if (!n || !*n) return true;
    for (unsigned i = 0; i < _countof(p); ++i)
        if (HdPrefix(n, p[i])) return true;
    return false;
}
// Ambrosius's own states this policy may change: idle, the flying moves, CHARGE, and its held rush dash.
static bool HdFree(const char* n)
{
    static const char* const moves[] = {"CHARGE", "EMPTY", "FORWARD_B", "FORWARD_A", "BACK_A", "LMOVE_A", "RMOVE_A",
        "LFORWARD_A", "RFORWARD_A", "LBACK_A", "RBACK_A", "UP_A", "DOWN_A"};
    if (HdPrefix(n, "STOP")) return true;
    for (unsigned i = 0; i < _countof(moves); ++i)
        if (HdIs(n, moves[i])) return true;
    return false;
}
static HdState* HdStateFor(BYTE* actor)
{
    for (unsigned i = 0; i < _countof(hdStates); ++i)
        if (hdStates[i].actor == actor) return &hdStates[i];
    for (unsigned i = 0; i < _countof(hdStates); ++i)
        if (!hdStates[i].actor) { ZeroMemory(&hdStates[i], sizeof hdStates[i]); hdStates[i].actor = actor; return &hdStates[i]; }
    ZeroMemory(&hdStates[0], sizeof hdStates[0]); hdStates[0].actor = actor;
    return &hdStates[0];
}
static bool HdRead(BYTE* ai, HdFrame* f)
{
    __try {
        BYTE* a = *(BYTE**)(ai + 4);
        BYTE* mgr = *(BYTE**)(hdModule + 0x11e3b0);
        BYTE* b = (a && mgr) ? ((MmTarget_t)(hdModule + 0x39d60))(mgr, a) : NULL;
        if (!b || b == a) return false;
        MmWorld_t w = (MmWorld_t)(hdModule + 0x15930);
        AnParamFn get = (AnParamFn)(hdModule + 0x30e90);
        float root[16], troot[16];
        memcpy(root, w(a), sizeof root);
        memcpy(troot, w(b), sizeof troot);
        BYTE* an = *(BYTE**)(a + 0xb84);
        BYTE* bn = *(BYTE**)(b + 0xb84);
        float ly = root[13], tlx = troot[12], tly = troot[13], tlz = troot[14];
        if (an) ly = w(an)[13];
        if (bn) { const float* m = w(bn); tlx = m[12]; tly = m[13]; tlz = m[14]; }
        f->ai = ai; f->actor = a; f->target = b;
        f->motion = *(BYTE**)(a + 0x398); f->name = HdName(f->motion);
        BYTE* em = *(BYTE**)(b + 0x398); f->enemyName = HdName(em);
        f->frame = f->motion ? *(int*)(f->motion + 0x54) : 0;
        f->enemyFrame = em ? *(int*)(em + 0x54) : 0;
        f->hp = *(int*)(a + 0x558); f->maxHp = *(int*)(a + 0x560); f->mp = *(int*)(a + 0x55c);
        f->enemyHp = *(int*)(b + 0x558);
        f->eventFlag = get(a, 1); f->flight = get(a, 72); f->side = *(int*)(a + 0x724);
        f->x = root[12]; f->z = root[14];
        const float dx = troot[12] - root[12], dz = troot[14] - root[14];
        f->distance = sqrtf(dx * dx + dz * dz);
        f->dy = tly - ly;
        const float tx = tlx - root[12], tz = tlz - root[14];
        f->yaw = atan2f(tx * root[0] + tz * root[2], tx * root[8] + tz * root[10]);
        return _finite(f->distance) && _finite(f->dy) && _finite(f->yaw);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static const char* HdDirect(const HdFrame& f) { return HdName(*(BYTE**)(f.actor + 0x378)); }
static void HdAsk(const HdFrame& f, const char* n) { ((AnNameFn)(hdModule + 0x62000))(f.ai, n); }
static void HdWithdraw(const HdFrame& f, const char* n)
{
    // Only our own unstarted proposal; never an executing motion.
    for (int i = 0; i < 3; ++i) {
        BYTE** p = (BYTE**)(f.actor + 0x378 + i * 8);
        if (HdIs(HdName(*p), n)) { *p = NULL; *(DWORD*)(p + 1) = 0; }
    }
}
static void HdSetPlan(HdState& s, HdPlan plan)
{
    if (s.plan != plan) { s.plan = plan; s.planAt = GetTickCount(); }
}
// A held request: a move, CHARGE, the rush dash or STOP. It stays in the direct slots (the motion repeats / loops on it).
static void HdHold(HdState& s, const HdFrame& f, const char* n, HdPlan plan)
{
    if (!HdIs(HdDirect(f), n)) {
        HdAsk(f, n);
        if (!HdIs(s.held, n))
            Log("HD_HOLD t=%lu side=%d plan=%s action=%s motion=%s frame=%d hp=%d mp=%d xz=%.0f dy=%.0f dir=%d failed=%d",
                GetTickCount(), f.side, hdPlanNames[plan], n, f.name, f.frame, f.hp, f.mp, f.distance, f.dy, s.orbitDir,
                (int)*(BYTE*)(f.ai + 0x331));
    }
    HdSetPlan(s, plan);
    s.held = n;
}
// A one-shot: the Ether Rider, a dodge, a combo stage. Followed until it starts.
static void HdOnce(HdState& s, const HdFrame& f, const char* n, HdPlan plan)
{
    HdAsk(f, n);
    s.pending = n; s.active = n; s.started = false; s.pendingAt = GetTickCount(); s.held = NULL;
    ++s.ticket;
    HdSetPlan(s, plan);
    Log("HD_REQUEST t=%lu side=%d ticket=%u plan=%s action=%s motion=%s frame=%d hp=%d/%d mp=%d xz=%.0f dy=%.0f enemy=%s failed=%d",
        GetTickCount(), f.side, s.ticket, hdPlanNames[plan], n, f.name, f.frame, f.hp, f.maxHp, f.mp, f.distance, f.dy,
        f.enemyName, (int)*(BYTE*)(f.ai + 0x331));
}
static void HdRush(HdState& s, const HdFrame& f);
static bool HdMeleeMix() { return duelDifficulty == 0 || duelDifficulty == 1; }   // easy / normal (11-05 user)
static void HdAerial(HdState& s, const HdFrame& f, bool ether = true)
{
    const DWORD now = GetTickCount();
    if (ether && f.mp >= HD_ETHER_MP && f.flight == 0 && (!s.etherEndAt || now - s.etherEndAt >= HD_ETHER_GAP_MS)) {
        ++s.ethers; HdOnce(s, f, "E_LANCHAR", HD_ETHER); return;
    }
    // the opponent has just started an attack: dodge along the circle
    if (s.enemyAttackAt && now - s.enemyAttackAt < 450 && (LONG)(s.enemyAttackAt - s.lastEvade) > 0 && f.distance < 2600.0f) {
        const bool close = f.distance < 750.0f;
        const char* n = s.orbitDir < 0 ? (close ? "LQUICK_G" : "LMOVE_B") : (close ? "RQUICK_G" : "RMOVE_B");
        s.lastEvade = now; ++s.evades;
        HdOnce(s, f, n, HD_EVADE);
        return;
    }
    // 11-05 user 「좀 손해지만 적절하게 근접 공격을 섞게 해야겠어 보통 난이도에선」→「하드 난이도는 그냥 이대로 두고」: on easy and
    // normal (DUEL AI difficulty 0 / 1) a rush + COMBO1-4 between the Ether Riders - 4 s into the fight, then 5..8 s after the
    // last chain or after a rush that did not reach in 3.5 s - only at an opponent that is not attacking, within 1600. Hard
    // keeps the flying-only policy.
    if (HdMeleeMix()) {
        if (!s.nextMelee) s.nextMelee = now + 4000;
        if (s.plan == HD_COMBO || (s.plan == HD_RUSH && now - s.planAt >= 3500)) s.nextMelee = now + 5000 + HdRandom() % 3000;
        else if (!s.enemyAttacking && (!s.enemyAttackAt || now - s.enemyAttackAt > 600) && (LONG)(now - s.nextMelee) >= 0 &&
                 f.distance < 1600.0f) {
            if (s.plan != HD_RUSH)
                Log("HD_MELEE t=%lu side=%d xz=%.0f dy=%.0f mp=%d - rush and melee chain (DUEL AI difficulty %d)", now, f.side, f.distance,
                    f.dy, f.mp, (int)duelDifficulty);
            HdRush(s, f);
            return;
        }
    }
    // too close: a back boost-dash along the circle makes room (then the circle / charge can start)
    if (f.distance < HD_ROOM && now - s.lastRoom > HD_ROOM_MS) {
        s.lastRoom = now; ++s.roomDashes;
        HdOnce(s, f, s.orbitDir < 0 ? "LBACK_B" : "RBACK_B", HD_EVADE);
        return;
    }
    // the circle: flip every 4..7 s, and when it is blocked (the arena wall) for 1.2 s
    if (!s.orbitDir || (LONG)(now - s.nextFlip) >= 0) {
        s.orbitDir = s.orbitDir ? -s.orbitDir : ((HdRandom() & 1) ? 1 : -1);
        s.nextFlip = now + 4000 + HdRandom() % 3000;
    }
    if (s.plan == HD_ORBIT) {
        const float mx = f.x - s.stuckX, mz = f.z - s.stuckZ;
        if (mx * mx + mz * mz > 150.0f * 150.0f) { s.stuckX = f.x; s.stuckZ = f.z; s.stuckAt = now; }
        else if (now - s.stuckAt > 1200) {
            s.orbitDir = -s.orbitDir; s.stuckAt = now; s.nextFlip = now + 4000 + HdRandom() % 3000;
            Log("HD_FLIP t=%lu side=%d dir=%d xz=%.0f (blocked)", now, f.side, s.orbitDir, f.distance);
        }
    } else { s.stuckX = f.x; s.stuckZ = f.z; s.stuckAt = now; }
    // charge only while the opponent is far and quiet, in bursts between the circling
    const bool quiet = !s.enemyAttacking && (!s.enemyAttackAt || now - s.enemyAttackAt > 600);
    const bool distant = f.distance >= HD_CHARGE_FROM;
    if (quiet && distant && f.mp < HD_ETHER_MP) {
        if (s.plan == HD_CHARGE ? now - s.planAt < HD_CHARGE_MS : (s.plan != HD_ORBIT || now - s.planAt >= HD_BETWEEN_MS)) {
            HdHold(s, f, "CHARGE", HD_CHARGE);
            return;
        }
    }
    const char* move = f.distance < HD_NEAR ? (s.orbitDir < 0 ? "LBACK_A" : "RBACK_A")
                     : f.distance > HD_FAR ? (s.orbitDir < 0 ? "LFORWARD_A" : "RFORWARD_A")
                     : (s.orbitDir < 0 ? "LMOVE_A" : "RMOVE_A");
    // 11-05: once climbing / diving, on to 150 inside the line (at 447..453 it swapped UP_A and RMOVE_A every tick)
    const bool leveling = HdIs(s.held, "UP_A") || HdIs(s.held, "DOWN_A");
    if (fabsf(f.dy) > (leveling ? HD_LEVEL - 150.0f : HD_LEVEL)) move = f.dy > 0 ? "UP_A" : "DOWN_A";
    HdHold(s, f, move, HD_ORBIT);
}
// 11-05 user (Ambrosius mirror, below 40%: 「우리는 제자리에서 헛손질만」): the dash asked for COMBO1 on its first frame from
// 640 and the cuts went into the air 500-650 away, chain after chain. COMBO1 starts at its frame 21 on a request and homes in
// only to frame 40 and only from inside 500 (CORRECTDIST(500, 80, 1000, 180): from 205 it closed to 89 in 6 frames, from 468
// to 376 in 5; from 640 it stayed at 610), so the dash goes on to 400 (380 from a stand). COMBO2-4 home in only from inside
// 300 (CORRECTDIST(300, ...)): past that the chain stops and the dash comes back.
static const float HD_COMBO_DASH = 400.0f, HD_COMBO_STAND = 380.0f, HD_CHAIN = 300.0f;
static void HdRush(HdState& s, const HdFrame& f)
{
    const bool dashing = HdIs(f.name, "FORWARD_B");
    const bool close = f.distance <= (dashing ? HD_COMBO_DASH : HD_COMBO_STAND);
    if (close && fabsf(f.dy) <= 300.0f) {
        ++s.combos;
        HdOnce(s, f, "COMBO1", HD_COMBO);
        return;
    }
    if ((close || !dashing) && fabsf(f.dy) > (close ? 300.0f : 350.0f)) { HdHold(s, f, f.dy > 0 ? "UP_A" : "DOWN_A", HD_RUSH); return; }
    HdHold(s, f, "FORWARD_B", HD_RUSH);
}
// 11-05 user (mirror: 「먼저 체력이 떨어진 암브로시우스가 근접 공격만 하려고 해서 다른 암브로시우스가 에테르 런처만 쏴대니 못
// 이기네」): from 14:51:53 PLAYER1 (384) asked COMBO1 34 times from 400 at an opponent flying its Ether Launcher (E_LANCHAR,
// 7 s of passes) or boost-dashing away - none landed, its HP stayed 439 until we fell. Below 40% too, then: the Ether Launcher
// at full MP, and while the opponent attacks (the Ether Launcher, shots, its combos) the side step / circle / charge of the
// policy above 40%; the rush + COMBO1-4 go at an opponent that is not attacking - from 800 and more away after a 1.5 s charge
// burst when MP is short (1 s of rushing between bursts), so the Ether Launcher comes back.
// 11-05 then 「우리편 암브로시우스는 체력이 40% 이하로 떨어져도 컷씬 돌진기를 안쓰는건가」→「쓰게 해 줘」 (only hard_v11's PLAYER2
// fired it; none in the 14:38-15:01 fights): 塵殺の魔翼 (mayoku, category 18, MP 80) - time stops for its cut-ins (DEF:STOP 3.0
// at frame 3), then the grab rush homes in from up to 1000 inside 10 degrees at frames 120-180 (CORRECTDIST(1000, -100, 1000,
// 10), LOCKON keeps facing; CORRECTDIST_Y 150 a second), grab 120-179 -> mayoku2 -> AUTOEVENT. STOP and the moves (interrupts
// 0 / 1 / 3, FORWARD_B too) and CHARGE take it. Below 40% it comes first: MP >= 80 inside 400 and 300 in height, not into the
// opponent's cut-in / win / loss / own 塵殺の魔翼, 3 s after the last ask. MP is kept for it - the Ether Launcher only at full MP
// past 800, the charge bursts up to 80 - and with MP >= 80 the rush closes in for it instead of asking COMBO1 on the way.
static const int HD_MAYOKU_MP = 80;
static const float HD_MAYOKU_RANGE = 400.0f, HD_MAYOKU_HEIGHT = 300.0f;
static bool HdGrabWasted(const char* n)
{
    static const char* const p[] = {"STOP_MUTEKI", "MODE_CHANGE", "LOSE", "PLOSE", "WIN", "PWIN", "AUTOEVENT", "AUTO_EVENT",
        "mayoku", "DUEL_"};
    if (!n || !*n) return true;
    for (unsigned i = 0; i < _countof(p); ++i)
        if (HdPrefix(n, p[i])) return true;
    return false;
}
// 11-05 then 「네로한테 마익을 쓴후 제자리에서 헛손질만 하네」「네로는 기절상태인가」 (and the hard test's Nero fight that never ended):
// mayoku2 sets #290 = 7 (frame 1; 8 at 170, 0 at 171) and every Nero (qm) definition jumps on #290 == 7 to its story event
// AUTOEVENT1 / AUTOEVENT2 (category 41, `jump VOID 900 900` - holds its last frame for good): Nero stood there until the test
// tool's 4-minute limit while we cut the air. The other robots that read #290 == 7 go to a damage motion or STOP_G. First
// (daf6b96e) never against Nero; then the user 「정의 고쳐 줘」: dmpack533_nero_event.dat (hmdueluild_nero_event_pack.py)
// comments those #290 == 7 / 8 jumps out of the 99 Nero definitions DUEL loads, so 塵殺の魔翼 is used against Nero again.
static void HdPinch(HdState& s, const HdFrame& f)
{
    const DWORD now = GetTickCount();
    const bool ready = f.mp >= HD_MAYOKU_MP && !HdGrabWasted(f.enemyName) && (!s.lastSpecial || now - s.lastSpecial >= 3000);
    if (ready && f.distance <= HD_MAYOKU_RANGE && fabsf(f.dy) <= HD_MAYOKU_HEIGHT) {
        s.lastSpecial = now; ++s.specials;
        HdOnce(s, f, "mayoku", HD_SPECIAL);
        return;
    }
    const bool away = f.distance > HD_CHARGE_FROM;      // (not `far`: a windows.h macro)
    if (away && f.mp >= HD_ETHER_MP && f.flight == 0 && (!s.etherEndAt || now - s.etherEndAt >= HD_ETHER_GAP_MS)) {
        ++s.ethers; HdOnce(s, f, "E_LANCHAR", HD_ETHER); return;
    }
    if (s.enemyAttacking) { HdAerial(s, f, away); return; }
    const bool quiet = !s.enemyAttackAt || now - s.enemyAttackAt > 600;
    if (quiet && f.distance >= HD_CHARGE_FROM && f.mp < HD_MAYOKU_MP &&
        (s.plan == HD_CHARGE ? now - s.planAt < HD_CHARGE_MS : now - s.planAt >= HD_BETWEEN_MS)) {
        HdHold(s, f, "CHARGE", HD_CHARGE);
        return;
    }
    if (ready) {                                       // close in for it: the rush dash, levelling first only for a big gap
        if (fabsf(f.dy) > HD_MAYOKU_HEIGHT) { HdHold(s, f, f.dy > 0 ? "UP_A" : "DOWN_A", HD_RUSH); return; }
        HdHold(s, f, "FORWARD_B", HD_RUSH);
        return;
    }
    HdRush(s, f);
}
static void HdDecide(HdState& s, const HdFrame& f)
{
    const DWORD now = GetTickCount();
    const bool attacking = !HdPassive(f.enemyName);
    if (attacking && (!s.enemyAttacking || s.enemyMotion != f.enemyName)) s.enemyAttackAt = now;
    s.enemyAttacking = attacking; s.enemyMotion = f.enemyName;
    // the opponent is KO: our request outlives its motion (interrupt 0 restarts it from STOP) - park STOP so PWIN can run
    if (f.enemyHp <= 0 && f.hp > 0 && f.eventFlag == 0) {
        if (!HdIs(HdDirect(f), "STOP")) {
            HdAsk(f, "STOP");
            Log("HD_POSTKO t=%lu side=%d motion=%s frame=%d", now, f.side, f.name, f.frame);
        }
        s.pending = s.active = NULL; s.held = "STOP";
        return;
    }
    if (f.hp <= 0 || f.eventFlag != 0) return;
    if (s.pending) {
        if (HdIs(f.name, s.pending)) {
            s.started = true;
            Log("HD_STARTED t=%lu side=%d ticket=%u action=%s frame=%d mp=%d xz=%.0f", now, f.side, s.ticket, f.name, f.frame, f.mp, f.distance);
            s.pending = NULL;
            HdAsk(f, "STOP");                      // parked behind it: no restart of the same one-shot from STOP
            s.held = "STOP";
        } else if (now - s.pendingAt < 1200) return;
        else {
            Log("HD_NOT_STARTED t=%lu side=%d ticket=%u action=%s actual=%s frame=%d", now, f.side, s.ticket, s.pending, f.name, f.frame);
            HdWithdraw(f, s.pending);
            s.pending = s.active = NULL; s.started = false;
        }
    }
    // the melee chain in its native request frames (COMBO1 takes MREQ 6 at 55, COMBO2 MREQ 7 at 40, COMBO3 MREQ 8 at 40)
    if (s.plan == HD_COMBO && s.started && s.active && HdIs(f.name, s.active) && f.distance <= HD_CHAIN) {
        const char* next = NULL;
        if (HdIs(f.name, "COMBO1") && f.frame >= 38 && f.frame < 55) next = "COMBO2";
        else if (HdIs(f.name, "COMBO2") && f.frame >= 22 && f.frame < 40) next = "COMBO3";
        else if (HdIs(f.name, "COMBO3") && f.frame >= 22 && f.frame < 40) next = "COMBO4";
        if (next) { HdOnce(s, f, next, HD_COMBO); return; }
    }
    if (!HdFree(f.name)) return;                   // attacks, steps, side dashes, damage, landings: native
    if (s.active && s.started) {
        if (HdIs(s.active, "E_LANCHAR")) s.etherEndAt = now;
        s.active = NULL; s.started = false;
    }
    const bool pinch = f.maxHp > 0 && f.hp * 100 <= f.maxHp * 40;
    if (pinch && !s.pinchLogged) {
        s.pinchLogged = true;
        Log("HD_PINCH t=%lu side=%d hp=%d/%d mp=%d - 塵殺の魔翼 at MP 80 inside 400, Ether Launcher only at full MP past 800, dodge / circle while it attacks, rush and melee chain at it otherwise (ethers=%u evades=%u room=%u)", now, f.side, f.hp, f.maxHp, f.mp, s.ethers, s.evades, s.roomDashes);
    }
    if (pinch) HdPinch(s, f);
    else HdAerial(s, f);
}
// 11-05 user 「다시 쓰게 해 줘」 (asked whether an enemy Ambrosius uses 塵殺の魔翼 much in hard mode - it did before this
// policy: hard_v11's PLAYER2 at <= 40% HP drops the old AI and only charges to 80, closes to 200 and fires mayoku; 03:23
// Lord Byakhee vs Ambrosius (scene 391 -> 811), 3 in 35 s - and this policy, outside hard_v11, had taken that away; 06:02
// test 72BA1182, mirror scene 456: hand-off at 39.5%, mayoku 3 s later, its grab took PLAYER1 from 384 to 0). So the hard-mode PLAYER2
// Ambrosius (the same scope test as HvAiTick: hard, balance, its private data/hv11/r* definition) at <= 40% of its HP goes
// back to the previous pointer - hard_v11 hands off there and runs its special loop. Above 40%, PLAYER1 and other
// difficulties keep this policy (PLAYER1 keeps the rush + COMBO1-4 the user asked for).
static bool HdHardPinch(BYTE* a)
{
    __try {
        if (!a || !hvEnabled || !duelBalance || duelDifficulty != 2 || balScene < 0 || *(int*)(a + 0x724) != 1) return false;
        const int hp = *(int*)(a + 0x558), maximum = *(int*)(a + 0x560);
        if (hp <= 0 || maximum <= 0 || hp * 100 > maximum * 40) return false;
        const char* def = HvDefinition(a);
        if (!def) return false;
        char path[512];
        if (strcpy_s(path, def)) return false;
        _strlwr_s(path);
        for (char* p = path; *p; ++p) if (*p == '\\') *p = '/';
        return strstr(path, "data/hv11/r") != NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// 11-05 user (Lord Byakhee against this flying Ambrosius: 「로드 비야키가 어느 시점부터 공격을 안하고 날아다니기만 하네」 -
// 13:48:28-53 it held DOWN_A for 25 s; the 06:07 test fight: DOWN_A 16% of the time; then 「로드 비야키까지 바꾼거 아냐?」 -
// no: its files and AI are as on 10-19, and this tick passed it straight through). Lord Byakhee's own AI (CAiLoad,
// khd-runtime-fix/special_mode/ai_reports/LOAD.md B.4) matches altitude first: while its height and ours differ by 5 or more
// (+0x6f4) it asks UP_A / DOWN_A and decides nothing else, and when that cannot close (its DOWN_A loop, frames 30-60, does
// not move it) it stops fighting. Codex met the same stall against QM (mm_qm_anchor_fix.h C12 feeds that branch a lockOn-
// height predicate through the same field). Here a stall breaker only: when its UP_A / DOWN_A has not moved it 3 up or down
// in 0.5 s, its own tick reads a 1e6 tolerance (put back after the tick) until Ambrosius moves 150 up or down or 6 s pass.
// Its phases, moves, MP and dispatch stay native; Lord Byakhee's files are untouched.
// 11-05 then (on-screen test, user 「여전히 로드비야키가 암브로시우스를 쫓지 못하고 방어만 하네」): its sense (0x62580, read
// 0x62707) arms the guard while we are in E_LANCHAR / gun moves anywhere 200..+0x6ec away - 1500 from the ctor (600 after its
// phase set-up, 0x60914 / 0x61091) - and this Ambrosius flies the Ether Rider (E_LANCHAR) about 7 s in every 12: guards of 9 s
// and 7 s in a row. Against this Ambrosius the far line reads 450 during its own tick (put back after, unless its own code set
// a new one meanwhile): it guards when the Ether Rider comes close and otherwise shoots or chases as its step table says.
static const float HD_LOAD_GUARD_FAR = 450.0f;
struct HdLoadStall { BYTE* actor; float y, stuckTargetY; DWORD movedAt, until; unsigned count; bool guardLogged; };
static HdLoadStall hdLoadStalls[2];
static void HdLoadTick(BYTE* ai, BYTE* secondary, DWORD dt, DWORD arg, MmTick_t previous)
{
    float* tolerance = NULL;
    float* guardFar = NULL;
    float guardWas = 0.0f;
    __try {
        BYTE* a = *(BYTE**)(ai + 4);
        BYTE* mgr = *(BYTE**)(hdModule + 0x11e3b0);
        if (hdEnabled && a && mgr && GetModuleHandleW(battleModule) == (HMODULE)hdModule &&
            *(void**)ai == hdModule + 0xe80d4 && *(int*)(a + 0x558) > 0 && ImDefIn(a, "/character/mm/")) {
            BYTE* b = ((MmTarget_t)(hdModule + 0x39d60))(mgr, a);
            if (b && b != a && *(int*)(b + 0x558) > 0 && ImDefIn(b, "/character/hm/")) {
                HdLoadStall* st = NULL;
                for (unsigned i = 0; i < _countof(hdLoadStalls) && !st; ++i) if (hdLoadStalls[i].actor == a) st = &hdLoadStalls[i];
                for (unsigned i = 0; i < _countof(hdLoadStalls) && !st; ++i) if (!hdLoadStalls[i].actor) st = &hdLoadStalls[i];
                if (!st) st = &hdLoadStalls[0];
                MmWorld_t w = (MmWorld_t)(hdModule + 0x15930);
                const float y = w(a)[13], ty = w(b)[13];
                BYTE* m = *(BYTE**)(a + 0x398);
                const char* n = HdName(m);
                const DWORD now = GetTickCount();
                if (st->actor != a) { ZeroMemory(st, sizeof *st); st->actor = a; st->y = y; st->movedAt = now; }
                const bool height = HdIs(n, "UP_A") || HdIs(n, "DOWN_A");
                if (!height || fabsf(y - st->y) > 3.0f) { st->y = y; st->movedAt = now; }
                if (st->until) {
                    if ((LONG)(now - st->until) >= 0 || fabsf(ty - st->stuckTargetY) > 150.0f) {
                        Log("HD_LOAD_STALL_END t=%lu side=%d motion=%s root=%.0f target_root=%.0f (Ambrosius moved %.0f)",
                            now, *(int*)(a + 0x724), n, y, ty, ty - st->stuckTargetY);
                        st->until = 0; st->y = y; st->movedAt = now;
                    }
                } else if (height && now - st->movedAt >= 500 && *(float*)(ai + 0x6f4) == 5.0f) {
                    st->until = now + 6000; st->stuckTargetY = ty; ++st->count;
                    const BYTE* an = *(BYTE**)(a + 0xb84); const BYTE* bn = *(BYTE**)(b + 0xb84);
                    Log("HD_LOAD_STALL t=%lu side=%d n=%u motion=%s frame=%d root=%.0f target_root=%.0f lock=%.0f target_lock=%.0f hp=%d mp=%d phase=%d - its height test reads level for up to 6 s",
                        now, *(int*)(a + 0x724), st->count, n, m ? *(int*)(m + 0x54) : -1, y, ty, an ? w((BYTE*)an)[13] : y,
                        bn ? w((BYTE*)bn)[13] : ty, *(int*)(a + 0x558), *(int*)(a + 0x55c), *(int*)(ai + 0xc));
                }
                if (st->until && *(float*)(ai + 0x6f4) == 5.0f) tolerance = (float*)(ai + 0x6f4);
                float* g = (float*)(ai + 0x6ec);
                if (_finite(*g) && *g > HD_LOAD_GUARD_FAR) {
                    guardFar = g; guardWas = *g;
                    if (!st->guardLogged) {
                        st->guardLogged = true;
                        Log("HD_LOAD_GUARD t=%lu side=%d far %.0f -> %.0f: Lord Byakhee guards Ambrosius's Ether Rider / shots only inside %.0f",
                            now, *(int*)(a + 0x724), *g, HD_LOAD_GUARD_FAR, HD_LOAD_GUARD_FAR);
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { tolerance = NULL; guardFar = NULL; }
    if (!tolerance && !guardFar) { previous(secondary, dt, arg); return; }
    if (tolerance) *tolerance = 1000000.0f;
    if (guardFar) *guardFar = HD_LOAD_GUARD_FAR;
    __try { previous(secondary, dt, arg); }
    __finally {
        if (tolerance) *tolerance = 5.0f;
        if (guardFar && *guardFar == HD_LOAD_GUARD_FAR) *guardFar = guardWas;   // its phase set-up may write its own line
    }
}
// 11-05 test switch, autobattle.ini [autobattle] kokuin_test=1 (user 「실험용으로 시작하자마자 각인 666을 쓰는 버전을 만들고 데우스
// 마키나들을 바꿔가며 써보는게 어떨까?」): the Nameless One asks KOKUIN666 as soon as its START is over, once a fight - while it
// waits for it to start, its own tick runs first and KOKUIN666 is written over its request. After 4 s without a start, it stops asking.
// 17:41 works (F02 HM, F03 MM: KOKUIN666 9-11 s in, the target's DUEL_666 and the camera). Not in ground battles (scene 73xx):
// there the Nameless One is the ground-only model (NG_LIVE_*.def, user 「지상전일 경우 지상전 전용 모델을 쓰게 했는데」) - KENTEI2 only.
static int hdKokuinTest;
static BYTE* hdKokuinDone;
static DWORD hdKokuinAskFrom;
static bool HdKokuinTest(BYTE* ai, BYTE* a)
{
    (void)ai;                                     // the actor's own request now (see below)
    if (hdKokuinTest <= 0 || !a || a == hdKokuinDone || !ImDefIn(a, "/character/qm/")) return false;
    BYTE* m = *(BYTE**)(a + 0x398);
    const char* n = m ? *(const char**)m : "";
    if (!_stricmp(n, "START") || *(int*)(a + 0x558) <= 0) return false;
    const DWORD now = GetTickCount();
    if (!_stricmp(n, "KOKUIN666")) {
        hdKokuinDone = a;
        Log("KOKUIN_TEST t=%lu side=%d KOKUIN666 started (%lu ms after the first ask)", now, *(int*)(a + 0x724), now - hdKokuinAskFrom);
        return false;
    }
    if (!hdKokuinAskFrom) { hdKokuinAskFrom = now; Log("KOKUIN_TEST t=%lu side=%d asks KOKUIN666 (motion %s)", now, *(int*)(a + 0x724), n); }
    if (now - hdKokuinAskFrom > 4000) {
        hdKokuinDone = a;
        Log("KOKUIN_TEST t=%lu side=%d KOKUIN666 did not start in 4 s (motion %s) - its own AI again", now, *(int*)(a + 0x724), n);
        return false;
    }
    // 17:26 / 17:28: the AI's ask (0x62000) was not taken - KENTEI2 came instead. hard_v11's way: the actor's own request by
    // motion name into its three direct slots (0x307f0); its STOP / move interrupts take MREQ(8), KOKUIN666 is category 8.
    for (int q = 0; q < 5; ++q) { *(BYTE**)(a + 0x2b8 + q * 8) = NULL; *(DWORD*)(a + 0x2bc + q * 8) = 0; }
    bool taken = false;
    for (int slot = 0; slot < 3; ++slot) taken = ((HvRequestFn)(hdModule + 0x307f0))(a, "KOKUIN666", slot) || taken;
    if (!taken && now - hdKokuinAskFrom < 50) Log("KOKUIN_TEST t=%lu the direct request was refused (motion %s)", now, n);
    return true;
}
static void __fastcall HdTick(BYTE* secondary, void*, DWORD dt, DWORD arg)
{
    MmTick_t previous = NULL;
    for (unsigned i = 0; i < _countof(hdSlots); ++i)
        if (*(void**)secondary == hdModule + hdSlots[i].at) { previous = hdSlots[i].previous; break; }
    if (!previous) return;
    BYTE* ai = secondary - 0x714;
    HdState* s = NULL;
    BYTE* actor = NULL;
    if (hdEnabled && GetModuleHandleW(battleModule) == (HMODULE)hdModule) {
        __try {
            BYTE* a = *(BYTE**)(ai + 4);
            if (a && ImDefIn(a, "/character/hm/")) { s = HdStateFor(a); actor = a; }
        } __except (EXCEPTION_EXECUTE_HANDLER) { s = NULL; }
    }
    if (!s) {
        // 17:35 (user 「소드 스펠만 써대는데」): asked before the Nameless One's own tick, its KENTEI2 (sword spell) request took the
        // same direct slots. Now its own tick first, then KOKUIN666 written over the slots (the motion update reads them after).
        HdLoadTick(ai, secondary, dt, arg, previous);
        __try { if (hdEnabled) HdKokuinTest(ai, *(BYTE**)(ai + 4)); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return;
    }
    if (HdHardPinch(actor)) {                         // before our sensing call: hard_v11 makes its own
        if (!s->hardHanded) {
            s->hardHanded = true;
            HdFrame g = {};
            g.actor = actor;
            if (s->held) HdWithdraw(g, s->held);       // our held move / rush dash and an unstarted one-shot leave the slots
            if (s->pending) HdWithdraw(g, s->pending);
            s->held = s->pending = s->active = NULL;
            Log("HD_HARD_HANDOFF t=%lu side=1 hp=%d/%d mp=%d: hard PLAYER2 at <= 40%% - hard_v11 from here (charge to 80, close to 200, 塵殺の魔翼)",
                GetTickCount(), *(int*)(actor + 0x558), *(int*)(actor + 0x560), *(int*)(actor + 0x55c));
        }
        previous(secondary, dt, arg);
        return;
    }
    typedef void (__thiscall* HdSenseFn)(BYTE*, DWORD);
    ((HdSenseFn)(hdModule + 0x62580))(ai, dt);    // the native sensing and timers only
    *(int*)(ai + 0x2c) = 0;                          // no movement output from the borrowed AI
    HdFrame f = {};
    if (!HdRead(ai, &f)) return;
    if (HdIs(f.name, "START")) { ((AnPhaseFn)(hdModule + 0x64b20))(ai); return; }
    HdDecide(*s, f);
    const DWORD now = GetTickCount();
    if (now - s->lastLog >= 250) {
        s->lastLog = now;
        Log("HD_STATE t=%lu side=%d plan=%s held=%s pending=%s motion=%s frame=%d hp=%d/%d mp=%d flight=%d xz=%.0f dy=%.0f yaw=%.0f enemy=%s@%d attacking=%d dir=%d",
            now, f.side, hdPlanNames[s->plan], s->held ? s->held : "-", s->pending ? s->pending : "-", f.name, f.frame, f.hp, f.maxHp,
            f.mp, f.flight, f.distance, f.dy, f.yaw * 57.2957795f, f.enemyName, f.enemyFrame, (int)s->enemyAttacking, s->orbitDir);
    }
}
// Taken out right after the hand-over at the next fight start (before hard_v11 and the hero AIs check their slots).
static void RestoreHmDuelSlots(HMODULE module)
{
    if (!hdModule || hdModule != (BYTE*)module) return;
    hdEnabled = false;
    for (unsigned i = 0; i < _countof(hdSlots); ++i) {
        void** slot = (void**)(hdModule + hdSlots[i].at);
        if (*slot == (void*)HdTick && hdSlots[i].previous) MmSwapSlot(slot, (void*)HdTick, (void*)hdSlots[i].previous);
    }
}
// After hard_v11, before the hand-over: DUEL fights only.
static void InstallHmDuelAi(HMODULE module, bool duel)
{
    hdEnabled = false;
    ZeroMemory(hdStates, sizeof hdStates);
    ZeroMemory(hdLoadStalls, sizeof hdLoadStalls);
    hdKokuinDone = NULL; hdKokuinAskFrom = 0;
    {
        wchar_t ini[MAX_PATH];
        swprintf_s(ini, L"%s\\autobattle.ini", abDir);
        hdKokuinTest = GetPrivateProfileIntW(L"autobattle", L"kokuin_test", 0, ini);
        if (hdKokuinTest > 0) Log("KOKUIN_TEST on: the Nameless One asks KOKUIN666 right after START (autobattle.ini kokuin_test=1)");
    }
    if (!duel || !module || !HdWanted()) return;
    BYTE* m = (BYTE*)module;
    hdModule = m;
    hdRand ^= GetTickCount();
    for (unsigned i = 0; i < _countof(hdSlots); ++i) {
        void** slot = (void**)(m + hdSlots[i].at);
        void* old = *slot;
        if (old == (void*)HdTick) continue;
        hdSlots[i].previous = (MmTick_t)old;
        if (!MmSwapSlot(slot, old, (void*)HdTick)) {
            Log("HM_DUEL_AI slot %lx not written - off for this fight", hdSlots[i].at);
            RestoreHmDuelSlots(module);
            return;
        }
    }
    hdEnabled = true;
    Log("HM_DUEL_AI enabled: Ambrosius on its own AI (circle %.0f..%.0f, room dash < %.0f, dodge, charge from %.0f, Ether Launcher at MP %d every >= %lu ms; easy / normal mix a rush + melee chain in every 5..8 s; at 40%% HP 塵殺の魔翼 at MP 80 inside 400, the Ether Launcher past 800, dodges, rush + COMBO1-4 at an opponent not attacking)",
        HD_NEAR, HD_FAR, HD_ROOM, HD_CHARGE_FROM, HD_ETHER_MP, HD_ETHER_GAP_MS);
}
