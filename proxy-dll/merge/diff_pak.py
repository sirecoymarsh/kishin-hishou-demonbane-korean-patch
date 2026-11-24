"""PAK 항목 단위 3갈래 대조(읽기만): 바탕 대비 각 판에서 바뀐·새·빠진 항목, 둘 다 바꾼 항목"""
import hashlib, sys
from pathlib import Path
sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
import kishinhisho_pak_fixed as P
def load(path):
    a = P.parse_archive(Path(path))
    return {e.name: hashlib.sha256(P.decode_stored(e, P.read_stored(a, e))).hexdigest()[:12] for e in a.entries}
base, *others = sys.argv[1:]
B = load(base)
V = {p: load(p) for p in others}
for p, d in V.items():
    ch = [n for n in d if n in B and d[n] != B[n]]
    new = [n for n in d if n not in B]
    gone = [n for n in B if n not in d]
    print(f"== {p}: {len(d)} entries; changed {len(ch)}, new {len(new)}, gone {len(gone)}")
    for n in ch[:40]: print("   ~", n)
    if len(ch) > 40: print("   ...")
    for n in new[:40]: print("   +", n)
    if len(new) > 40: print("   ... +", len(new) - 40)
    for n in gone: print("   -", n)
if len(others) >= 2:
    a, b = V[others[0]], V[others[1]]
    both = sorted(n for n in set(a) & set(b) if a[n] != B.get(n) and b[n] != B.get(n) and a[n] != b[n])
    print("== changed differently in both:", both)
