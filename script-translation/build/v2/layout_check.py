"""Engine-layout check of built script.pak files (11-05).

Simulates system.dll's text layout (0x5b770): per-character GetTextExtentPoint32A widths of the message font
(KHD Korean, em 24 px), a character fits only while x + w < box width, and a message window holds 3 lines
(box 5 measured in the 11-01 review; box 0 is shorter).  A message = text lines up to the next empty line.

  python -I -X utf8 layout_check.py <script.pak> [<script.pak> ...]
Prints, per pak: messages over 3 lines in the dialog boxes (with and without a voice), staged lines that overflow
the screen, and lines the engine folds by itself.
"""
from __future__ import annotations

import importlib.util
import json
import re
import sys
import unicodedata
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
spec = importlib.util.spec_from_file_location("knew", r"P:\AI\khd-runtime-fix\tools\kishinhisho_pak_fixed.py")
K = importlib.util.module_from_spec(spec)
sys.modules["knew"] = K
spec.loader.exec_module(K)

from fontTools.ttLib import TTFont  # noqa: E402

FONT = TTFont(r"F:\AI\khd-latest-game\KHDKorean-CP932Han-Ext.otf")
CMAP, HMTX, UPM = FONT.getBestCmap(), FONT["hmtx"], FONT["head"].unitsPerEm
MAP = json.loads(Path(r"P:\AI\khd-retl\cp932_han_carrier_map_ext.json").read_text(encoding="utf-8"))["mappings"]
DEC = {x["carrier"]: x["target"] for x in MAP}
TAGRE = re.compile(r'<(?:[^>"]|"[^"]*")*>')
RB = re.compile(r'<r text="([^"]*)">(.*?)</r>', re.I)
BOXW = {0: 700, 5: 460, 6: 460, 7: 460, 8: 460, 9: 690, 10: 460, 12: 800, 13: 800}
DIALOG = {0, 5, 6, 7, 8, 9, 10}


def px(c):
    g = CMAP.get(ord(c))
    if g is None:
        return 24 if unicodedata.east_asian_width(c) in "FWA" else 12
    return int(HMTX[g][0] * 24 / UPM + 0.5)


def visible(s):
    return TAGRE.sub("", RB.sub(lambda m: m.group(2), s))


def folds(text, w):
    x, n = 0, 1
    for c in text:
        d = px(c)
        if x + d >= w and x > 0:
            n += 1
            x = 0
        x += d
    return n


def ko(s):
    return "".join(DEC.get(c, c) for c in s)


def check(pak):
    a = K.parse_archive(Path(pak))
    files = {e.name.rsplit("\\", 1)[-1].lower(): K.decode_stored(e, K.read_stored(a, e)) for e in a.entries
             if e.name.lower().endswith(".nps")}
    over, over_voice, staged, selffold = [], [], [], 0
    for name, raw in sorted(files.items()):
        box, block, voice = 0, [], False
        lines = raw.decode("cp932", "replace").split("\r")
        for i, line in enumerate(lines + [""]):
            m = re.search(r'<BOX\s+type="(\d+)"', line, re.I)
            if m:
                box = int(m.group(1))
            if re.search(r"<VOICE\b", line, re.I):
                voice = True
            vis = visible(line)
            if line.strip() == "":
                if block:
                    n = sum(k for _, k in block)
                    if box in DIALOG and n > 3:
                        (over_voice if voice else over).append((name, box, n, " / ".join(ko(visible(t)).strip() for t, _ in block)[:150]))
                block, voice = [], False
                continue
            if not vis:
                continue
            w = BOXW.get(box, 700)
            k = folds(vis, w)
            if k > 1:
                head = len(vis) - len(vis.lstrip("\u3000 "))
                if head > 2:
                    staged.append((name, box, head, ko(vis.strip())[:60]))
                elif box in DIALOG:
                    selffold += 1
            block.append((line, k))
    return over, over_voice, staged, selffold


for p in sys.argv[1:]:
    over, ov, st, sf = check(p)
    print(f"== {p}\n  messages over 3 lines: {len(over) + len(ov)} (with voice {len(ov)}) | staged lines overflowing: {len(st)} | dialog lines the engine folds itself: {sf}")
    if "-v" in sys.argv[1:] or True:
        for x in ov[:12]:
            print("   voice", x)
        for x in st[:60]:
            print("   staged", x)
