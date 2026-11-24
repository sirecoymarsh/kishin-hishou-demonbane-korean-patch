"""전투 텍스처(DDS) 조사: 이름·크기·형식 — 이펙트로 보이는 것(파티클·재질·마법진·탄·컷인) 위주"""
import sys, struct, re, json
from pathlib import Path
TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs = {}
_a, sys.argv = sys.argv, [sys.argv[0], r"F:\AI\khd-latest-game\demonbane.dll"]
exec(compile(src, "gupk_sounds.py", "exec"), gs)
sys.argv = _a
G = Path(r"F:\AI\khd-latest-game")
TABLE = gs["read_pe_va"](str(G / "demonbane.dll"), gs["LOOKUP_TABLE_VA"], 6 * 256)
eff = {}
for pack in ("dmpack.dat", "dmpack001.dat", "dmpack002.dat", "dmpack003.dat"):
    for idx, name, key, unpacked, off, size in gs["entries"](str(G / pack), TABLE):
        if name.lower().endswith(".dds"):
            eff[name.lower()] = (name, pack, key, unpacked, off, size)
rows = []
for low, (name, pack, key, unpacked, off, size) in sorted(eff.items()):
    try:
        d = gs["extract"](str(G / pack), TABLE, key, unpacked, off, size)
    except Exception:
        continue
    if d[:4] != b"DDS ":
        continue
    h, w = struct.unpack_from("<II", d, 12)
    mips = struct.unpack_from("<I", d, 28)[0]
    fourcc = d[84:88].decode("ascii", "replace")
    rows.append(dict(name=name, pack=pack, w=w, h=h, mips=mips, fmt=fourcc, bytes=len(d)))
Path("dds_survey.json").write_text(json.dumps(rows, ensure_ascii=False, indent=0), encoding="utf-8")
print(len(rows), "dds")
