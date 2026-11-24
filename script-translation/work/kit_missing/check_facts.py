"""Facts for the addendum: trap contexts in missing packets, corrected-text renderings of the same words."""
import collections
import json
import os
import re
import sys

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load

PKG = r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031"
R = r"P:\AI\khd-argon-review\out\responses"
miss = [p for p in sorted(os.listdir(os.path.join(PKG, "packets"))) if p not in set(os.listdir(R))]
mu = []
for p in miss:
    mu += json.load(open(os.path.join(PKG, "packets", p), encoding="utf-8"))["units"]
units = []
for pid in load.packets():
    cur = collections.OrderedDict()
    for s in load.segments(pid, r"..\..\out\responses"):
        u = cur.setdefault(s["uid"], {"sp": s.get("speaker") or "", "jp": "", "ko": "", "sid": s["sid"]})
        u["jp"] += s["jp"]
        u["ko"] += ("〔" + (s["ko"] or "") + "〕") if s["kind"] == "ruby_reading" else (s["ko"] or "")
    units += list(cur.values())

for w in ["苛", "星気", "全霊", "大冊", "無貌", "秘め", "脚部", "姫さん", "親父殿", "母上", "怨敵", "偃月刀", "妖蛆", "爆神", "久遠", "念話", "鋼造"]:
    m = [u for u in mu if w in u["source_text"]]
    print(f"### {w}: 미확보 {len(m)}")
    for u in m[:3]:
        i = u["source_text"].find(w)
        print(f"   {u['id']} {u.get('speaker_jp') or '-'} …{u['source_text'][max(0, i - 12):i + 16]!r}")
    c = [u for u in units if w in u["jp"]]
    for u in c[:3]:
        i = u["jp"].find(w)
        print(f"   교정본 {u['sid']} {u['jp'][max(0, i - 10):i + 14]!r} → {u['ko'][:70]!r}")

print("### 웨스트 である (미확보)")
for u in mu:
    if u.get("speaker_jp") == "ドクター・ウェスト" and re.search(r"であ[ー〜～]*る", u["source_text"]):
        print("  ", u["id"], repr(u["source_text"][:60]))
print("### である 지문/다른 화자 (미확보)", sum(1 for u in mu if u.get("speaker_jp") != "ドクター・ウェスト" and "である" in u["source_text"]))
print("### チアキ (미확보)")
for u in mu:
    if (u.get("speaker_jp") or "").startswith("チアキ"):
        print("  ", u["id"], repr(u["source_text"][:50]))
print("### リューガ/コリン 화자", collections.Counter(u.get("speaker_jp") for u in mu if u.get("speaker_jp") in ("リューガ", "コリン", "ジョージ", "アリスン", "ライカ", "ストーン")))
