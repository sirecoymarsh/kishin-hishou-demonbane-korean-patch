"""Collect translationese candidates in the corrected text (out/responses), per text cell."""
import collections, json, re, sys
sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load
PATS = {
    "에의해": r"에 의해|에 의한|에 의하여",
    "동시에": r"(함|과|와|는 것과) 동시에",
    "것이다": r"[가-힣] 것이(다|었다|었던)|[가-힣]던 것이다",
    "가득차": r"가득 차",
    "정도의": r"정도의|정도로",
    "에대해": r"에 대해|에 대한|에 대하여",
    "로부터": r"로부터|에게서부터",
    "그것": r"그것[은을이]",
    "적인": r"(대규모|통상|기형|일반|최종|결정|압도|절대|근본|본격|구체|직접|간접|기본|개인|전체|부분|일시|지속|순간|본능|물리|정신|육체|본질|실질|상대|형식|극단|비정상|이상)적(인|으로)",
    "않으면안": r"않으면 안|않고서는 안",
    "이중피동": r"되어지|어지게 되|려지게|되어져",
    "가능": r"것이 가능|것은 가능|것도 가능",
    "있어서": r"에 있어서|에 있어",
    "터": r"할 터|일 터|할 터인|ㄹ 터",
}
cells = []
for pid in load.packets():
    for s in load.segments(pid, r"..\..\out\responses"):
        if s["kind"] == "ruby_reading" or not s["ko"]:
            continue
        hits = [k for k, p in PATS.items() if re.search(p, s["ko"])]
        if hits:
            cells.append({"pid": pid, "sid": s["sid"], "uid": s["uid"], "speaker": s.get("speaker") or "",
                          "jp": s["jp"], "ko": s["ko"], "hits": hits})
c = collections.Counter(h for x in cells for h in x["hits"])
print("cells with candidates:", len(cells)); print(dict(c.most_common()))
json.dump(cells, open(r"P:\AI\khd-argon-review\work\tl\candidates.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
