"""각 dmpack 의 data/menu/<이름> 을 뽑아 해시 비교 + PNG 로 저장"""
import sys, hashlib
from pathlib import Path
TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs = {}
_a, sys.argv = sys.argv, [sys.argv[0], r"F:\AI\khd-latest-game\demonbane.dll"]
exec(compile(src, "gupk_sounds.py", "exec"), gs)
sys.argv = _a
TABLE = gs["read_pe_va"](r"F:\AI\khd-latest-game\demonbane.dll", gs["LOOKUP_TABLE_VA"], 6 * 256)
G = Path(sys.argv[1]); want = sys.argv[2].lower(); out = Path(sys.argv[3]); out.mkdir(parents=True, exist_ok=True)
for p in sorted(G.glob("dmpack*.dat")):
    for idx, name, key, unpacked, off, size in gs["entries"](str(p), TABLE):
        if name.lower() == want:
            try:
                d = gs["extract"](str(p), TABLE, key, unpacked, off, size)
            except Exception as e:
                print(p.name, "extract failed", e); continue
            hh = hashlib.sha256(d).hexdigest()[:8]
            (out / f"{p.stem}__{Path(name).name}").write_bytes(d)
            ispng = d[:4] == bytes([0x89, 0x50, 0x4E, 0x47])
            print(f"{p.name:28} idx {idx:3} {len(d):7} {hh} png={ispng}")
