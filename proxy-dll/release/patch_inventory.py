"""기신비상 합본 → 배포 패치 가늠(읽기만): 원본 일본판 1.00 기록(_KHD_BASE_STAGING.json)과 대조해
원본 그대로 / 바뀜 / 새 파일, 빼야 할 것(백업·기록·세이브)을 가르고 크기를 더한다. 결과 JSON 도 남김."""
import hashlib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
G = Path(r"F:\AI\khd-latest-game")
BASE = json.loads(Path(r"P:\GAME\KishinHisho\_KHD_BASE_STAGING.json").read_text(encoding="utf-8-sig"))
OUT = Path(r"Y:\AI\claude-work\khd-session-57a04242\patch_inventory.json")
orig = {f["name"].lower(): (f["size"], f["sha256"].lower()) for f in BASE["files"]}
EXCL = re.compile(r"(\.bak$|\.before|\.orig_|_off$|\.dmp$|_log\.txt$|^save/|^autobattle/cap/|"
                  r"^autobattle/(proxy_log|subtitle_log|adv_log|play_log|play_error|khd4k_trace|subtitle_status)[^/]*$|"
                  r"^autobattle/.*\.(log|bak)$|^autobattle/.*(_old|backup|before)[^/]*/)", re.I)


def sha(p):
    d = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            d.update(b)
    return d.hexdigest()


rows = []
for p in sorted(G.rglob("*")):
    if not p.is_file():
        continue
    rel = p.relative_to(G).as_posix()
    size = p.stat().st_size
    if EXCL.search(rel):
        rows.append(dict(rel=rel, size=size, status="exclude"))
        continue
    o = orig.get(rel.lower())
    if o:
        s = sha(p)
        st = "original" if (size, s) == o else "modified"
    else:
        st = "new"
    rows.append(dict(rel=rel, size=size, status=st))
OUT.write_text(json.dumps(dict(base=BASE.get("source_dir"), rows=rows), ensure_ascii=False, indent=0), encoding="utf-8")
tot = defaultdict(lambda: [0, 0])
for r in rows:
    tot[r["status"]][0] += 1
    tot[r["status"]][1] += r["size"]
print("by status:", {k: f"{n} files {s / 2**20:.1f} MB" for k, (n, s) in tot.items()})
print("original files listed in the 1.00 record but missing here:",
      sorted(n for n in orig if not (G / n).exists()))
print("\n== top level, not excluded")
for r in rows:
    if "/" not in r["rel"] and r["status"] != "exclude":
        o = orig.get(r["rel"].lower())
        print(f"  {r['status']:9} {r['size'] / 2**20:8.1f} MB  {r['rel']}" + (f"   (1.00: {o[0] / 2**20:.1f} MB)" if o and r['status'] == 'modified' else ""))
sub = defaultdict(lambda: [0, 0])
for r in rows:
    if r["rel"].startswith("autobattle/") and r["status"] != "exclude":
        parts = r["rel"].split("/")
        key = "/".join(parts[:2]) if len(parts) > 2 else "autobattle/(files)"
        sub[key][0] += 1
        sub[key][1] += r["size"]
print("\n== autobattle (not excluded), by folder")
for k, (n, s) in sorted(sub.items(), key=lambda x: -x[1][1])[:20]:
    print(f"  {s / 2**20:8.1f} MB  {n:4} files  {k}")
big = sorted((r for r in rows if r["rel"].startswith("autobattle/") and r["status"] != "exclude"), key=lambda r: -r["size"])[:10]
print("  biggest:", [(r["rel"], round(r["size"] / 2**20, 1)) for r in big])
ex = sorted((r for r in rows if r["status"] == "exclude"), key=lambda r: -r["size"])
print(f"\n== excluded {len(ex)} files {sum(r['size'] for r in ex) / 2**20:.1f} MB; biggest:",
      [(r["rel"], round(r["size"] / 2**20, 1)) for r in ex[:8]])
