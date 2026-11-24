"""네로 등·치마 막기 풀기 A/B 용 시험 묶음(11-03) — 지금 520 의 네로 정의 몇 개를 그대로 두고 그 두 줄만 GUARD(100) 로 되돌린다.
「고침 없음」 판에만 run_duel_test --pack= 으로 덧넣는다(530 > 520 이라 덮음) → 두 판의 차이는 그 두 줄뿐.

  python -I -X utf8 build_ab_guard.py
"""
import hashlib
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import dm_lib as D  # noqa: E402

GAME = Path(r"P:\AI\khd-runtime-fix\game")
SRC = Path(r"P:\AI\khd-hero-ai\kokuin\pack-work")          # 지금 520 의 정의들(각인 666·판 끝 고침 포함)
OUT = Path(r"P:\AI\khd-hero-ai\neroguard\out\dmpack530_ab_guard.dat")
WORK = Path(r"P:\AI\khd-hero-ai\neroguard\pack-work-ab")
DEFS = ["data/character/qm/ng_live_262_am", "data/character/qm/ng_live_70_am", "data/character/qm/ng_live_326_am",
        "data/character/qm/qmdvb"]                          # AM:QM 7320 · GM:QM 7300 · CM:QM 7323 · BM:QM·HM:QM 사본(QMDVB)
HIT = re.compile(r"(?m)^([ \t]*collision[ \t]+collid_(?:back|skirt)[ \t]+collid_root[ \t]+SEPARATER&)HIT")

WORK.mkdir(parents=True, exist_ok=True)
specs = []
for d in DEFS:
    src = SRC / (d.replace("/", "__") + ".def")
    t, n = HIT.subn(r"\1GUARD(100)", src.read_bytes().decode("cp932"))
    assert n == 2, (d, n)
    p = WORK / src.name
    p.write_bytes(t.encode("cp932"))
    specs.append(f"{d.replace('/', chr(92))}.def={p}")
    print(f"{d}: back·skirt → GUARD(100)")
(WORK / "_specs.txt").write_text("\n".join(specs) + "\n", encoding="utf-8")
r = subprocess.run([sys.executable, "-I", str(D.TOOLS / "gupk_build.py"), str(GAME / "demonbane.dll"), str(OUT),
                    "@" + str(WORK / "_specs.txt")], capture_output=True, text=True)
print(r.stdout.strip() or r.stderr.strip())
print(OUT, hashlib.sha256(OUT.read_bytes()).hexdigest())
