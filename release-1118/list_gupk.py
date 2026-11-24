import sys
from pathlib import Path
TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs = {}
_a, sys.argv = sys.argv, [sys.argv[0], r"F:\AI\khd-latest-game\demonbane.dll"]
exec(compile(src, "gupk_sounds.py", "exec"), gs)
sys.argv = _a
TABLE = gs["read_pe_va"](r"F:\AI\khd-latest-game\demonbane.dll", gs["LOOKUP_TABLE_VA"], 6 * 256)
pat = sys.argv[2] if len(sys.argv) > 2 else ""
for idx, name, key, unpacked, off, size in gs["entries"](sys.argv[1], TABLE):
    if pat.lower() in name.lower(): print(idx, name, unpacked)
