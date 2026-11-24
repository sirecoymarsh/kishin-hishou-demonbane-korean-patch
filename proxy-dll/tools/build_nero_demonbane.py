"""네로전 한정 데몬베인(11-03) — 사용자 「원작처럼 네로전에 한해 데몬베인의 크투가와 이타콰 대미지를 강하게 하고 신수 형태를 쓰게
할까? 그리고 아틀란티스 스트라이크를 공중으로 추적해서 명중시키게 하고」「기신포후를 말하는거야」→ 크투가·이타콰 「3배」.

균형 모드의 데몬베인 대 네로 장면(네로 지상 경로) 7320(수동)·7348(자동)에서만:
  ① 크투가·이타콰 탄(material/gun/ 아래, ATTACK(n) 이 있는 것 — 크투가탄·이타콰탄 다섯·신수 형태 C_BIG·I_BIG·쌍권총 기본탄)
     피해 3배: 탄 정의 사본(…_NR, ATTACK ×3) + 데몬베인 캐릭터 파일 사본 AMNR.cmn(그 탄 블록에 def_file …_NR 한 줄 —
     file 줄은 모델 등 기본 이름이라 그대로)
     신수 형태 탄(C_BIG·I_BIG)만 5배(사용자 「신수 형태를 대미지를 5배로」).
  ② 신수 형태(SINJUU, 총 소환 뒤 C+A = DUEL X+Z)는 기술표 조건 #14 == 1 — 체력 400 이하의 위기 대기(STOP_X)가 켜는 「필살기
     사용 깃발」. 11-03 시연은 데몬베인 정의 사본 AMHVNR(수동)·PAIR682P0R1NR(자동)의 START 에서 켰으나, 11-04 사용자 「크투가
     이타콰 신수 형태는 원래 필살기인데 지금 시험판이 그대로 들어가있는거 같아 그냥 체력 높은데도 막 쏘네」→ 원래대로 둔다.
     아틀란티스 스트라이크는 네로전에서 빼달라(「어색하네」) → 그 사본의 AS 동작(위기 때 총 없는 X+Z)은 1프레임에 KICK.
  ③ 자동 사본(7348)의 AI 는 원래 대기·이동 범주 블록의 한 줄 「기력 30 이상이면 이타콰」(총 소환 없이 이타콰만 쏜다) —
     사용자 「일단 이타콰 크투가 상태에서 AI가 신수 모드로 공격하게 해줘」→ 11-03 그 줄을 「기력 80 이상이면 신수 형태」로.
     11-04(위 ②): 그 줄은 「기력 80 이상이면 갈림길 NR_BEAST_AI」 — 필살기 깃발이 켜졌으면(위기) 신수 형태, 아니면 이타콰.
     기력 30 줄을 되살리면 위기에도 30 에서 먼저 쏴 80 까지 안 차므로, 위기가 아닐 때의 이타콰도 80 에서 쏜다.
     11-04 사용자 「이타콰만 쏘면 좀 그러니까 크투가를 섞어 쓰다가 필살기 신수 형태 쓰는걸로」→ 위기가 아니면 크투가·이타콰를 번갈아
     (차례 칸 #150 — 이타콰가 1, 크투가가 0 을 씀; 모든 DUEL 정의·대리 DLL 소스에 안 쓰이는 칸). 둘 다 기력 30 — 대미지는 그대로.
묶음 dmpack517_nero_demonbane.dat(> 510 의 7320·7348 장면을 덮음, < 520 → 520 을 이 위에 다시 빌드해 각인 666 반응을 얹는다).

  python -I -X utf8 build_nero_demonbane.py
"""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import dm_lib as D  # noqa: E402

GAME = Path(r"P:\AI\khd-runtime-fix\game")
ROOT = Path(r"P:\AI\khd-hero-ai\nerodb")
OUT = ROOT / "out" / "dmpack517_nero_demonbane.dat"
WORK = ROOT / "pack-work"
SKIP = (OUT.name, "dmpack520_kokuin666.dat", "dmpack521_kokuin666_test.dat", "dmpack530_ab_guard.dat")
D._EFF = D.effective(tuple(p.name for p in sorted(GAME.glob("dmpack*.dat")) if p.name not in SKIP), GAME)
SCENES = {7320: ("data/character/AM/AMHV", "data/character/AM/AMHVNR"),
          7348: ("data/character/AM/PAIR682P0R1", "data/character/AM/PAIR682P0R1NR")}
CMN_SRC, CMN_NEW = "data/character/AM/AM", "data/character/AM/AMNR"
FACTOR = 3
# 11-03 사용자 「신수 형태를 대미지를 5배로 늘려」: 신수 형태 탄(크투가 C_BIG 70·이타콰 I_BIG 50)만 5배, 나머지 크투가·이타콰 탄 3배
BEAST_FACTOR = {"c_big": 5, "i_big": 5}
SHOT_TURN = "#150"      # 11-04 자동 AI 의 크투가·이타콰 차례(기체마다 따로인 #0~#199 중 어느 DUEL 정의·대리 DLL 소스에도 없는 칸)
ATTACK = re.compile(r"ATTACK\((\d+)\)")
# 11-03 사용자 「네로 전에선 신수 형태만 쓰게 해줘 아틀란티스 스트라이크는 어색하네」: 네로전 사본의 AS 동작은 곧장 KICK 으로
# (필살기 깃발 #14 가 켜진 위기 때 총 없는 X+Z 가 AS 로 가므로 — 기술표·빌린 AI 의 이름 요청 둘 다 막힌다). AS 3배·공중 추적은 뺐다.


def nl_of(t: str) -> str:
    return "\r\n" if "\r\n" in t else "\n"


def motion_block(t: str, name: str) -> str | None:
    m = re.search(r"(?ms)^\s*motion\s+" + re.escape(name) + r"\s*\{.*?(?=^\s*motion\s|\Z)", t)
    return m.group(0) if m else None


def main():
    files: dict[str, bytes] = {}
    report = {"shots": {}, "defs": {}, "scenes": {}}
    cmn = D.read(CMN_SRC.lower() + ".cmn").decode("cp932")
    nl = nl_of(cmn)

    def add_def_file(m: re.Match) -> str:                 # 탄 블록마다: 크투가·이타콰 탄이면 file 줄 아래 def_file …_NR
        block = m.group(0)
        fm = re.search(r"(?m)^([ \t]*)file[ \t]+(data/character/material/gun/\S+)[^\r\n]*\r?\n", block)
        if not fm or re.search(r"(?m)^\s*def_file\s", block):
            return block
        src = fm[2]
        name = src.lower() + ".def"
        if name not in D._EFF:
            return block
        t = D.read(name).decode("cp932")
        if not ATTACK.search(re.sub(r"//[^\n]*", "", t)):   # 피해 없는 탄(불꽃 등)은 그대로
            return block
        new = src + "_NR"
        if new.lower() + ".def" not in {k.lower() for k in files}:
            k = BEAST_FACTOR.get(Path(src).name.lower(), FACTOR)
            t2, n = ATTACK.subn(lambda a: f"ATTACK({int(a[1]) * k})", t)
            files[new + ".def"] = t2.encode("cp932")
            report["shots"][src] = dict(copy=new, attacks=sorted({int(x) for x in ATTACK.findall(t)}), factor=k, lines=n)
        return block[:fm.end()] + f"{fm[1]}def_file {new}{nl}" + block[fm.end():]

    cmn2 = re.sub(r"(?ms)^\s*material\s*\(\s*(?!robot)\w+\s*,\s*\w+[^)]*\)\s*\{.*?\n\s*\}", add_def_file, cmn)
    assert cmn2 != cmn
    files[CMN_NEW + ".cmn"] = cmn2.encode("cp932")
    for s, (src, dst) in SCENES.items():                 # 데몬베인 정의 사본: AS 는 킥으로(신수 형태는 원래대로 위기 때만)
        t = D.read(src.lower() + ".def").decode("cp932")
        n = nl_of(t)
        b = motion_block(t, "AS")
        kick = re.search(r"(?m)^\s*DMO_motion\s+(\S+)", motion_block(t, "KICK") or "")
        cat = re.search(r"(?m)^\s*category\s+(\d+)", b or "")
        assert b and kick and cat, src
        head = b[:len(b) - len(b.lstrip())]
        stub = n.join(["motion AS {", f"    DMO_motion {kick[1]}", f"    category {cat[1]}",
                       "    jump VOID 1 0 MOTION(KICK)   // DUEL 11-03 Nero fight: no Atlantis Strike (user) - X+Z without guns kicks",
                       "}", "", ""])
        t = t.replace(b, head + stub, 1)
        # 신수 형태(와 AS)의 #14 는 위기 대기(STOP_X)가 켜는 필살기 깃발 — 11-03 시연은 START 에서 켰으나 11-04 사용자 「원래
        # 필살기인데…체력 높은데도 막 쏘네」로 원래대로 둔다(START 는 #14 를 건드리지 않는다)
        assert "#14" not in (motion_block(t, "START") or "#14"), src
        if s == 7348:
            # 이타콰 줄을 남겨 두면 기력 30 에서 먼저 쏴 버려 80 까지 차지 않는다 → 기력 80 줄로 바꿔 끼운다(수동 사본엔 이 줄이 없다).
            # jump 줄 하나엔 조건이 하나라 필살기 깃발은 갈림길 동작에서 본다 — 분류는 이타콰와 같은 12(interrupt 블록이 없어
            # 기력 줄이 다시 걸리지 않고, 공격 중인 동작이라 AI 가 덮어쓰지 않는다)
            t, k = re.subn(r"(?m)^([ \t]*)jump COMP\(#101>=30\) \[20,999\] 0 MOTION\(ITACUA\)[^\r\n]*",
                           lambda m: m[1] + "jump COMP(#101>=80) [20,999] 0 MOTION(NR_BEAST_AI)   // DUEL Nero fight AI: the beast form as the finisher (user 11-03, 11-04)", t)
            assert k == 2, k
            sj = re.search(r"(?m)^[ \t]*motion[ \t]+SINJUU[ \t]*\{", t)
            assert sj and "NR_BEAST_AI {" not in t and SHOT_TURN not in t, src
            gate = n.join(["// DUEL 11-04 Nero fight AI: the beast form only with the game's finisher flag (#14 == 1, set by the pinch stance",
                           f"// STOP_X at 400 HP or less), otherwise Cthugha and Ithaqua by turns ({SHOT_TURN}, set by the two shots)",
                           "motion NR_BEAST_AI {", "DMO_motion STOP", "category 12",
                           "    jump COMP(#14==1) -1 0 MOTION(SINJUU)",
                           f"    jump COMP({SHOT_TURN}==1) 2 0 MOTION(CTHUGUA)",
                           "    jump VOID 3 0 MOTION(ITACUA)", "}", "", ""])
            t = t[:sj.start()] + gate + t[sj.start():]
            for mv, nxt in (("ITACUA", 1), ("CTHUGUA", 0)):     # 다음 차례: 이타콰 뒤엔 크투가(1), 크투가 뒤엔 이타콰(0)
                b = motion_block(t, mv)
                cat = re.search(r"(?m)^([ \t]*category[ \t]+\d+[^\r\n]*\r?\n)", b or "")
                assert b and cat, (src, mv)
                t = t.replace(b, b[:cat.end()] + f"    set_param(1, {SHOT_TURN}, {nxt})   // DUEL 11-04 Nero fight AI: "
                              f"the next gun shot is {'Cthugha' if nxt else 'Ithaqua'}{n}" + b[cat.end():], 1)
            report["ai"] = dict(def_=dst, lines=k, rule=f"#101>=80 -> NR_BEAST_AI: #14==1 -> SINJUU, else {SHOT_TURN}==1 -> CTHUGUA, "
                                                         "else ITACUA (was #101>=30 -> ITACUA; 11-03 #101>=80 -> SINJUU)")
        files[dst + ".def"] = t.encode("cp932")
        report["defs"][src] = dst
        name = f"data/scene/scene{s}/scene{s}.cmn"         # 장면: 데몬베인 블록의 정의·캐릭터 파일만 사본으로
        sc = D.read(name).decode("cp932")
        sc2, k1 = re.subn(r"(?m)^([ \t]*def_file[ \t]+)" + re.escape(src) + r"([ \t]*\r?)$", lambda m: m[1] + dst + m[2], sc, count=1)
        sc2, k2 = re.subn(r"(?m)^([ \t]*cmn_file[ \t]+)" + re.escape(CMN_SRC) + r"([ \t]*\r?)$", lambda m: m[1] + CMN_NEW + m[2], sc2, count=1)
        assert k1 == 1 and k2 == 1, (s, k1, k2)
        files[name] = sc2.encode("cp932")
        report["scenes"][s] = dict(def_=dst, cmn=CMN_NEW, from_pack=Path(D._EFF[name][1]).name)
    for name in files:
        assert name.lower() not in D._EFF or name.lower().startswith("data/scene/scene73"), name
    ROOT.mkdir(parents=True, exist_ok=True)
    (ROOT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"탄 사본 {len(report['shots'])}: " + ", ".join(f"{Path(k).name} {v['attacks']}" for k, v in report["shots"].items()))
    WORK.mkdir(parents=True, exist_ok=True)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    specs = []
    for name, data in files.items():
        p = WORK / name.replace("/", "__")
        p.write_bytes(data)
        specs.append(f"{name.replace('/', chr(92))}={p}")
    (WORK / "_specs.txt").write_text("\n".join(specs) + "\n", encoding="utf-8")
    r = subprocess.run([sys.executable, "-I", str(D.TOOLS / "gupk_build.py"), str(GAME / "demonbane.dll"), str(OUT),
                        "@" + str(WORK / "_specs.txt")], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit(r.returncode)
    print(OUT, hashlib.sha256(OUT.read_bytes()).hexdigest())


main()
