"""기체 모델의 「보이는 부품」 계층(읽기만) — 정의 머리의 parts … on 줄 + .dmo 마디 부모.

python -I -X utf8 model_parts.py <정의 이름(data/character/XX/YY)> [<dmo 이름>]...
    정의 이름만 주면 같은 폴더의 같은 이름 .dmo 를 쓴다(예: data/character/BM/BM011 → BM011.dmo 없으면 BM.dmo).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-runtime-fix\work\command_list\render")
sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import dm_lib as D  # noqa: E402
import dmo_mesh as M  # noqa: E402

GAME = Path(r"P:\AI\khd-runtime-fix\game")
D._EFF = D.effective(tuple(p.name for p in sorted(GAME.glob("dmpack*.dat"))), GAME)


def visible_parts(def_text: str) -> list[str]:
    head = def_text.split("\nmotion", 1)[0]
    return [m[1] for m in re.finditer(r"(?m)^\s*parts\s+(\S+)\s+0\s+on\b", head)]


def tree(def_name: str, dmo_name: str | None = None):
    t = D.read(def_name + ".def").decode("cp932", "replace")
    vis = visible_parts(t)
    folder = def_name.rsplit("/", 1)[0]
    cands = [dmo_name] if dmo_name else [def_name, folder + "/" + folder.rsplit("/", 1)[1]]
    for c in cands:
        try:
            nodes = M.walk(D.read(c + ".dmo"))
            break
        except KeyError:
            continue
    else:
        raise SystemExit(f"{def_name}: dmo 를 못 찾음 {cands}")
    by = {n["name"]: n for n in nodes}
    vset = set(vis)

    def vis_parent(name):
        p = by[name]["par"]
        while p >= 0:
            nm = nodes[p]["name"]
            if nm in vset:
                return nm
            p = nodes[p]["par"]
        return None

    rows = []
    for v in vis:
        if v not in by:
            rows.append((v, "(모델에 없음)", 0, 0))
            continue
        tri = len(re.findall(r"\n", by[v]["x"])) if by[v]["x"] else 0
        rows.append((v, vis_parent(v), tri, by[v]["i"]))
    return c, rows


for a in sys.argv[1:]:
    def_name, _, dmo = a.partition(":")
    c, rows = tree(def_name, dmo or None)
    print(f"=== {def_name}  (dmo {c})  보이는 부품 {len(rows)}")
    kids = {}
    for v, par, size, i in rows:
        kids.setdefault(par, []).append((v, size))
    def show(par, depth):
        for v, size in kids.get(par, []):
            print("   " + "  " * depth + f"{v}  ({size} 줄)")
            show(v, depth + 1)
    show(None, 0)
    missing = [v for v, par, s, i in rows if par == "(모델에 없음)"]
    if missing:
        print("   모델에 없는 parts:", missing)
