"""DUEL 전용 네로 각인 666(11-01) — 완성되면 상대가 리베르 레기스처럼 한 점으로 줄어들며 사라지고 패배.

사용자 11-01 「네로의 각인 666은 원래 자체 효과가 있는 기술이 아니라 완성되면 리벨 레기스가 소실되는 특수 상태가 발동되는
기술이거든? 이걸 모든 상황에서 효과있게」→「듀얼 모드에서만 그렇게 해주는데, 문제는 연출이야. 원래 리벨 레기스는 완성되면
몸이 한 점으로 압축되듯이 쪼그라들며 사라지거든. 3D 모델로 하는거라 다른 기체도 재현 가능하지 않을까?」

원래(QM.def KOKUIN666): 1149프레임에 공유 #220=666 한 프레임 → 리베르 레기스(BM011)만 모든 interrupt 에서 DAMAGE_666
(부품 keyframe 1→2→0.01, 흰→검 필터, 100프레임에 enemy setparam:100:0 + GAME_OVER — 이야기 연출: 네로 쪽 체력이 0).
네로 정의 1151프레임엔 개발 때의 `material message 1151 enemy setparam:100:0` 이 주석으로 남아 있다.

이 묶음(dmpack520_kokuin666.dat, 번호가 가장 높아 같은 이름을 덮음)은 네로가 낀 DUEL 장면의 두 기체 정의만 고친다:
- 네로(QM) 사본: 1149프레임의 공유 #220 한 프레임 신호 대신 `material message 1149 enemy setparam:166:666` — 상대 자신의
  칸(#0~#199 는 기체마다 따로)에 남으니 갱신 순서·그 순간의 동작과 상관없이 상대가 다음 interrupt 에서 받는다(11-01 첫 시험:
  공유 #385 한 프레임은 네로가 먼저 갱신되는 DUEL 에서 상대가 못 봄). 시전자 자기 칸은 그대로라 자기 신호에 안 걸린다.
  1259프레임에 상대 체력 0(원본 PM 「요그=소토스」가 쓰는 같은 명령).
- 모든 상대 사본(거울전 네로 포함): 모든 interrupt 첫 줄에 #166==666 → DUEL_666. DUEL_666 = 끼어들기 없는 분류, 그 모델의 보이는 부품을 팔다리→그 밖→몸통 순으로
  2배로 부풀렸다 0.01 로, 흰 섬광, 그 정의 LOSE1 과 같은 간격으로 ENEMY:DEF:GAME_OVER(DUEL 판 끝 신호는 진 쪽 LOSE1 이 보냄).
  리베르 레기스는 원래 DAMAGE_666 의 keyframe·material 줄을 그대로 쓴다.
이야기 원본 이름(dmpack.dat·001~003 에서 오는 정의)은 고치지 않는다.

python -I -X utf8 build_kokuin666.py [--dry]
python -I -X utf8 build_kokuin666.py --test     시험 모드 묶음 dmpack521_kokuin666_test.dat(네로 사본 99개만: 시전을 아무것도 못 끊고
                                                 무적 — 520 위에 시험 때만 넣는다, show_hero_ai.py --kokuin-test)
"""
from __future__ import annotations

import collections
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-runtime-fix\work\command_list\render")
sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import dm_lib as D  # noqa: E402
import dmo_mesh as M  # noqa: E402

GAME = Path(r"P:\AI\khd-runtime-fix\game")
OUT = Path(r"P:\AI\khd-hero-ai\kokuin\out\dmpack520_kokuin666.dat")
WORK = Path(r"P:\AI\khd-hero-ai\kokuin\pack-work")
BASE = ("dmpack.dat", "dmpack001.dat", "dmpack002.dat", "dmpack003.dat")
TEST = "--test" in sys.argv     # 11-02 시험 모드 묶음(네로 사본만, 520 위에 덧넣음): 사용자 「시작부터 각인 666 써대고 방해 안받는 모드」
OUT_TEST = Path(r"P:\AI\khd-hero-ai\kokuin\out\dmpack521_kokuin666_test.dat")
PACKS = tuple(p.name for p in sorted(GAME.glob("dmpack*.dat")) if p.name not in (OUT.name, OUT_TEST.name))
D._EFF = D.effective(PACKS, GAME)
SIGNAL = 166             # 상대 자신의 칸(정의·팩 어디에도 안 쓰는 번호, 11-01 전수 확인): 네로가 message 로 상대에게만 666
KO_FRAME = 1259          # 네로 시전 프레임: 신호 1149 + 상대 수축·섬광 110
CATEGORY = 78            # DUEL_666: interrupt 블록 없는 분류(DUEL_HELD 는 77)
SHRINK_END = 100         # 상대 DUEL_666 안에서 마지막 부품이 0.01 이 되는 프레임 상한


def duel_scene(s: int) -> bool:   # 대리 DLL IsDuelScene 과 같은 범위(+ 전용 9xxx)
    return (s in (9301, 9302, 9303, 9304, 9083, 9109) or 7300 <= s <= 7355 or 58 <= s <= 1999
            or 5923 <= s <= 5926 or s == 5910 or 5940 <= s <= 5942 or 9400 <= s <= 9999)


def actors(cmn: str) -> list[tuple[str, str | None]]:
    """depth-0 material(robot, SLOT, ...) blocks, the first one per SLOT (the engine uses that one; DUEL scenes keep
    the stage's old robot block below the DUEL one): (def_file, dmo_file or file)"""
    out, depth, cur, seen = [], 0, None, set()
    for line in cmn.splitlines():
        s = line.split("//", 1)[0]
        m = re.match(r"\s*material\s*\(\s*robot\s*,\s*(\w+)", s)
        if depth == 0 and m:
            cur = {"slot": m[1], "def": None, "dmo": None, "file": None}
        if cur is not None and depth == 1:
            m2 = re.match(r"\s*(def_file|dmo_file|file)\s+(\S+)", s)
            if m2:
                cur[{"def_file": "def", "dmo_file": "dmo", "file": "file"}[m2[1]]] = m2[2]
        depth += s.count("{") - s.count("}")
        if cur is not None and depth == 0 and "}" in s:
            d = cur["def"] or cur["file"]
            if d and cur["slot"] not in seen:
                seen.add(cur["slot"])
                out.append((d, cur["dmo"] or cur["file"]))
            cur = None
    return out


def state_visible(rest: str) -> bool:
    tok = rest.strip().split()
    return bool(tok) and not tok[0].lower().startswith("off")


def visible_parts(t: str) -> list[str]:
    head = t.split("\nmotion", 1)[0]
    return [m[1] for m in re.finditer(r"(?m)^\s*parts\s+(\S+)\s+0\s+(.*)$", head) if state_visible(m[2])]


GROUPS = (("arm", "sl_", "shoulder", "hand"), ("leg", "thigh", "knee", "foot"), ("wing",))


def group_of(name: str) -> int:
    n = name.lower()
    for g, keys in enumerate(GROUPS):
        if any(k in n for k in keys):
            return g
    return len(GROUPS)


def shrink_plan(def_t: str, dmo: str) -> tuple[list[tuple[str, int]], int]:
    """[(노드, 시작 프레임)…], 끝 프레임 — 팔다리·그 밖 맨 위 부품을 차례로, 가장 큰 몸통을 마지막에"""
    nodes = M.walk(D.read(dmo + ".dmo"))
    by = {n["name"]: n for n in nodes}
    vis = [v for v in dict.fromkeys(visible_parts(def_t)) if v in by]
    if not vis:
        raise ValueError("보이는 부품 없음")
    vset = set(vis)

    def vparent(nm):
        p = by[nm]["par"]
        while p >= 0:
            if nodes[p]["name"] in vset:
                return nodes[p]["name"]
            p = nodes[p]["par"]
        return None

    par = {v: vparent(v) for v in vis}
    top = [v for v in vis if par[v] is None]
    size = {v: by[v]["x"].count("\n") for v in vis}
    body = max(top, key=lambda v: size[v])
    limbs = [v for v in top if v != body] + [v for v in vis if par[v] == body]
    limbs.sort(key=lambda v: (group_of(v), by[v]["i"]))
    step = max(2, min(5, 40 // max(1, len(limbs))))
    plan = [(v, 10 + k * step) for k, v in enumerate(limbs)]
    tb = max(45, 10 + len(limbs) * step + 5)
    tb = min(tb, SHRINK_END - 40)
    plan.append((body, tb))
    return plan, tb + 40


KEY = "        key {f:4d} CHILD  0.0 0.0 0.0   0.0 0.0 0.0   {s}   {s}   {s}  255 255 255 255"


def keyframes(plan) -> str:
    out = []
    for node, t in plan:
        out += [f"keyframe {node} {{", KEY.format(f=t, s="1.0"), KEY.format(f=t + 5, s="2.0"),
                KEY.format(f=t + 40, s="0.01"), "}"]
    return "\n".join(out)


def motion_block(t: str, name: str) -> str | None:
    m = re.search(r"(?ms)^\s*motion\s+" + re.escape(name) + r"\s*\{.*?(?=^\s*motion\s|\Z)", t)
    return m.group(0) if m else None


def stop_dmo(t: str) -> str:
    """DUEL_666 의 DMO 애니메이션 — 그 정의의 서 있는 동작이 이미 쓰는 이름만. 모델에 없는 이름을 가리키면 그 동작을 안 써도
    전투 첫 프레임에 demonbane+0x31014(UpdateMotion) 접근 위반으로 게임째 죽는다(11-02 MM 은 STOP_A 뿐인데 「STOP」, 10-18 RM·SM 「WIN」)."""
    for name in ("STOP", "STOP_G", "STOP_A", "START", "WAIT"):
        m = re.search(r"(?m)^\s*DMO_motion\s+(\S+)", motion_block(t, name) or "")
        if m:
            return m[1]
    raise ValueError("서 있는 동작(STOP·STOP_G·STOP_A·START·WAIT)의 DMO 가 없음")


def loss_motion(t: str) -> str:
    """이 정의가 질 때 가는 동작(interrupt 의 YOULOSE 점프 대상): 1P 조종 사본은 PLOSE·LOSE1(SELF:DEF:GAME_OVER),
    2P 상대 사본은 WAIT → LOSE1(ENEMY:DEF:GAME_OVER). 11-02 사용자 「각인 666으로 승리하면 어나더 블러드와의 전투가 끝나지
    않는 버그」 — DUEL_666 이 늘 ENEMY 로 보내 1P 가 사라진 판(CPU 네로가 쓴 각인)은 판 끝 신호가 2P 로 가서 안 끝났다."""
    clean = re.sub(r"//[^\n]*", "", t)
    tg = collections.Counter(re.findall(r"MSG\(SYS:YOULOSE\)\s+-?\d+\s+\d+\s+MOTION\((\w+)\)", clean))
    name = tg.most_common(1)[0][0] if tg else "LOSE1"
    return "LOSE1" if name == "WAIT" else name


def game_over(t: str, name: str) -> tuple[int, str, str] | None:
    """그 동작(과 jump VOID … MOTION(다음) 로 이어지는 LOSE1→LOSE2→LOSE3)에서 GAME_OVER 사건 → (그 동작 시작부터의 프레임, 사건, 사건이 든 동작)"""
    offset, seen = 0, set()
    while name and name not in seen and len(seen) < 4:
        seen.add(name)
        b = re.sub(r"//[^\n]*", "", motion_block(t, name) or "")
        m = re.search(r"event\s+VOID\s+(\d+)\s+\d+\s+((?:SELF|ENEMY):DEF:GAME_OVER)", b)
        if m:
            return offset + int(m[1]), m[2], name
        j = re.search(r"jump\s+VOID\s+(\d+)\s+\d+\s+MOTION\((\w+)\)", b)
        if not j:
            return None
        offset, name = offset + int(j[1]), j[2]
    return None


def lose_tail(t: str, go: int, motion: str, lose_go: int) -> list[str]:
    """보통 KO 의 끝맺음(그 동작의 흰 빛 → 검게, 음악 줄이기)을 GAME_OVER 까지 같은 간격으로 — 판 끝이 갑자기 끊기지 않게"""
    b = motion_block(t, motion) or ""
    ev = [(int(m[1]), m[2]) for m in re.finditer(
        r"(?m)^\s*event\s+VOID\s+(\d+)\s+\d+\s+(FILTER2:\S+|SELF:DEF:BGM_FADE:\S+)", b)]
    if not any(e.startswith("FILTER2") for _, e in ev):     # 끝맺음이 없는 정의(BM000DV): 흔한 꼴(DM·GM LOSE1)
        ev = [(lose_go - 210, "FILTER2:40:255:255:255:0:255:255:255:255:0"),
              (lose_go - 180, "FILTER2:70:255:255:255:255:255:255:255:255:0"),
              (lose_go - 120, "FILTER2:70:255:255:255:255:0:0:0:255:0"),
              (lose_go - 60, "FILTER2:600:0:0:0:255:0:0:0:255:0"), (lose_go - 210, "SELF:DEF:BGM_FADE:-30.0")]
    return [f"    event VOID {f + go - lose_go} {f + go - lose_go} {e}   // {motion} {f}" for f, e in ev]


def duel_666(t: str, dmo: str, is_bm: bool) -> tuple[str, dict]:
    if is_bm:   # 리베르 레기스: 원래 DAMAGE_666 의 keyframe·material 줄 그대로
        b = motion_block(t, "DAMAGE_666")
        body = "\n".join(l for l in b.splitlines()
                         if re.match(r"\s*(material\s+(shoot|message)|keyframe|key\s|\})", l) and "setparam" not in l)
        body = body.rsplit("}", 1)[0]       # 블록 끝 괄호 하나(동작의 닫는 괄호)는 아래에서 다시 붙인다
        end, info = 80, {"nodes": "BM011 DAMAGE_666 원본"}
    else:
        plan, end = shrink_plan(t, dmo)
        body, info = keyframes(plan), {"nodes": [n for n, _ in plan]}
    lm = loss_motion(t)
    g = game_over(t, lm)
    lose_go, go_event, go_motion = g if g else (520, "ENEMY:DEF:GAME_OVER", lm)
    go = 110 + lose_go
    text = "\n".join([
        "",
        f"// ---- DUEL 11-01: Nero KOKUIN666 (#{SIGNAL} set by the enemy message) - parts swell then shrink to a point, like BM011 DAMAGE_666 ----",
        "motion DUEL_666 {",
        f"    DMO_motion {stop_dmo(t)}",
        f"    category {CATEGORY}",
        "    set_param(2,  #222, 0)",
        f"    set_param(1,  #{SIGNAL}, 0)",
        body,
        f"    event VOID {end - 5} {end - 5} SELF:DEF:COMFLASH",
        f"    event VOID {end - 5} {end - 5} FILTER2:10:255:255:255:0:255:255:255:255:0",
        f"    event VOID {end + 5} {end + 5} FILTER2:10:255:255:255:255:255:255:255:255:0",
        f"    event VOID {end + 15} {end + 15} FILTER2:20:255:255:255:255:255:255:255:0:0",
        *lose_tail(t, go, go_motion, lose_go),
        f"    event VOID {go} {go} {go_event}  //ACTパート終了 (same delay and target as this def's {go_motion}, after the KO)",
        f"    jump VOID {go + 300} {go + 200}",
        "}",
        ""])
    info.update(end=end, game_over=go, game_over_event=go_event, loss_motion=lm, game_over_in=go_motion)
    return text, info


def add_reaction(t: str, skip: set[int], nl: str) -> tuple[str, int]:
    n = 0

    def rep(m):
        nonlocal n
        if int(m[1]) in skip:
            return m[0]
        n += 1
        return m[0] + f"{nl}    jump COMP(#{SIGNAL}==666) -1 0 MOTION(DUEL_666)   // DUEL 11-01 Nero KOKUIN666"
    return re.sub(r"(?m)^interrupt\s+(\d+)\s*\r?\n\s*\{", rep, t), n


def patch_nero(t: str) -> tuple[str, list[str]]:
    b = motion_block(t, "KOKUIN666")
    if not b:
        return t, ["KOKUIN666 없음"]
    nb, k1 = re.subn(r"set_param\(\s*1149\s*,\s*#220\s*,\s*666\s*\)",
                     f"material message 1149 enemy setparam:{SIGNAL}:666   // DUEL 11-01: the story #220 pulse, to the enemy only", b)
    nb, k2 = re.subn(r"set_param\(\s*1150\s*,\s*#220\s*,\s*0\s*\)",
                     f"material message {KO_FRAME} enemy setparam:100:0   // DUEL 11-01: KO after the shrink (the commented 1151 line)", nb)
    if k1 != 1 or k2 != 1:
        return t, [f"#220 줄 {k1}/{k2}"]
    return t.replace(b, nb, 1), []


def test_cpu_cast(t: str) -> tuple[str, int]:
    """시험 모드(2P 네로 정의만): 시작 동작(START)이 끝나면 STOP 대신 곧장 KOKUIN666 — CPU 네로가 1P 에게 각인을 쓰는 판을
    만들어 1P 가 사라질 때 판이 끝나는지 본다(11-02 「어나더 블러드와의 전투가 끝나지 않는 버그」 — 네이티브 AI 는 각인을 거의 안 씀)"""
    b = motion_block(t, "START")
    if not b:
        return t, 0
    nb, n = re.subn(r"(jump\s+VOID\s+\d+\s+\d+\s+MOTION\()STOP\w*\)", r"\1KOKUIN666)", b, count=1)
    return t.replace(b, nb, 1), n


def test_nero(t: str, nl: str) -> str:
    """시험 모드: 시전 본체를 KOKUIN666_T(interrupt 블록 없는 분류 = 잡기·피격 반응·명령이 못 끊음, 처음부터 무적, 승리 신호는
    동작 안에서 직접)로 옮기고, KOKUIN666 은 분류 8 그대로 1프레임에 그리로 넘기는 껍데기. 11-02 첫 시험 판은 KOKUIN666 자체를
    분류 78 로 바꿔 입력이 아예 안 받아졌다 — 입력 요청은 지금 동작의 interrupt 블록에 그 분류 MREQ(8) 가 있어야 시작된다."""
    b = motion_block(t, "KOKUIN666")
    body, k1 = re.subn(r"(?m)^(\s*motion\s+)KOKUIN666\b", r"\g<1>KOKUIN666_T", b, count=1)
    body, k2 = re.subn(r"(?m)^([ \t]*category[ \t]+)8\b([^\r\n]*)",
                       lambda m: f"{m[1]}{CATEGORY}{m[2]}   // DUEL test mode: no interrupt block{nl}"
                                 f"    event VOID 1 1 SELF:DEF:MUTEKI:1400   // DUEL test mode: no damage during the cast{nl}"
                                 f"    jump MSG(SYS:YOUWIN) -1 0 MOTION(PWIN)", body, count=1)
    if k1 != 1 or k2 != 1:
        raise ValueError(f"KOKUIN666 시험 판 못 만듦 {k1}/{k2}")
    stub = nl.join(["motion KOKUIN666 {", "    DMO_motion KOKUIN666", "    category 8",
                    "    jump VOID 1 0 MOTION(KOKUIN666_T)   // DUEL test mode: the request needs category 8 (MREQ(8)), the cast runs in 78",
                    "}", ""])
    return t.replace(b, nl + stub + body, 1)


def main():
    dry = "--dry" in sys.argv
    out, work = (OUT_TEST, WORK.with_name("pack-work-test")) if TEST else (OUT, WORK)
    scenes, actor_defs = {}, {}
    for n in D._EFF:
        m = re.match(r"data/scene/scene(\d+)/scene\1\.cmn$", n)
        if not (m and duel_scene(int(m[1]))):
            continue
        acts = actors(D.read(n).decode("cp932", "replace"))
        if any("/qm/" in d.lower() for d, _ in acts):
            scenes[int(m[1])] = acts
            for d, dmo in acts:
                actor_defs.setdefault(d.lower(), set()).add(dmo.lower() if dmo else None)
    report = dict(scenes=len(scenes), defs={}, skipped={})
    files = {}
    for d in sorted(actor_defs):
        name = d + ".def"
        if Path(D._EFF[name][1]).name in BASE:
            report["skipped"][d] = "이야기 원본 파일"
            continue
        t = D.read(name).decode("cp932")
        folder = d.rsplit("/", 1)[0]
        dmos = [x for x in actor_defs[d] if x] or [folder + "/" + folder.rsplit("/", 1)[1]]
        dmo = sorted(dmos)[0]
        is_qm, is_bm = "/qm/" in d, "DAMAGE_666" in t
        notes = []
        if re.search(rf"(?m)^interrupt\s+{CATEGORY}\b", t) or "motion DUEL_666" in t:
            report["skipped"][d] = f"분류 {CATEGORY} 또는 DUEL_666 이 이미 있음"
            continue
        if is_qm:
            t, notes = patch_nero(t)
        try:
            block, info = duel_666(t, dmo, is_bm)
        except (ValueError, KeyError) as e:
            report["skipped"][d] = f"수축 동작 못 만듦: {e}"
            continue
        # 11-02: 넣는 줄은 원본 줄 끝(CRLF)으로 — 엔진은 LF 를 줄 끝으로 안 봐서, LF 로 붙인 DUEL_666 은 첫 「//」 주석이
        # 다음 CRLF(=파일 끝)까지 먹어 없는 동작이 됐다(상대가 1프레임에 묶이고 판이 안 끝남). 원본 바이트는 그대로 둔다.
        nl = "\r\n" if "\r\n" in t else "\n"
        t, nint = add_reaction(t, set(), nl)    # 신호가 상대 자신의 칸이라 네로 사본도 시전 분류 8 까지 다 받는다(거울전)
        t = t.rstrip() + nl + block.replace("\n", nl)
        if TEST:
            if not is_qm:
                continue
            t = test_nero(t, nl)
            if info["game_over_event"].startswith("ENEMY"):      # 2P 로 쓰이는 네로 정의
                t, n_cast = test_cpu_cast(t)
                info["test_cpu_cast"] = n_cast
        t.encode("cp932")                        # 정의는 cp932 — 한글 주석은 cp932 로 안 되면 여기서 멈춘다
        files[name] = t.encode("cp932")
        report["defs"][d] = dict(dmo=dmo, nero=is_qm, bm=is_bm, interrupts=nint, notes=notes, **info,
                                 from_pack=Path(D._EFF[name][1]).name)
    print(f"장면 {len(scenes)}, 고친 정의 {len(files)}, 건너뜀 {len(report['skipped'])}")
    for d, why in report["skipped"].items():
        print("  건너뜀", d, why)
    bad = {d: r["notes"] for d, r in report["defs"].items() if r["notes"]}
    if bad:
        print("  네로 사본 문제:", bad)
    out.parent.mkdir(parents=True, exist_ok=True)
    (out.parent.parent / ("report_test.json" if TEST else "report.json")).write_text(
        json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    if dry:
        return
    work.mkdir(parents=True, exist_ok=True)
    specs = []
    for name, data in files.items():
        p = work / name.replace("/", "__")
        p.write_bytes(data)
        specs.append(f"{name.replace('/', chr(92))}={p}")
    listing = work / "_specs.txt"
    listing.write_text("\n".join(specs) + "\n", encoding="utf-8")
    r = subprocess.run([sys.executable, "-I", str(D.TOOLS / "gupk_build.py"), str(GAME / "demonbane.dll"), str(out),
                        "@" + str(listing)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit(r.returncode)
    print(out, hashlib.sha256(out.read_bytes()).hexdigest())


main()
