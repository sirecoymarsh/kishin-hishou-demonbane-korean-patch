"""Sweep the corrected text for the homograph-trap readings still sitting in units whose JP has the trap kanji."""
import collections
import re
import sys

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load

PAIRS = [("人影", "인영"), ("芳香", "방향"), ("緋色", "비색"), ("紫電", "자전"), ("防戦", "방전"), ("校舎", "교사"),
         ("全機", "전기"), ("光柱", "광주"), ("受肉", "수육"), ("本命", "본명"), ("鏖殺", "오살"), ("劣情", "열정"),
         ("星気", "성기"), ("大冊", "대책"), ("愚物", "우물"), ("静謐", "정밀"), ("不敵", "불적"), ("屍骸", "시해"),
         ("養母", "양모"), ("四散", "사산"), ("無貌", "무모"), ("苛ま", "가책"), ("呪詛", "주저|주소"), ("内周", "내주"),
         ("改修", "개수"), ("下賤", "하천"), ("手刀", "수도"), ("視認", "시인"), ("奔流", "분류"), ("凶相", "흉상"),
         ("死相", "사상"), ("前門", "전문"), ("鮮度", "선도"), ("禁呪", "금주"), ("呪具", "주구"), ("無骨", "무골"),
         ("諒解", "양해"), ("脚部", "각부"), ("魔刃", "마인"), ("秘め", "비장"), ("銀光", "은광"), ("敵影", "적영"),
         ("機影", "기영"), ("唐突", "당돌"), ("瘴気", "장기"), ("全霊", "전령"), ("奇遇", "기우"), ("敵性", "적성")]
segs = []
for pid in load.packets():
    cur = collections.OrderedDict()
    for s in load.segments(pid, r"..\..\out\responses"):
        u = cur.setdefault(s["uid"], {"jp": "", "cells": []})
        u["jp"] += s["jp"]
        u["cells"].append((s["sid"], s["kind"], s["ko"] or ""))
    segs += list(cur.values())
left = 0
for jp, ko in PAIRS:
    for u in segs:
        if jp in u["jp"]:
            for sid, kind, k in u["cells"]:
                m = re.search(ko, k)
                if m:
                    left += 1
                    print(f"{jp}→{m.group(0)}  {sid} {kind}  {k[max(0, m.start() - 18):m.end() + 12]!r}")
print("left", left)
