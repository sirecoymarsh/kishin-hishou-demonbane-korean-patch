"""Merge the translationese proposals (work/tl/out*.json), flag rule breaks, print diffs for review.

  python -I -X utf8 review_tl.py            # summary + flagged edits
  python -I -X utf8 review_tl.py all        # every edit, compact
"""
import collections
import json
import re
import sys
from pathlib import Path

TL = Path(r"P:\AI\khd-argon-review\work\tl")
parts = {}
for p in sorted(TL.glob("part*.json")):
    for c in json.loads(p.read_text(encoding="utf-8")):
        parts[c["sid"]] = c
edits = []
for f in sorted(TL.glob("out*.json")):
    d = json.loads(f.read_text(encoding="utf-8-sig"))
    for e in d["edits"] if isinstance(d, dict) else d:
        e["_agent"] = f.stem
        edits.append(e)


def flags(e):
    c = parts.get(e["sid"])
    out = []
    if c is None:
        return ["후보 목록에 없는 칸"]
    old, new, ko = e.get("old", ""), e.get("new", ""), c["cell_ko"]
    for ch in ("\n", "\u3000", "「", "」", "『", "』", "…", "―", "!", "?", "〈", "〉"):
        if old.count(ch) != new.count(ch):
            out.append(f"부호/줄바꿈 수 바뀜({repr(ch)})")
    if re.search(r"[\u4e00-\u9fff]", new):
        out.append("한자")
    if c["speaker"] != "(지문)" and "그녀" in new and "그녀" not in old:
        out.append("대사에 그녀")
    if c["ruby_neighbor"]:
        i = ko.find(old)
        if i == 0 or i + len(old) == len(ko):
            out.append("루비 옆 칸의 끝을 건드림")
    if len(new) > len(old) * 1.6 + 6:
        out.append("크게 늘어남")
    return out


mode = sys.argv[1] if len(sys.argv) > 1 else "flagged"
by_agent = collections.Counter(e["_agent"] for e in edits)
print("edits:", len(edits), dict(by_agent), " cells:", len({e["sid"] for e in edits}))
for e in edits:
    fl = flags(e)
    if mode == "all" or fl:
        c = parts.get(e["sid"], {})
        print(f"[{e['_agent']}] {e['sid']} {c.get('speaker', '')} {' / '.join(fl)}")
        print(f"    JP : {c.get('cell_jp', '')[:90]!r}")
        print(f"    old: {e['old']!r}")
        print(f"    new: {e['new']!r}")
