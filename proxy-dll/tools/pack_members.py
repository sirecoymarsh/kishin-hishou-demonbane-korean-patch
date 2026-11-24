"""후보 전용 묶음의 항목을 나열하고, 설치된 묶음과 이름이 겹치는지 본다(읽기만).

python -I -X utf8 pack_members.py <게임 폴더> <후보 묶음>...
"""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
_src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
game = Path(sys.argv[1])
cands = [Path(p) for p in sys.argv[2:]]
gs: dict = {}
_argv, sys.argv = sys.argv, [sys.argv[0], str(game / "demonbane.dll")]
exec(compile(_src, "gupk_sounds.py", "exec"), gs)
sys.argv = _argv
TABLE = gs["read_pe_va"](str(game / "demonbane.dll"), gs["LOOKUP_TABLE_VA"], 6 * 256)


def members(pack: Path) -> dict:
    out = {}
    for idx, name, key, unpacked, off, size in gs["entries"](str(pack), TABLE):
        data = gs["extract"](str(pack), TABLE, key, unpacked, off, size)
        out[name.replace("\\", "/").lower()] = hashlib.sha256(data).hexdigest()[:16]
    return out


installed = {}
for pack in sorted(game.glob("dmpack*.dat")):
    for idx, name, key, unpacked, off, size in gs["entries"](str(pack), TABLE):
        installed.setdefault(name.replace("\\", "/").lower(), []).append(pack.name)

seen: dict = {}
for c in cands:
    m = members(c)
    print(f"=== {c.name}  ({len(m)} 항목)")
    for n, h in sorted(m.items()):
        clash = installed.get(n, [])
        other = [o for o in seen.get(n, []) if o != c.name]
        mark = ""
        if clash:
            mark += f"  ★설치본과 겹침 {clash}"
        if other:
            mark += f"  ★다른 후보와 겹침 {other}"
        print(f"  {n}  {h}{mark}")
        seen.setdefault(n, []).append(c.name)
