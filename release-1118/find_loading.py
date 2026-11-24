"""어느 묶음에 로딩(커맨드 리스트) 그림이 있나 — 합본 F:\AI\khd-latest-game 의 .pak·.dat 항목 이름에서 load/command 찾기"""
import sys, re
from pathlib import Path
sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
import kishinhisho_pak_fixed as P
TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs = {}
_a, sys.argv = sys.argv, [sys.argv[0], r"F:\AI\khd-latest-game\demonbane.dll"]
exec(compile(src, "gupk_sounds.py", "exec"), gs)
sys.argv = _a
G = Path(sys.argv[1] if len(sys.argv) > 1 else r"F:\AI\khd-latest-game")
TABLE = gs["read_pe_va"](str(Path(r"F:\AI\khd-latest-game") / "demonbane.dll"), gs["LOOKUP_TABLE_VA"], 6 * 256)
pat = re.compile(r"load|command|cmd|コマンド", re.I)
for p in sorted(G.glob("*.pak")):
    try:
        a = P.parse_archive(p)
    except Exception as e:
        print(p.name, "pak?", e); continue
    hits = [e.name for e in a.entries if pat.search(e.name)]
    if hits: print(p.name, len(hits), hits[:12])
for p in sorted(G.glob("dmpack*.dat*")):
    try:
        names = [n for _i, n, *_r in gs["entries"](str(p), TABLE)]
    except Exception as e:
        print(p.name, "gupk?", e); continue
    hits = [n for n in names if pat.search(n)]
    if hits: print(p.name, len(names), len(hits), hits[:12])
