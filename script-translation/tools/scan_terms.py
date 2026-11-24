"""Count remaining occurrences of known-bad renderings and spelling splits
across all packets, against a given responses folder.

  python -I -X utf8 scan_terms.py [responses_dir] [label ...]
"""
import json
import re
import sys
from pathlib import Path

PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
CUR = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"P:\AI\khd-argon-review\out\responses")
ONLY = set(sys.argv[2:])

CHECKS = [
    ("瘴気→장기", r"瘴気", r"장기"),
    ("妾→짐", r"妾", r"(?<![가-힣])짐(?:은|이|을|의|도|에게|과|만|이나|이야|이다)?(?![가-힣])"),
    ("コリン 언니", r"お姉ちゃん", r"언니"),
    ("ダンウィッチ", r"ダンウィッチ", r"(던위치|더누이치|더니치)"),
    ("腐乱→부란", r"腐乱", r"부란"),
    ("怨嗟→원사", r"怨嗟", r"원사"),
    ("呪詛→주저/주소", r"呪(\s|　)*詛", r"(주저|주소|주　소)"),
    ("紫電→자전", r"紫電", r"자전"),
    ("魔刃→마인", r"魔刃", r"마인"),
    ("脚部", r"脚部", r"(각부|다리 부분|다리 실드|다리)"),
    ("一切の→일절", r"一切", r"일절"),
    ("不敵→불적", r"不敵", r"불적"),
    ("臓腑→장부", r"臓腑", r"장부"),
    ("全長→전장", r"全長", r"전장"),
    ("視認→시인", r"視認", r"시인"),
    ("外なる神", r"外なる神", r"(외부의 신|바깥의 신|외신|바깥 신)"),
    ("イブン・ガズイ", r"ガズイ", r"(가지|가즈이)"),
    ("姫さん", r"姫さん", r"(공주님|아가씨|아씨|공주)"),
    ("謳→구가", r"謳", r"구가"),
    ("久遠→구원", r"久遠", r"구원"),
    ("邪神 한자괄호", r"邪神", r"\(邪神\)"),
    ("覇道", r"覇道", r"(하도우|하도|패도)"),
    ("ブラックロッジ", r"ブラックロッジ", r"(블랙 로지|블랙 롯지|블랙로지|블랙롯지)"),
    ("紅の少女", r"紅の少女", r"(붉은 소녀|진홍의 소녀|진홍빛 소녀)"),
    ("人影→인영", r"人影", r"인영"),
    ("唐突→당돌", r"唐突", r"당돌"),
    ("緋色→비색", r"緋色", r"비색"),
    ("芳香→방향", r"芳香", r"방향"),
]


def main():
    cur = {}
    for f in CUR.glob("*.json"):
        for r in json.loads(f.read_text(encoding="utf-8"))["records"]:
            cur[r["id"]] = r.get("ko")
    segs = []
    for p in sorted((PKG / "packets").glob("*.json")):
        for u in json.loads(p.read_text(encoding="utf-8"))["units"]:
            for s in u["segments"]:
                segs.append((u, s))
    for label, jp_re, ko_re in CHECKS:
        if ONLY and label not in ONLY:
            continue
        hits, forms = [], {}
        for u, s in segs:
            if not re.search(jp_re, s["jp"]):
                continue
            ko = cur.get(s["id"]) or ""
            for m in re.finditer(ko_re, ko):
                forms[m.group(0)] = forms.get(m.group(0), 0) + 1
                hits.append((s["id"], u.get("speaker_jp"), ko))
        print(f"== {label}: {len(hits)} {forms}")
        for sid, spk, ko in hits[:12]:
            print(f"   {sid} {spk or '지문'} | {ko[:70]!r}")


if __name__ == "__main__":
    main()
