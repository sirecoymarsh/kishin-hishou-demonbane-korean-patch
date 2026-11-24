"""후보 전용 장면·정의가 원래 장면·정의와 어디가 다른지 본다(읽기만).

python -I -X utf8 scene_diff.py <게임 폴더> <후보 묶음> <원래 장면 번호> <전용 장면 번호>
"""
from __future__ import annotations

import difflib
import sys
from pathlib import Path

TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
_src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
game, cand, src, dst = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
gs: dict = {}
_argv, sys.argv = sys.argv, [sys.argv[0], str(game / "demonbane.dll")]
exec(compile(_src, "gupk_sounds.py", "exec"), gs)
sys.argv = _argv
TABLE = gs["read_pe_va"](str(game / "demonbane.dll"), gs["LOOKUP_TABLE_VA"], 6 * 256)


def table(packs):
    eff = {}
    for p in packs:
        for idx, name, key, unpacked, off, size in gs["entries"](str(p), TABLE):
            eff[name.replace("\\", "/").lower()] = (str(p), key, unpacked, off, size)
    return eff


def read(eff, name):
    p, key, unpacked, off, size = eff[name]
    return gs["extract"](p, TABLE, key, unpacked, off, size)


# 게임처럼 이름 순으로 읽고 뒤 묶음이 이긴다. 후보 묶음은 맨 뒤.
installed = table(sorted(game.glob("dmpack*.dat")))
mine = table([cand])
for ext in ("cmn", "scn", "se"):
    a = f"data/scene/scene{src:03d}/scene{src:03d}.{ext}" if src < 1000 else f"data/scene/scene{src}/scene{src}.{ext}"
    b = f"data/scene/scene{dst}/scene{dst}.{ext}"
    if a not in installed:
        print(f"--- {a}: 설치본에 없음")
        continue
    if b not in mine:
        print(f"--- {b}: 후보에 없음 (원래 {a} 는 있음)")
        continue
    ta = read(installed, a).decode("cp932", "replace").splitlines()
    tb = read(mine, b).decode("cp932", "replace").splitlines()
    d = list(difflib.unified_diff(ta, tb, a, b, n=0, lineterm=""))
    print(f"--- {ext}: 다른 줄 {sum(1 for x in d if x[:1] in '+-' and x[:3] not in ('---', '+++'))}")
    for x in d[:60]:
        print("   ", x)
