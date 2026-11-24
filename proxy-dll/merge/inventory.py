"""기신비상 사본들 파일 대조(읽기만) — 놀이 사본을 바탕으로 각 사본에서 바뀐·새·빠진 파일. 11-03 「모든 변경사항을 최신화한 사본」"""
import hashlib, json, os, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
COPIES = {"base": r"P:\AI\khd-runtime-fix\game_play", "argon": r"F:\AI\khd-argon-game", "v12": r"F:\AI\khd-v12-game",
          "test": r"P:\AI\khd-runtime-fix\game", "gpt": r"P:\AI\Codex\projects\khd-hard-v11-20261102\runtime\game"}
OUT = Path(r"P:\AI\khd-hero-ai\merge\inventory.json")
def h(p):
    d = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            d.update(b)
    return d.hexdigest()[:16]
def walk(root):
    out = {}
    for dp, dn, fn in os.walk(root):
        for n in fn:
            p = os.path.join(dp, n)
            out[os.path.relpath(p, root).replace("\\", "/")] = p
    return out
res = {}
for k, root in COPIES.items():
    files = walk(root)
    with ThreadPoolExecutor(6) as ex:
        hashes = dict(zip(files, ex.map(h, files.values())))
    res[k] = hashes
    print(k, len(hashes), "files", flush=True)
OUT.write_text(json.dumps(res, indent=0), encoding="utf-8")
