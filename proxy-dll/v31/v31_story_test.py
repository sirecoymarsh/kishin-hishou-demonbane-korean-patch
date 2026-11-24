"""v31 스토리 어려움 시험(11-19) — 시험 사본 F:\\AI\\khd-fullrun-game, 숨김(화면 밖·소리 끔, 게임 창·대화 상자 모두 화면 밖), 정상 종료.
사용자 「가드·충전 넣어」: 스토리 어려움에서 티베리우스 폭식 대죄·오거 비검·비룡·데몬 블러드 금기의 피 해방 동안 가드(피해 절반),
티베리우스 원령 주탄 모으기 절반.

  python -I -X utf8 v31_story_test.py <프리 파이트 번호> [제한 초] [--normal]
   10 = 데몬베인 vs 벨제부트(IM), 11 = 아이온 vs 오거(NM), 4 = D 투소드 vs D 블러드(GM)
프리 파이트 N번을 우리 편 자동 전투(free_auto=1)로 싸운다. 난이도는 system.dll 의 난이도 변수(RVA 0x72CB2 명령의 주소)를 게임이 켜 있는
동안 2(어려움)로 둔다(--normal 이면 1, 대조용). 이 판 동안만 autobattle.ini 에 free_auto=1·unlock_all=1(막대 잠금 방지) — 시험 도구가
끝에 ini 를 바이트 그대로 되돌린다. 세이브 폴더와 레지스트리 ActDifficult·창 자리도 되돌린다.
판정: 시험 도구의 F01_battle.log(두 쪽 동작@프레임·체력을 0.3~0.5초마다)와 대리 DLL 기록.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import json
import re
import shutil
import sys
import threading
import time
from pathlib import Path

ORIG_ARGV = sys.argv[:]
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
BATTLE = int(ARGS[0])
LIMIT = ARGS[1] if len(ARGS) > 1 else "200"
WANT = 1 if "--normal" in sys.argv else 2
G = Path(r"F:\AI\khd-fullrun-game")
HERE = Path(__file__).resolve().parent
RUNS = HERE / "runs"
RUNS.mkdir(exist_ok=True)
sys.argv = [sys.argv[0], f"--free={BATTLE}", "--passive", f"--limit={LIMIT}"]
sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
import run_duel_test as D  # noqa: E402

S, T, realreg = D.S, D.T, D.realreg
S.GAME = G
S.INI = G / "autobattle" / "autobattle.ini"
T.CAPDIR = G / "autobattle" / "cap"
T.PROXY_LOG = G / "autobattle" / "proxy_log.txt"
EXE = str(G / "KishinHisho_chs.exe").lower()

_iw = T.ini_with


def ini_with(raw, enc, extra):
    extra = dict(extra)
    extra.update(free_auto=1, unlock_all=1)
    return _iw(raw, enc, extra)


T.ini_with = ini_with

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = W.HANDLE
psapi = ctypes.WinDLL("psapi")
u32 = ctypes.WinDLL("user32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
stop = threading.Event()
notes: list[str] = []
CUR = {"pid": 0}      # keep_difficulty 가 0.5초마다 채움(keep_offscreen 은 이것만 씀 — 20ms 마다 프로세스를 훑지 않게)


def exe_of(pid):
    h = k32.OpenProcess(0x1000, False, pid)
    if not h:
        return ""
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = W.DWORD(1024)
        return buf.value.lower() if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) else ""
    finally:
        k32.CloseHandle(h)


def our_pid():
    import psutil
    for p in psutil.process_iter(["name"]):
        if (p.info["name"] or "").lower() == "kishinhisho_chs.exe" and exe_of(p.pid) == EXE:
            return p.pid
    return 0


def module_base(h, name):
    mods = (W.HMODULE * 1024)()
    need = W.DWORD()
    if not psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3):
        return None
    for m in mods[: need.value // ctypes.sizeof(W.HMODULE)]:
        nm = ctypes.create_unicode_buffer(260)
        psapi.GetModuleBaseNameW(h, W.HMODULE(m), nm, 260)
        if nm.value.lower() == name:
            return m
    return None


def rd(h, addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    return buf.raw[: got.value] if k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) else None


def keep_difficulty():
    """게임이 켜 있는 동안 system.dll 난이도 변수를 WANT 로(세이브 설정을 읽은 뒤에도 덮이게 0.5초마다)"""
    said = False
    while not stop.is_set():
        pid = our_pid()
        CUR["pid"] = pid
        if pid:
            h = k32.OpenProcess(0x0438, False, pid)      # VM_OPERATION | VM_READ | VM_WRITE | QUERY_INFORMATION
            try:
                base = module_base(h, "system.dll") if h else None
                ins = rd(h, base + 0x72CB2, 10) if base else None
                if ins and ins[0] == 0xC7 and ins[1] == 0x05 and ins[6] == 2:
                    var = int.from_bytes(ins[2:6], "little")
                    now = rd(h, var, 4)
                    if now is not None and int.from_bytes(now, "little") != WANT:
                        val = ctypes.c_int(WANT)
                        k32.WriteProcessMemory(h, ctypes.c_void_p(var), ctypes.byref(val), 4, None)
                        if not said:
                            notes.append(f"difficulty variable {var:#x}: {int.from_bytes(now, 'little')} -> {WANT}")
                            said = True
            finally:
                if h:
                    k32.CloseHandle(h)
        time.sleep(0.5)


def keep_offscreen():
    """11-16 「안보이게」: 시험 게임의 보이는 창(대화 상자 포함)을 화면 밖·맨 뒤로"""
    moved = set()
    while not stop.is_set():
        pid = CUR["pid"]
        if pid:
            def cb(hw, _l):
                p = W.DWORD()
                u32.GetWindowThreadProcessId(hw, ctypes.byref(p))
                if p.value == pid and u32.IsWindowVisible(hw):
                    rc = W.RECT()
                    u32.GetWindowRect(hw, ctypes.byref(rc))
                    if -30000 < rc.left < 15000:
                        u32.SetWindowPos(hw, 1, 20900, 0, 0, 0, 0x0001 | 0x0010)
                        b = ctypes.create_unicode_buffer(64)
                        u32.GetClassNameW(hw, b, 64)
                        if (hw, b.value) not in moved:
                            moved.add((hw, b.value))
                            notes.append(f"moved off screen: {b.value} from ({rc.left},{rc.top})")
                return True
            u32.EnumWindows(WNDENUMPROC(cb), 0)
        time.sleep(0.02)


FIXTURE = "--fixture" in ORIG_ARGV          # (sys.argv 는 위에서 시험 도구용으로 바뀜)
# 시험 전용(--fixture): 적 체력을 다음 단계 문턱 바로 위로(우리 편의 다음 타격이 문턱을 넘겨 컷인 → 그 단계 필살기가 자연스럽게 나오게),
# 우리 편 체력은 300 아래면 600 으로(판이 일찍 끝나지 않게). 배포 데이터·대리 DLL 은 그대로 — 이 시험기만 체력 칸(#100, +0x558)을 쓴다.
FRACS = {10: [0.75, 0.50, 0.25], 11: [0.80, 0.60, 0.40, 0.20], 4: [0.60, 0.30], 3: [0.60, 0.30]}


def u32(h, a):
    b = rd(h, a, 4)
    return int.from_bytes(b, "little") if b and len(b) == 4 else 0


def cstr(h, a, n=40):
    b = rd(h, a, n) if a else None
    return b.split(b"\0", 1)[0].decode("ascii", "replace") if b else ""


def fighters(h, base):
    mgr = u32(h, base + 0x11E3B0)
    scene = u32(h, mgr + 4) if mgr else 0
    out = []
    if scene:
        a, b = u32(h, scene + 0x28), u32(h, scene + 0x2C)
        if a and b and 0 < b - a < 0x400:
            for m in range(a, b, 4):
                mat = u32(h, m)
                c = u32(h, mat + 0x64) if mat else 0
                if not c or (u32(h, c + 0x720) & 3) == 0:
                    continue
                mo = u32(h, c + 0x398)
                hp = int.from_bytes((rd(h, c + 0x558, 4) or b"\0\0\0\0"), "little", signed=True)
                out.append(dict(c=c, grp=u32(h, c + 0x724), motion=cstr(h, u32(h, mo)) if mo else "", hp=hp))
    return out


def fixture():
    fr = FRACS.get(BATTLE, [])
    maxhp, seen_mc, on_at, tops = None, set(), None, 0
    while not stop.is_set():
        pid = CUR["pid"]
        if pid:
            h = k32.OpenProcess(0x0438, False, pid)
            try:
                base = module_base(h, "demonbane.dll") if h else None
                fs = fighters(h, base) if base else []
                en = next((f for f in fs if f["grp"] == 1), None)
                me = next((f for f in fs if f["grp"] == 0), None)
                if en and me:
                    on_at = on_at or time.time()
                    if maxhp is None and en["hp"] > 0:
                        maxhp = en["hp"]
                        notes.append(f"fixture: enemy max HP {maxhp}, cut-in thresholds {[int(maxhp * x) for x in fr]}")
                    m = en["motion"]
                    if m.startswith("MODE_CHANGE") and m not in seen_mc:
                        seen_mc.add(m)
                        notes.append(f"fixture: saw {m} at enemy HP {en['hp']}")
                    stage = len(seen_mc)
                    ok_state = not m.startswith("MODE_CHANGE") and not m.startswith("DAMAGE") and m not in ("START", "")
                    if maxhp and stage < len(fr) and time.time() - on_at > 8 and ok_state:
                        target = int(maxhp * fr[stage]) + 12
                        if en["hp"] > target + 3:
                            v = ctypes.c_int(target)
                            k32.WriteProcessMemory(h, ctypes.c_void_p(en["c"] + 0x558), ctypes.byref(v), 4, None)
                            notes.append(f"fixture: enemy HP {en['hp']} -> {target} (stage {stage}, during {m})")
                    if 0 < me["hp"] < 300 and tops < 12:
                        v = ctypes.c_int(600)
                        k32.WriteProcessMemory(h, ctypes.c_void_p(me["c"] + 0x558), ctypes.byref(v), 4, None)
                        tops += 1
                        notes.append(f"fixture: our HP {me['hp']} -> 600")
            finally:
                if h:
                    k32.CloseHandle(h)
        time.sleep(0.25)


def analyse(out: Path) -> dict:
    """F01_battle.log: 「 12.0s scene | g0 X.def MOTION@f hp1000 mp100 … || g1 Y.def MOTION@f hp1200 mp60 … | d=…」"""
    res = {"out": str(out)}
    bl = out / "F01_battle.log"
    rows = []
    if bl.exists():
        for ln in bl.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.match(r"\s*([\d.]+)s .*?\| g0 (\S+) (\S+)@(-?\d+) hp(-?\d+) mp(-?\d+).*?\|\| g1 (\S+) (\S+)@(-?\d+) hp(-?\d+) mp(-?\d+)", ln)
            if m:
                rows.append(dict(t=float(m[1]), d0=m[2], m0=m[3], f0=int(m[4]), hp0=int(m[5]), d1=m[7], m1=m[8], f1=int(m[9]),
                                 hp1=int(m[10]), mp1=int(m[11])))
    res["samples"] = len(rows)
    if not rows:
        return res
    res["enemy_def"] = rows[-1]["d1"]
    res["hp_end"] = (rows[-1]["hp0"], rows[-1]["hp1"])
    seq = []
    for r in rows:
        if not seq or seq[-1][0] != r["m1"]:
            seq.append([r["m1"], r["t"], r["t"], r["f1"], r["f1"], r["hp1"], r["hp1"]])
        else:
            seq[-1][2], seq[-1][4], seq[-1][6] = r["t"], r["f1"], r["hp1"]
    res["enemy_motions"] = sorted({s[0] for s in seq})
    big = ("TAIZAI", "HIRYUU", "KINKI1", "ONRYOU", "ASHURA_1", "S_VANPIRE", "MODE_CHANGE1", "MODE_CHANGE2", "MODE_CHANGE3", "MODE_CHANGE4")
    segs = []
    for i, s in enumerate(seq):
        if s[0] in big:
            nxt = seq[i + 1][0] if i + 1 < len(seq) else "-"
            segs.append(dict(motion=s[0], t0=s[1], t1=s[2], f0=s[3], f1=s[4], hp_lost=s[5] - s[6], next=nxt))
    res["big_moves"] = segs
    # 우리 편이 적에게 준 피해: 적 체력이 줄어든 표본마다, 그때 적 동작이 가드 대상이면 「가드 중」, 아니면 「밖」
    guard = {"TAIZAI", "HIRYUU", "KINKI1"}
    hits = {"in_guarded_move": [], "outside": []}
    for a, b in zip(rows, rows[1:]):
        lost = a["hp1"] - b["hp1"]
        if lost > 0:
            (hits["in_guarded_move"] if (a["m1"] in guard and b["m1"] in guard) else hits["outside"]).append(lost)
    res["enemy_hp_drops"] = {k: v for k, v in hits.items()}
    # 원령 주탄: 프레임이 초당 얼마나 가나(원래 60/s, 절반 시간이면 약 110/s)
    on = [r for r in rows if r["m1"] == "ONRYOU" and 1 <= r["f1"] <= 560]
    if len(on) >= 2:
        dt = on[-1]["t"] - on[0]["t"]
        res["onryou_frames_per_s"] = round((on[-1]["f1"] - on[0]["f1"]) / dt, 1) if dt > 0 else None
    pl = out / "proxy_log_part.txt"
    if pl.exists():
        keys = ("Hard pack", "hard pack", "STORY_ONRYOU_FAST", "ONRYOU_FAST", "fight kind", "fight difficulty scope", "game_rank")
        res["proxy"] = [ln[20:].strip()[:200] for ln in pl.read_text(encoding="utf-8", errors="replace").splitlines()
                        if any(k in ln for k in keys)][:20]
    return res


def main():
    if our_pid() or S.running():
        sys.exit("기신비상이 켜져 있다 — 시험하지 않는다")
    stamp = time.strftime("%m%d_%H%M%S")
    save_bk = RUNS / f"save_before_{stamp}"
    shutil.copytree(G / "SAVE", save_bk)
    reg0 = realreg.query()
    diff0 = reg0.get("ActDifficult")
    pos0 = (reg0.get("XPos"), reg0.get("YPos"))
    realreg.set_dword("XPos", 20000)                    # 처음부터 화면 밖에서 켜지게(시험 도구는 끝에 이 값을 되돌리고, 아래에서 사용자 값으로)
    realreg.set_dword("YPos", 0)
    threads = [threading.Thread(target=keep_difficulty, daemon=True), threading.Thread(target=keep_offscreen, daemon=True)]
    if FIXTURE:
        threads.append(threading.Thread(target=fixture, daemon=True))
    for t in threads:
        t.start()
    try:
        D.main()
    finally:
        stop.set()
        time.sleep(1)
        if not our_pid() and pos0[0] is not None:
            realreg.set_dword("XPos", pos0[0])
            realreg.set_dword("YPos", pos0[1])
            notes.append(f"window position back to {pos0}")
        if not our_pid():
            for f in (G / "SAVE").rglob("*"):                 # 세이브 폴더를 시험 전 그대로
                rel = f.relative_to(G / "SAVE")
                if f.is_file() and not (save_bk / rel).exists():
                    f.unlink()
            for f in save_bk.rglob("*"):
                if f.is_file():
                    dst = G / "SAVE" / f.relative_to(save_bk)
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(f, dst)
            if diff0 is not None:
                realreg.set_dword("ActDifficult", diff0)
            notes.append(f"SAVE restored from {save_bk.name}; registry ActDifficult back to {diff0}")
        else:
            notes.append("the game is still running - SAVE and ActDifficult left as they are")
    out = T.OUT
    res = analyse(out)
    res.update(battle=BATTLE, want_difficulty=WANT, fixture=FIXTURE, notes=notes[:60])
    reg1 = realreg.query()
    res["registry_after"] = {k: reg1.get(k) for k in ("ActDifficult", "XPos", "YPos")}
    (RUNS / f"story_{BATTLE:02d}_{'normal' if WANT == 1 else 'hard'}_{stamp}.json").write_text(
        json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps(res, ensure_ascii=False, indent=1)[:6000])


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
