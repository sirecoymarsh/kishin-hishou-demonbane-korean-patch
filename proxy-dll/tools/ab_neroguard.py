"""네로 막기 풀기 A/B 판정(11-02): ab_runs.txt 의 판마다 F0n_battle.log 에서 네로(g1) 체력 감소·맞은 횟수를 센다.

  python -I -X utf8 ab_neroguard.py
"""
import re
from pathlib import Path

RUNS = Path(r"P:\AI\khd-hero-ai\neroguard\ab_runs.txt")
LINE = re.compile(r"^\s*([\d.]+)s .*?\|\| g1 (\S+) (\S+)@\d+ hp(\d+)")
rows = []
for ln in RUNS.read_text(encoding="utf-8").splitlines():
    m = re.match(r"^(A\d|B\d) log=(.+)$", ln.strip())
    if not m:
        continue
    step, folder = m[1], Path(m[2].replace("/p/", "P:/"))
    for f in sorted(folder.glob("F0*_battle.log")):
        hp, hits, first, last_t = None, 0, None, 0.0
        for l in f.read_text(encoding="utf-8", errors="replace").splitlines():
            mm = LINE.match(l)
            if not mm:
                continue
            t, h = float(mm[1]), int(mm[4])
            if first is None:
                first = h
            if hp is not None and h < hp:
                hits += 1
            hp, last_t = h, t
        if first is not None:
            rows.append((f.name[:3], step, first, hp, first - hp, hits, last_t))
pairs = {"F01": "AM:QM 데몬베인", "F02": "GM:QM D 블러드", "F03": "CM:QM 파괴로보", "F04": "BM:QM 리베르 레기스",
         "F05": "HM:QM 앰브로시우스"}
print("판  짝               단계  처음  끝   깎음  맞은 수  시간")
for fid, step, a, b, d, n, t in sorted(rows):
    print(f"{fid} {pairs.get(fid, fid):16s} {step:4s} {a:5d} {b:5d} {d:5d} {n:6d}  {t:5.0f}s")
for fid in sorted({r[0] for r in rows}):
    for k in "AB":
        ds = [r[4] for r in rows if r[0] == fid and r[1][0] == k]
        if ds:
            print(f"{fid} {pairs.get(fid, fid)} {'고침 없음' if k == 'A' else '고침 있음'}: 평균 깎음 {sum(ds) / len(ds):.0f} ({ds})")
