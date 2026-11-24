"""How the main speakers of the missing files already talk in the corrected text (out/responses).

  python -I -X utf8 voice_profile.py
"""
import collections
import json
import re
import sys

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load

units = []
for pid in load.packets():
    cur = collections.OrderedDict()
    for s in load.segments(pid, r"..\..\out\responses"):
        u = cur.setdefault(s["uid"], {"sp": s.get("speaker") or "", "jp": "", "ko": "", "sid": s["sid"]})
        u["jp"] += s["jp"]
        if s["kind"] != "ruby_reading":
            u["ko"] += s["ko"] or ""
    units += list(cur.values())


def reg(text):
    last = re.sub(r"[」』…―!?！？.,、。〜~\s　ー]+$", "", text)
    if re.search(r"(니다|니까|십시오|습니다|시오)$", last):
        return "합쇼"
    if re.search(r"(요|죠)$", last):
        return "해요"
    if re.search(r"(네|게|나|세|오|소|구려|시게)$", last):
        return "하게·하오"
    if re.search(r"(노라|니라|도다|로다|느냐|거라|구나)$", last):
        return "예스러운"
    return "반말·해라"


FIRST = r"(?<![가-힣])(나는|내가|나를|나의|내 |저는|제가|저를|제 |이 몸|짐은|짐이|짐의|본인|우리|저희|소인|여기 있는 나)"
SECOND = r"(?<![가-힣자])(너는|네가|너를|너의|네 |그대|당신|네놈|자네|귀하|그쪽|너희|아버지|아빠)"
for name in ("九朔", "アズラッド", "アナザーブラッド", "ナイア", "ニアーラ", "メタトロン", "ファーザー・ナイ",
             "瑠璃", "ウェイトリイ", "ダンセイニ", "旧神", "ネス", "ジョージ", "コリン", "アリスン"):
    us = [u for u in units if u["sp"].startswith(name) or name in u["sp"]]
    if not us:
        print(f"== {name}: 교정본에 없음")
        continue
    files = collections.Counter(u["sid"].split(":")[0] for u in us)
    regs = collections.Counter(reg(u["ko"]) for u in us if u["ko"].lstrip().startswith(("「", "『")))
    f1 = collections.Counter(m.strip() for u in us for m in re.findall(FIRST, u["ko"]))
    f2 = collections.Counter(m.strip() for u in us for m in re.findall(SECOND, u["ko"]))
    jp1 = collections.Counter(m for u in us for m in re.findall(r"妾|我輩|我|余|私|僕|俺|あたし|わたし|ボク|オレ", u["jp"]))
    jp2 = collections.Counter(m for u in us for m in re.findall(r"汝|貴様|お前|あなた|貴方|君|てめえ|父上|父さん|ダディ|パパ", u["jp"]))
    print(f"== {name}: {len(us)}문맥, 파일 {dict(files.most_common(6))}")
    print(f"   어미 {dict(regs)}")
    print(f"   1인칭 JP {dict(jp1.most_common(5))} → KO {dict(f1.most_common(6))}")
    print(f"   2인칭 JP {dict(jp2.most_common(5))} → KO {dict(f2.most_common(6))}")
    ex = [u for u in us if u["ko"].lstrip().startswith("「")][:2]
    for u in ex:
        print(f"   예 {u['sid']} {u['jp'][:40]!r} → {u['ko'][:50]!r}")

ko_all = "\n".join(u["ko"] for u in units)
print("\n[보류 용어의 교정본 표기]")
for jp, cands in (("九朔", ["쿠자쿠", "쿠자루", "구삭"]), ("アズラッド", ["아즈랏드", "아즈라드"]),
                  ("エセルドレーダ", ["에셀드레이다", "에셀드레다"]), ("リベル・レギス", ["리베르 레기스", "리벨 레기스"]),
                  ("偃月刀", ["언월도"]), ("ミスカトニック", ["미스캐토닉", "미스카토닉"]),
                  ("怨敵", ["원적", "원수", "숙적"]), ("姫さん", ["공주님", "아가씨"]), ("瘴気", ["독기", "장기", "瘴기"]),
                  ("外なる神", ["바깥의 신", "외신", "외부의 신"]), ("久遠", ["구원", "영원"]), ("念話", ["염화", "텔레파시"])):
    njp = sum(u["jp"].count(jp) for u in units)
    print(f"  {jp}: 원문 {njp} / " + ", ".join(f"{c} {ko_all.count(c)}" for c in cands))
