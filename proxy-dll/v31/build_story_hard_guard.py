"""v31 (11-19): 스토리 어려움 묶음에 긴 시전 가드 — 사용자 「가드·충전 넣어」(DUEL 어려움의 가드·원령 주탄 모으기 절반을 스토리 어려움에도,
위력 3배는 뺌).

DUEL 어려움의 묶음 542(hmduel\\build_hard_cast_guard_pack.py)와 같은 줄을 스토리 어려움 묶음(autobattle\\hard\\dmpack900_hard.dat,
10-14 v3 40466E40)의 적 정의 셋에 넣는다:
    티베리우스 폭식 대죄(IM TAIZAI) · 오거 비검·비룡(NM HIRYUU) · 데몬 블러드 금기의 피 해방(GM KINKI1)
그 동작의 분류(category) 줄 바로 뒤에, 정의 머리가 선언한 몸 판정(…HIT…)마다
    collision <판정> VOID -1 -1 SEPARATER&GUARD(50) <그 판정의 네 수>
(원작 개발자의 꼴 - 각인 666 주석 블록, 레거시 오브 골드 레이저). 스토리에서 IM·NM·GM 은 적만 쓰고(우리 편은 AM·BM011·CM·EM·FM·HM),
이 묶음은 어려움일 때만 게임 폴더에 들어가므로 쉬움·보통은 그대로.
나머지 항목은 바이트 그대로(키도 그대로). 이름은 역슬래시로 쓴다 — 엔진은 data\\character\\... 로 찾는다(11-18 커맨드 리스트 교훈).
  python -I -X utf8 build_story_hard_guard.py [기준 묶음] [출력]
"""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs = {}
_argv, sys.argv = sys.argv, [sys.argv[0], r"F:\AI\khd-latest-game\demonbane.dll"]
exec(compile(src, "gupk_sounds.py", "exec"), gs)
sys.argv = _argv
DLL = r"F:\AI\khd-latest-game\demonbane.dll"
TABLE = gs["read_pe_va"](DLL, gs["LOOKUP_TABLE_VA"], 6 * 256)
HERE = Path(__file__).resolve().parent
args = [a for a in sys.argv[1:] if not a.startswith("--")]
BASE = Path(args[0]) if args else Path(r"F:\AI\khd-latest-game\autobattle\hard\dmpack900_hard.dat")
OUT = Path(args[1]) if len(args) > 1 else HERE / "out" / "dmpack900_hard.dat"
WORK = HERE / "work_story_guard"
BASE_SHA = "40466e40"
MOVES = {"data/character/im/im.def": ["TAIZAI"], "data/character/nm/nm.def": ["HIRYUU"], "data/character/gm/gm.def": ["KINKI1"]}
TAG = "// 11-19 v31 story hard: auto guard GUARD(50) through {moves} (as DUEL hard pack 542)"
MARK = "// 11-19 v31: auto guard through this move (story hard)"

base_sha = hashlib.sha256(BASE.read_bytes()).hexdigest()
if not base_sha.startswith(BASE_SHA):
    print(f"warning: base pack is {base_sha[:8]}, not {BASE_SHA}")
rows = []
with open(BASE, "rb") as f:
    for idx, name, key, unpacked, off, size in gs["entries"](str(BASE), TABLE):
        data = gs["extract"](str(BASE), TABLE, key, unpacked, off, size)
        rows.append((name, key, data))
print(f"base {BASE.name} {base_sha[:8]}: {len(rows)} entries")


def guard(text: str, moves: list[str]) -> tuple[str, list[str], list[str]]:
    nl = "\r\n" if "\r\n" in text else "\n"
    first = re.search(r"(?m)^\s*motion\s", text)
    head = text[:first.start()] if first else text
    vols = []
    for ln in head.splitlines():
        m = re.match(r"\s*collision\s+(\w+)\s+\S+\s+(\S+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", ln)
        if m and "HIT" in m.group(2):
            vols.append((m.group(1), " ".join(m.group(i) for i in range(3, 7))))
    new, done = text, []
    for mv in moves:
        mm = re.search(r"(?m)^[ \t]*motion[ \t]+%s\b[^\r\n]*\r?\n" % re.escape(mv), new)
        if not mm:
            continue
        cat = re.compile(r"(?m)^[ \t]*category[ \t]+\d+[^\r\n]*\r?\n").search(new, mm.end())
        nxt = re.compile(r"(?m)^[ \t]*motion[ \t]").search(new, mm.end())
        if not cat or (nxt and cat.start() > nxt.start()) or MARK in new[mm.end():(nxt.start() if nxt else len(new))]:
            continue
        ins = MARK + nl + "".join(f"    collision {v} VOID -1 -1 SEPARATER&GUARD(50) {nums}{nl}" for v, nums in vols)
        new = new[:cat.end()] + ins + new[cat.end():]
        done.append(mv)
    if done:
        new = TAG.format(moves="/".join(done)) + nl + new
    return new, done, [v for v, _ in vols]


report = []
WORK.mkdir(parents=True, exist_ok=True)
for old in WORK.glob("*"):
    old.unlink()
specs = []
for i, (name, key, data) in enumerate(rows):
    low = name.lower()
    if low in MOVES:
        text = data.decode("cp932")
        new, done, vols = guard(text, MOVES[low])
        report.append(dict(file=name, volumes=vols, moves=done))
        print(f"{name}: volumes {vols} guarded {done}")
        if set(done) != set(MOVES[low]):
            raise SystemExit(f"{name}: expected {MOVES[low]}, guarded {done}")
        data = new.encode("cp932")
    p = WORK / f"{i:04d}"
    p.write_bytes(data)
    specs.append(f"{name.replace('/', chr(92))}={p}:{key}")
(WORK / "_specs.txt").write_text("\n".join(specs) + "\n", encoding="utf-8")
OUT.parent.mkdir(parents=True, exist_ok=True)
res = subprocess.run([sys.executable, "-I", str(TOOLS / "gupk_build.py"), DLL, str(OUT), "@" + str(WORK / "_specs.txt")],
                     capture_output=True, text=True)
print(res.stdout.strip() or res.stderr.strip())
if res.returncode:
    raise SystemExit(res.returncode)
# read back: same names in the same order, same keys, every entry byte-identical but the three defs
back = []
for idx, name, key, unpacked, off, size in gs["entries"](str(OUT), TABLE):
    back.append((name, key, gs["extract"](str(OUT), TABLE, key, unpacked, off, size)))
assert [(n, k) for n, k, _ in back] == [(n, k) for n, k, _ in rows], "names/keys differ"
changed = [n for (n, _k, d), (_n2, _k2, d2) in zip(rows, back) if d != d2]
assert sorted(c.lower() for c in changed) == sorted(MOVES), changed
raw = OUT.read_bytes()
assert b"data/" not in raw[:4096], "a forward-slash name got into the table"
sha = hashlib.sha256(raw).hexdigest()
(HERE / "story_hard_guard_report.json").write_text(json.dumps(dict(base=str(BASE), base_sha256=base_sha, pack=str(OUT), sha256=sha,
                                                                     changed=changed, rows=report), ensure_ascii=False, indent=1), encoding="utf-8")
print("pack", OUT, sha[:8], len(raw), "bytes; changed", changed)
