"""dmpack.dat·dmpack001.dat: 원본과 항목 단위로 무엇이 바뀌었나(읽기만) — 바뀐 항목만 덧씌우기 묶음에 담으면 얼마인가"""
import hashlib
import lzma
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import dm_lib as D  # noqa: E402

sys.stdout.reconfigure(encoding="utf-8")
G = Path(r"F:\AI\khd-latest-game")


FAILS = Counter()


def entries(p):
    """name -> raw stored bytes, and a function to unpack on demand (some entries do not unpack with this reader)"""
    out = {}
    with open(p, "rb") as f:
        for _i, name, key, unpacked, off, size in D.gs["entries"](str(p), D.TABLE):
            f.seek(off)
            out[name.replace("\\", "/").lower()] = (f.read(size), (str(p), key, unpacked, off, size))
    return out


def content(meta):
    try:
        return D.gs["extract"](meta[0], D.TABLE, *meta[1:])
    except Exception:
        FAILS[meta[0]] += 1
        return None


for cur, orig in (("dmpack.dat", "dmpack.dat.orig_FDD3D1A8"), ("dmpack001.dat", "dmpack001.dat.orig_0B479A4D")):
    a, b = entries(G / cur), entries(G / orig)
    rawdiff = [k for k in a if k in b and a[k][0] != b[k][0]]
    changed = []
    for k in rawdiff:
        x, y = content(a[k][1]), content(b[k][1])
        if x is None or y is None or x != y:
            changed.append(k)
    added = [k for k in a if k not in b]
    gone = [k for k in b if k not in a]
    print(f"   raw bytes differ in {len(rawdiff)} entries; same content after unpacking: {len(rawdiff) - len(changed)}; unpack failures {dict(FAILS)}")
    payload = b"".join(a[k][0] for k in changed + added)
    kinds = Counter(k.rsplit(".", 1)[-1] for k in changed + added)
    comp = len(lzma.compress(payload[:64 << 20], preset=1)) / max(1, len(payload[:64 << 20]))
    print(f"{cur}: {len(a)} entries; changed {len(changed)}, added {len(added)}, gone {len(gone)}; "
          f"changed+added {len(payload) / 2**20:.1f} MB unpacked (lzma ~{comp:.2f}); kinds {kinds.most_common(8)}")
    print("   e.g.", (changed + added)[:8])
