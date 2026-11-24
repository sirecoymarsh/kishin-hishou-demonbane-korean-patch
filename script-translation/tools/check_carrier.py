"""Check every character of the current Korean against the existing build's
CP932 Han carrier map and font (P:\\AI\\khd-retl), read-only.

- Hangul syllables must be in the carrier map and have a non-empty glyph.
- Other characters must be encodable in CP932 and must not be a carrier
  kanji (it would render as the Hangul glyph that took its slot).
Writes work/carrier_report.tsv.
"""
import collections
import json
import sys
from pathlib import Path

from fontTools.pens.boundsPen import BoundsPen
from fontTools.ttLib import TTFont

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from load import packets, segments

RETL = Path(r"P:\AI\khd-retl")
OUT = Path(r"P:\AI\khd-argon-review\work\carrier_report.tsv")

d = json.loads((RETL / "cp932_han_carrier_map_ext.json").read_text(encoding="utf-8"))
rows = d["mappings"] if isinstance(d, dict) else d
CAR = {r["target"]: r["carrier"] for r in rows}
CARRIER_SET = set(CAR.values())
BLOCKED = set((RETL / "blocked.txt").read_text(encoding="utf-8").split())

font = TTFont(RETL / "KHDKorean-CP932Han-Ext.otf")
cmap = font.getBestCmap()
gs = font.getGlyphSet()


def has_glyph(cp):
    g = cmap.get(cp)
    if g is None:
        return False
    pen = BoundsPen(gs)
    gs[g].draw(pen)
    return pen.bounds is not None


def main():
    where = collections.defaultdict(list)
    for pid in packets():
        for s in segments(pid):
            for c in set(s["ko"] or ""):
                where[c].append((s["sid"], s["reviewed"], s["ko"]))
    problems = []
    for c, occ in sorted(where.items(), key=lambda x: -len(x[1])):
        if "가" <= c <= "힣":
            if c not in CAR:
                problems.append(("hangul_not_in_map" + ("(blocked)" if c in BLOCKED else ""), c, occ))
            elif not has_glyph(ord(CAR[c])):
                problems.append(("hangul_blank_glyph", c, occ))
        elif c in "\n\t":
            continue
        else:
            try:
                c.encode("cp932")
            except UnicodeEncodeError:
                problems.append(("not_cp932", c, occ))
                continue
            if c in CARRIER_SET:
                problems.append(("kanji_is_carrier", c, occ))
            elif "\u4e00" <= c <= "\u9fff":
                problems.append(("kanji_noncarrier", c, occ))
            elif "\u3130" <= c <= "\u318f":
                problems.append(("compat_jamo", c, occ))
    with OUT.open("w", encoding="utf-8") as f:
        f.write("kind\tchar\tcount\tids\texample\n")
        for kind, c, occ in problems:
            f.write(f"{kind}\t{c}\t{len(occ)}\t{','.join(o[0] for o in occ[:8])}\t{occ[0][2][:80].replace(chr(10), '⏎')}\n")
    summary = collections.Counter(k for k, _, _ in problems)
    print("map size", len(CAR), "blocked", "".join(sorted(BLOCKED)))
    print(summary)
    for kind, c, occ in problems:
        if kind.startswith("kanji") and "da0506:T00001" in occ[0][0]:
            continue
        print(kind, repr(c), len(occ), [o[0] for o in occ[:5]], occ[0][2][:70].replace("\n", "⏎"))


if __name__ == "__main__":
    main()
