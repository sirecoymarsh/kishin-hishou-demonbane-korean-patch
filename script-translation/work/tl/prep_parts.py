"""Build agent input files (work/tl/part1..4.json) with unit context for each candidate cell."""
import collections, json, sys
sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load
cand = {c["sid"]: c for c in json.load(open(r"P:\AI\khd-argon-review\work\tl\candidates.json", encoding="utf-8"))}
items = []
for pid in load.packets():
    segs = list(load.segments(pid, r"..\..\out\responses"))
    units = collections.OrderedDict()
    for s in segs:
        units.setdefault(s["uid"], []).append(s)
    prev_ko = ""
    for uid, ss in units.items():
        ujp = "".join(f"〈{s['jp']}〉" if s["kind"] == "ruby_reading" else s["jp"] for s in ss)
        uko = "".join(f"[{s['sid'].split(':')[1]}]{s['ko'] or ''}" for s in ss)
        for i, s in enumerate(ss):
            if s["sid"] in cand:
                nb = [ss[j]["kind"] for j in (i - 1, i + 1) if 0 <= j < len(ss)]
                items.append({"sid": s["sid"], "speaker": s.get("speaker") or "(지문)", "hits": cand[s["sid"]]["hits"],
                              "cell_jp": s["jp"], "cell_ko": s["ko"], "unit_jp": ujp, "unit_ko": uko,
                              "prev_unit_ko": prev_ko[-160:], "ruby_neighbor": "ruby_reading" in nb})
        prev_ko = "".join(s["ko"] or "" for s in ss)
n = len(items); k = 4
for p in range(k):
    part = items[p * n // k:(p + 1) * n // k]
    json.dump(part, open(rf"P:\AI\khd-argon-review\work\tl\part{p+1}.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(f"part{p+1}: {len(part)} cells, {part[0]['sid']} .. {part[-1]['sid']}")
