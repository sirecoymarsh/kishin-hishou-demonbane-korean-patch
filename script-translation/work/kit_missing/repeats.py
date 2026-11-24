"""Lines in the missing packets that already appear (same Japanese) in the 104 corrected packets.

  python -I -X utf8 repeats.py          # summary
  python -I -X utf8 repeats.py write    # also write repeats.json (JP line -> corrected KO unit) for the kit
"""
import collections
import json
import os
import re
import sys

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load

PKG = r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031"
R = r"P:\AI\khd-argon-review\out\responses"
OUT = r"P:\AI\khd-argon-review\work\kit_missing\repeats.json"
miss = [p for p in sorted(os.listdir(os.path.join(PKG, "packets"))) if p not in set(os.listdir(R))]

done = {}
for pid in load.packets():
    cur = collections.OrderedDict()
    for s in load.segments(pid, r"..\..\out\responses"):
        u = cur.setdefault(s["uid"], {"uid": s["uid"], "jp": "", "segs": []})
        u["jp"] += s["jp"]
        u["segs"].append({"kind": s["kind"], "jp": s["jp"], "ko": s["ko"]})
    for u in cur.values():
        done.setdefault(u["jp"], u)


def norm(t):
    return re.sub(r"[\s\u3000]", "", t)


done_norm = {norm(k): v for k, v in done.items()}
# line level: split corrected units into JP lines that are long enough to be distinctive
line_src = {}
for u in done.values():
    for ln in re.split(r"\n+", u["jp"]):
        k = norm(ln)
        if len(k) >= 12:
            line_src.setdefault(k, u["uid"])

exact, lines, rows = 0, 0, []
for p in miss:
    for u in json.load(open(os.path.join(PKG, "packets", p), encoding="utf-8"))["units"]:
        src = u["source_text"]
        hit = done.get(src) or done_norm.get(norm(src))
        if hit and len(norm(src)) >= 6:
            exact += 1
            rows.append({"unit": u["id"], "match": "unit", "from": hit["uid"], "jp": src,
                         "ko_segments": [s["ko"] for s in hit["segs"]]})
            continue
        ls = [ln for ln in re.split(r"\n+", src) if len(norm(ln)) >= 12 and norm(ln) in line_src]
        if ls:
            lines += 1
            rows.append({"unit": u["id"], "match": "line", "from": sorted({line_src[norm(l)] for l in ls}),
                         "jp_lines": ls})
print(f"미확보 문맥 중 교정본과 문맥 전체가 같은 것 {exact}, 긴 줄(12자+)이 같은 것 {lines}")
by_file = collections.Counter(r["unit"].split(":")[0] for r in rows)
print(dict(by_file))
for r in rows[:12]:
    print(" ", r["unit"], r["match"], r["from"] if r["match"] == "unit" else r["from"][:3],
          repr((r.get("jp") or " / ".join(r["jp_lines"]))[:60]))
if sys.argv[1:] == ["write"]:
    json.dump(rows, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("wrote", OUT, len(rows))
