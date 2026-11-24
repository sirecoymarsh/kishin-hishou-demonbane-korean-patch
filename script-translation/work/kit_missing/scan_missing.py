"""Scan the 33 packets Argon never returned: speakers, lanes, ruby, decided terms, homograph traps.

  python -I -X utf8 scan_missing.py
"""
import collections
import json
import os
import re

PKG = r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031"
pk = sorted(os.listdir(os.path.join(PKG, "packets")))
rs = set(os.listdir(os.path.join(PKG, "responses")))
miss = [p for p in pk if p not in rs]

TERMS = ["覇道", "鋼造", "ブラックロッジ", "皇餓", "リューガ", "紅の少女", "蠱毒", "久遠", "念話", "妖蟲",
         "爆神", "怨敵", "瘴気", "魔刃", "ダニッチ", "外なる神", "姫さん", "妾", "汝", "忙殺", "外道",
         "九朔", "クザク", "アズラッド", "エセルドレーダ", "リベル・レギス", "ミスカトニック", "偃月刀", "掃射",
         "ズアウィア", "ロボ", "であーる", "である", "ダディ", "ダーリン", "レディ", "イエス"]
TRAPS = ["唐突", "人影", "瘴気", "芳香", "緋色", "紫電", "防戦", "校舎", "全機", "光柱", "受肉", "本命", "鏖殺",
         "劣情", "星気", "大冊", "愚物", "静謐", "不敵", "屍骸", "養母", "四散", "無貌", "苛", "呪詛", "内周",
         "改修", "下賤", "手刀", "視認", "奔流", "凶相", "死相", "前門", "鮮度", "禁呪", "呪具", "無骨", "諒解",
         "脚部", "魔刃", "秘め", "銀光", "敵影", "忙殺", "機影", "奇遇", "敵性", "全霊"]
TL = {"のだ/のである": r"の(?:だ|である|です)", "によって": r"によ[っり]", "と同時に": r"と同時に",
      "ほど": r"ほど", "について": r"について", "より(起点)": r"より(?![もは])", "それ": r"それ",
      "なければならない": r"なければ(?:ならな|いけな)", "彼女(대사)": None, "彼(대사)": None}

sp = collections.Counter()
lanes = collections.Counter()
kinds = collections.Counter()
term = collections.Counter()
trap = collections.Counter()
trap_ex = {}
tl = collections.Counter()
latin = []
units = segs = 0
for p in miss:
    d = json.load(open(os.path.join(PKG, "packets", p), encoding="utf-8"))
    for u in d["units"]:
        units += 1
        sp[u.get("speaker_jp") or "(지문)"] += 1
        lanes[u.get("lane")] += 1
        jp = u["source_text"]
        dialog = bool(u.get("speaker_jp"))
        for s in u["segments"]:
            segs += 1
            kinds[s["kind"]] += 1
        for t in TERMS:
            term[t] += jp.count(t)
        for t in TRAPS:
            n = jp.count(t)
            if n:
                trap[t] += n
                trap_ex.setdefault(t, f"{u['id']} {jp[:40]!r}")
        for k, rx in TL.items():
            if rx:
                tl[k] += len(re.findall(rx, jp))
        if dialog:
            tl["彼女(대사)"] += jp.count("彼女")
            tl["彼(대사)"] += len(re.findall(r"彼(?!女|方|岸|ら)", jp))
        if re.search(r"[Ａ-Ｚａ-ｚ]{2,}|[A-Za-z]{3,}", jp) and u.get("speaker_jp") != "ズアウィア":
            latin.append(f"{u['id']} {jp[:50]!r}")

print("packets", len(miss), "units", units, "segments", segs, dict(kinds), dict(lanes))
print("\n[speakers top 40]")
for k, v in sp.most_common(40):
    print(f"  {v:4} {k}")
print("\n[terms]", {k: v for k, v in term.items() if v})
print("\n[traps]")
for k, v in trap.most_common():
    print(f"  {v:3} {k}  {trap_ex[k]}")
print("\n[translationese sources]", dict(tl))
print("\n[latin]", len(latin))
for x in latin[:30]:
    print("  ", x)
