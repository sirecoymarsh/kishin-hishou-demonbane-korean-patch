"""네로전 데몬베인 A/B 판정(11-03): 판마다 데몬베인(g0) 동작별로 네로(g1) 체력이 깎인 양을 센다.
깎인 순간의 우리 동작이 공격 동작이면 그 동작에, 아니면 1.5초 안의 마지막 공격 동작에 붙인다(탄이 날아가는 시간).

  python -I -X utf8 ab_nerodb.py <ab_runs.txt>
"""
import collections
import re
import sys
from pathlib import Path

ATTACKS = ("SINJUU", "CTHUGUA", "ITACUA", "GUN", "AS", "COMBO", "KICK", "KEN", "THROW")
LINE = re.compile(r"^\s*([\d.]+)s .*?\| g0 (\S+) (\S+)@\d+ hp(\d+) .*?\|\| g1 (\S+) (\S+)@\d+ hp(\d+)")
runs = Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
for ln in runs:
    m = re.match(r"^(A\d|B\d) log=(.+)$", ln.strip())
    if not m:
        continue
    step, folder = m[1], Path(m[2].replace("/p/", "P:/"))
    for f in sorted(folder.glob("F0*_battle.log")):
        per, hp, last_atk, start = collections.Counter(), None, (None, -9.0), None
        for l in f.read_text(encoding="utf-8", errors="replace").splitlines():
            mm = LINE.match(l)
            if not mm:
                continue
            t, ours, nhp = float(mm[1]), mm[3], int(mm[7])
            if any(ours.startswith(a) for a in ATTACKS):
                last_atk = (ours, t)
            if start is None:
                start = nhp
            if hp is not None and nhp < hp:
                who = ours if any(ours.startswith(a) for a in ATTACKS) else (last_atk[0] if t - last_atk[1] <= 1.5 else "기타")
                per[who] += hp - nhp
            hp = nhp
        if start is not None:
            total = start - hp
            print(f"{step} {f.name[:3]} 네로 {start}→{hp} (깎음 {total}) | " + ", ".join(f"{k} {v}" for k, v in per.most_common()))
