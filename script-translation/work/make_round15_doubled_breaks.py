"""Round 15 (user 11-01 「여기서 고쳐」): line breaks the corrected text writes twice. The source keeps the break and
indent between two cells in the raw gap (outside every cell, e.g. "<k>\\r　"); where a cell repeats it at its edge,
the game shows an empty line and a doubled indent. Same rule as gemini_translate/tools/check_tl.py: strip a trailing
(leading) newline run only when the cell's JP has none there and the raw gap on that side already holds a break
(for a ruby base, the gap before its reading). Cells that would be left blank are listed, not edited.

  python -I -X utf8 work/make_round15_doubled_breaks.py          # writes work/edits_round15.json
"""
import json
import re
from pathlib import Path

PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
OUT = Path(r"P:\AI\khd-argon-review\out\responses")
DST = Path(r"P:\AI\khd-argon-review\work\edits_round15.json")
TAIL = re.compile(r"\n[\n\u3000 ]*$")
HEAD = re.compile(r"^\n[\n\u3000 ]*")
REASON = ("원문이 칸 밖(틈)에 둔 줄바꿈·들여쓰기를 칸 안에도 써서 게임에서 빈 줄·들여쓰기가 두 겹 — 칸 쪽을 뺌"
          "(사용자 11-01 「여기서 고쳐」)")
raw_cache = {}


def raw(fname):
    if fname not in raw_cache:
        c = list((PKG / "source_jp_A_cp932").glob(fname + "*"))
        raw_cache[fname] = c[0].read_bytes() if c else b""
    return raw_cache[fname]


def has_break(gap):
    return b"\r" in gap or b"\n" in gap


def unique_fragment(ko, start, end):
    """Smallest slice around [start, end) that occurs once in ko."""
    for pad in range(0, len(ko) + 1):
        a, b = max(0, start - pad), min(len(ko), end + pad)
        if ko.count(ko[a:b]) == 1:
            return a, b
    return 0, len(ko)


edits, hand = [], []
for f in sorted(OUT.glob("P*.json")):
    pkt = json.loads((PKG / "packets" / f.name).read_text(encoding="utf-8"))
    ko = {r["id"]: r.get("ko") for r in json.loads(f.read_text(encoding="utf-8"))["records"]}
    for u in pkt["units"]:
        segs, rb = u["segments"], raw(u["file"])
        for i, s in enumerate(segs):
            k = ko.get(s["id"])
            if not isinstance(k, str):
                continue
            nxt = rb[s["source_byte_end"]:segs[i + 1]["source_byte_start"]] if i + 1 < len(segs) else b""
            if i > 1 and segs[i - 1]["kind"] == "ruby_reading":
                prv = rb[segs[i - 2]["source_byte_end"]:segs[i - 1]["source_byte_start"]]
            else:
                prv = rb[segs[i - 1]["source_byte_end"]:s["source_byte_start"]] if i > 0 else b""
            new = k
            m = TAIL.search(new)
            if m and not s["jp"].endswith("\n") and has_break(nxt):
                new = new[:m.start()]
            m = HEAD.match(new)
            if m and new.startswith("\n") and not s["jp"].startswith("\n") and has_break(prv):
                new = new[m.end():]
            if new == k:
                continue
            if not new.strip():
                hand.append((s["id"], s["jp"], k))
                continue
            # one fragment covering both edges if both changed, else the changed edge
            lead = len(k) - len(k.lstrip("\n\u3000 ")) if k[:1] == "\n" and not new[:1] == "\n" else 0
            cut_head = len(k) - len(k[lead:]) if lead else 0
            body_end = len(k) - (len(k) - len(k.rstrip("\n\u3000 "))) if TAIL.search(k) and not TAIL.search(new) else len(k)
            a, b = unique_fragment(k, 0 if cut_head else body_end, len(k) if body_end < len(k) else cut_head)
            old_frag = k[a:b]
            new_frag = old_frag
            if cut_head and a == 0:
                new_frag = new_frag[cut_head:]
            if body_end < len(k) and b == len(k):
                new_frag = new_frag[:len(new_frag) - (len(k) - body_end)]
            if k.replace(old_frag, new_frag) != new:  # fall back to the whole cell
                old_frag, new_frag = k, new
            edits.append({"sid": s["id"], "old": old_frag, "new": new_frag, "cat": "줄바꿈", "reason": REASON})
DST.write_text(json.dumps(edits, ensure_ascii=False, indent=1), encoding="utf-8")
print("고침", len(edits), "칸 →", DST)
print("손으로 볼 것(비게 되는 칸):", len(hand))
for h in hand:
    print("  ", h)
