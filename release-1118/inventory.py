"""기신비상 11-18 배포 목록(읽기만): 합본 F:\\AI\\khd-latest-game 의 파일을 「실음 / 원본(안 실음) / 뺌」으로 가르고,
코덱스 10-30 배포본(manifest.json)과 견줘 그 뒤 새로 생기거나 바뀐 것을 표시한다. 크기·압축(lzma preset 1 표본) 가늠.
  python -I -X utf8 inventory.py   -> inventory.json + 요약 출력"""
import hashlib
import json
import lzma
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
G = Path(r"F:\AI\khd-latest-game")
HERE = Path(__file__).resolve().parent
REL1030 = Path(r"P:\AI\Codex\projects\migrated-from-c\onedrive-chatgpt\데몬베인 시리즈 2\delivery\KishinHisho_KO_DUEL_20261030\manifest.json")
m1030 = json.loads(REL1030.read_text(encoding="utf-8-sig"))
shipped1030 = {f["path"].lower(): f["sha256"].lower() for f in m1030["files"]}
required1030 = {f["path"].lower(): f["sha256"].lower() for f in m1030["required"]}

EXCL = [
    (r"\.bak$|\.before|\.orig_|_off$|\.incoming$|\.writing$|\.crash\.dmp$", "옛 판·백업·덤프"),
    (r"^save/", "세이브"),
    (r"^ko-library/profile/|^ko-library/native\.log$", "서재 독서 기록(WebView 프로필)"),
    (r"^autobattle/cap/", "시험 화면 덤프"),
    (r"(^|/)[^/]*(_log|_trace|trace)[^/]*\.(txt|tsv|log)$|\.log$|^autobattle/(play_error|subtitle_status)[^/]*$", "기록(로그)"),
    (r"^_compat_[^/]*\.cmd$", "개발용 실행 cmd"),
    (r"^dgvoodoo\.conf$", "dgVoodoo 설정(dgVoodoo 는 11-03 부터 꺼짐)"),
]


def sha(p):
    d = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            d.update(b)
    return d.hexdigest()


def ratio(p, size):
    with open(p, "rb") as f:
        if size > 24 << 20:
            f.seek(size // 2 - (12 << 20))
            data = f.read(24 << 20)
        else:
            data = f.read()
    return len(lzma.compress(data, preset=1)) / max(1, len(data)) if data else 1.0


rows = []
for p in sorted(G.rglob("*"), key=lambda q: q.as_posix().lower()):
    if not p.is_file():
        continue
    rel = p.relative_to(G).as_posix()
    low = rel.lower()
    size = p.stat().st_size
    why = next((w for rx, w in EXCL if re.search(rx, low)), None)
    if why:
        rows.append(dict(rel=rel, size=size, status="exclude", why=why))
        continue
    h = sha(p)
    if low in required1030:
        st = "base" if h == required1030[low] else "base_changed"
        rows.append(dict(rel=rel, size=size, sha256=h, status=st))
        continue
    since = "same_as_1030" if shipped1030.get(low) == h else ("changed_since_1030" if low in shipped1030 else "new_since_1030")
    rows.append(dict(rel=rel, size=size, sha256=h, status="ship", since=since, ratio=round(ratio(p, size), 3)))

gone = sorted(k for k in shipped1030 if not (G / k).exists())
(HERE / "inventory.json").write_text(json.dumps(dict(game=str(G), rows=rows, gone_since_1030=gone), ensure_ascii=False, indent=0),
                                     encoding="utf-8")
tot = defaultdict(lambda: [0, 0, 0.0])
for r in rows:
    k = r["status"] + (("/" + r["since"]) if r["status"] == "ship" else "")
    tot[k][0] += 1
    tot[k][1] += r["size"]
    tot[k][2] += r["size"] * r.get("ratio", 0)
for k, (n, s, c) in sorted(tot.items()):
    print(f"{k:32} {n:5} files {s / 2**20:9.1f} MB" + (f"  ~{c / 2**20:7.1f} MB packed" if c else ""))
ex = defaultdict(lambda: [0, 0])
for r in rows:
    if r["status"] == "exclude":
        ex[r["why"]][0] += 1
        ex[r["why"]][1] += r["size"]
print("\nexcluded:", {k: f"{n} files {s / 2**20:.1f} MB" for k, (n, s) in ex.items()})
print("base_changed:", [r["rel"] for r in rows if r["status"] == "base_changed"])
print("shipped in 10-30 but not here now:", gone)
print("\nnew since 10-30 (top level and folders):")
grp = defaultdict(lambda: [0, 0])
for r in rows:
    if r.get("since") == "new_since_1030":
        k = r["rel"] if "/" not in r["rel"] else r["rel"].split("/")[0] + "/" + (r["rel"].split("/")[1] if r["rel"].count("/") > 1 else "")
        grp[k][0] += 1
        grp[k][1] += r["size"]
for k, (n, s) in sorted(grp.items()):
    print(f"  {k:48} {n:4} {s / 2**20:8.1f} MB")
