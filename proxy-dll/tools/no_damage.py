"""판마다 피해 없는 구간 — run_duel_test 의 F*_battle.log(체력이 바뀔 때마다 한 줄)에서 읽는다(읽기만).

python -I -X utf8 no_damage.py <logs\duel_* 폴더>...
"""
from __future__ import annotations

import collections
import re
import sys
from pathlib import Path

ROW = re.compile(r"^\s*([\d.]+)s (\S*) \| (.*?) \| d=(-?\d+) dy=(-?\d+)")
SIDE = re.compile(r"g(\d) (\S+) (\S+)@(-?\d+) hp(-?\d+) mp(\S+)")


def read(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = ROW.match(line)
        if not m:
            continue
        sides = {int(s[0]): s for s in SIDE.findall(m[3])}
        if 0 not in sides or 1 not in sides:
            continue
        rows.append(dict(t=float(m[1]), hp0=int(sides[0][4]), hp1=int(sides[1][4]),
                         mo0=sides[0][2], mo1=sides[1][2], d=int(m[4])))
    return rows


def longest(rows, key):
    """key(row) 가 같은 채로 이어진 가장 긴 구간 (시작, 끝)"""
    best, start = (0.0, 0.0, 0.0), rows[0]["t"]
    for a, b in zip(rows, rows[1:]):
        if key(b) != key(a):
            if b["t"] - start > best[0]:
                best = (b["t"] - start, start, b["t"])
            start = b["t"]
    if rows[-1]["t"] - start > best[0]:
        best = (rows[-1]["t"] - start, start, rows[-1]["t"])
    return best


for folder in sys.argv[1:]:
    for f in sorted(Path(folder).glob("F*_battle.log")):
        rows = read(f)
        if len(rows) < 2:
            continue
        t0 = rows[0]["t"]
        both = longest(rows, lambda r: (r["hp0"], r["hp1"]))
        p1 = longest(rows, lambda r: r["hp1"])      # 1P(주인공)가 상대에게 못 준 구간
        p2 = longest(rows, lambda r: r["hp0"])      # 2P 가 주인공에게 못 준 구간
        last = rows[-1]
        ko = "KO" if min(last["hp0"], last["hp1"]) <= 0 else "승부 안 남"
        win = [r for r in rows if both[1] <= r["t"] < both[2]]
        top0 = collections.Counter(r["mo0"] for r in win).most_common(3)
        top1 = collections.Counter(r["mo1"] for r in win).most_common(3)
        dist = sorted(r["d"] for r in win if r["d"] >= 0)
        mid = dist[len(dist) // 2] if dist else -1
        print(f"{Path(folder).name}/{f.name}: {last['t'] - t0:5.0f}초 기록, 끝 체력 {last['hp0']} 대 {last['hp1']} ({ko})")
        print(f"   양쪽 무피해 최장 {both[0]:5.1f}초 ({both[1] - t0:.0f}~{both[2] - t0:.0f}초) 주인공 동작 {top0} / 상대 {top1} / 거리 중앙값 {mid}")
        print(f"   주인공이 못 때린 최장 {p1[0]:5.1f}초 · 상대가 못 때린 최장 {p2[0]:5.1f}초")
