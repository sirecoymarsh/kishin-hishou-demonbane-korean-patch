"""Build Gemini polish inputs: one file per packet with the CURRENT corrected Korean only (no old build, no raw draft).

  python -I -X utf8 build_inputs.py      # writes ../in/<packet>.json and prints packet sizes
"""
import collections
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load

OUT = Path(r"P:\AI\khd-argon-review\gemini_polish\in")
stats = []
for pid in load.packets():
    segs = list(load.segments(pid, r"..\..\out\responses"))
    units = collections.OrderedDict()
    for s in segs:
        units.setdefault(s["uid"], []).append(s)
    out_units = []
    n_cells = n_narr = n_dia = 0
    for uid, ss in units.items():
        speaker = ss[0].get("speaker") or "(지문)"
        ujp = "".join(f"〈{s['jp']}〉" if s["kind"] == "ruby_reading" else s["jp"] for s in ss)
        cells = []
        for i, s in enumerate(ss):
            c = {"sid": s["sid"], "kind": "루비 읽기" if s["kind"] == "ruby_reading" else ("선택지" if s["kind"] == "choice_text" else "본문"),
                 "jp": s["jp"], "ko": s["ko"]}
            if not isinstance(s["ko"], str):
                c["lock"] = "엔진 명령 칸(원본 그대로 보존)"
            elif s["kind"] == "ruby_reading":
                c["lock"] = "루비 읽기 칸은 고치지 않는다"
            elif not re.search(r"[가-힣]", s["ko"]):
                c["lock"] = "한글 없는 칸"
            else:
                edge = []
                if i > 0 and ss[i - 1]["kind"] == "ruby_reading":
                    edge.append("앞")
                if i + 1 < len(ss) and ss[i + 1]["kind"] == "ruby_reading":
                    edge.append("뒤")
                if edge:
                    c["ruby_edge"] = "·".join(edge)
                n_cells += 1
            cells.append(c)
        out_units.append({"uid": uid, "speaker": speaker, "jp": ujp, "cells": cells})
        if speaker == "(지문)":
            n_narr += 1
        else:
            n_dia += 1
    name = Path(load.packet_path(pid)).name if hasattr(load, "packet_path") else f"{pid}.json"
    doc = {"packet": pid, "units": out_units}
    (OUT / f"{pid}.json").write_text(json.dumps(doc, ensure_ascii=False, indent=1), encoding="utf-8")
    stats.append((pid, len(out_units), n_cells, n_narr, n_dia))
print(len(stats), "packets;", sum(s[1] for s in stats), "units;", sum(s[2] for s in stats), "editable cells;",
      sum(s[3] for s in stats), "narration units;", sum(s[4] for s in stats), "dialogue units")
for s in stats[:3] + stats[-2:]:
    print("  ", s)
json.dump(stats, open(OUT.parent / "tools" / "packet_stats.json", "w", encoding="utf-8"), ensure_ascii=False)
