"""Per-character voice checks on the corrected text (out/responses), unit level."""
import collections, json, re, sys
sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import load
units = []
for pid in load.packets():
    cur = collections.OrderedDict()
    for s in load.segments(pid, r"..\..\out\responses"):
        u = cur.setdefault(s["uid"], {"sp": s.get("speaker") or "", "jp": "", "ko": "", "rd": "", "sid": s["sid"]})
        u["jp"] += s["jp"]
        if s["kind"] == "ruby_reading": u["rd"] += (s["ko"] or "") + " "
        else: u["ko"] += s["ko"] or ""
    units += list(cur.values())
def sp(name): return [u for u in units if u["sp"] == name or u["sp"].startswith(name)]
def show(title, pairs):
    print(f"== {title}")
    for k, v in pairs: print(f"   {k}: {v}")
# marker pairs: (speaker, JP regex, KO regex, label)
checks = [
 ("エルザ", r"ロボ", r"로보", "엘자 로보(원문에 있을 때만)"),
 ("シュリュズベリイ", r"レディ", r"레이디", "슈루즈베리 레이디"),
 ("ハヅキ", r"ダディ", r"대디", "하즈키 대디"),
 ("エセルドレーダ", r"イエス", r"예스", "에셀드레이다 예스, 마스터"),
 ("ストーン", r"であります", r"말입니다|입니다", "스톤 であります"),
 ("ライカ", r"九郎ちゃん", r"쿠로 ?쨩", "라이카 쿠로 쨩"),
 ("ドクター・ウェスト", r"であ[ー〜～]*る|なのであ", r"것이다|이다아|이다—|이로다|인 것이", "웨스트 である 계열"),
]
for speaker, jre, kre, label in checks:
    us = [u for u in sp(speaker) if re.search(jre, u["jp"])]
    ok = [u for u in us if re.search(kre, u["ko"] + u["rd"])]
    miss = [u for u in us if not re.search(kre, u["ko"] + u["rd"])]
    print(f"{label}: 원문 {len(us)} → 살림 {len(ok)}", ("  빠진 곳: " + " | ".join(f"{m['sid']} {m['ko'][:40]!r}" for m in miss[:4])) if miss else "")
    # also: KO marker where JP lacks it (added)
    added = [u for u in sp(speaker) if re.search(kre, u["ko"]) and not re.search(jre, u["jp"])]
    if added and speaker in ("エルザ", "シュリュズベリイ", "ハヅキ"):
        print(f"   원문에 없는데 붙은 곳 {len(added)}: " + " | ".join(f"{a['sid']} {a['ko'][:40]!r}" for a in added[:4]))
# register profiles
def endings(t):
    t = re.sub(r"[「」『』…―!?！？.、。〜~\s]+$", "", t.strip())
    return t[-3:]
HAEYO = re.compile(r"(요|죠|세요|에요|예요)[」!?.…~]*$|요[!?.…~]*」?$")
HAPSYO = re.compile(r"(니다|니까|십시오|습니까|ㅂ니다)[」!?.…~]*$")
def reg(text):
    lines = [l for l in re.split(r"[\n]", text) if l.strip()]
    last = re.sub(r"[」』…―!?！？.,、。〜~\s　]+$", "", text)
    if re.search(r"(니다|니까|십시오|습니다)$", last): return "합쇼"
    if re.search(r"(요|죠)$", last): return "해요"
    if re.search(r"(네|게|나|세|오|소)$", last): return "하게·하오"
    return "반말·해라"
for name in ("ウィンフィールド", "瑠璃", "九郎", "アル"):
    c = collections.Counter(reg(u["ko"]) for u in sp(name) if u["ko"].lstrip().startswith("「"))
    print(f"{name} 대사 끝 어미: {dict(c)}")
# Al: 2nd person
c = collections.Counter()
for u in sp("アル"):
    if "汝" in u["jp"]:
        c.update(re.findall(r"그대|네놈|너(?=[는가를의도에])|당신", u["ko"]))
print("알의 汝:", dict(c))
