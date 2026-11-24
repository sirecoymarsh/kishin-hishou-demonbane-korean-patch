"""Claude's fixes on the Gemini translations (after fix_structure.py); every change goes to fix_log.jsonl.

1. glossary names Gemini drifted on (unit-level, only where the JP term is in the unit), with the following particle
   re-chosen when the final consonant changes (샨탁을 -> 샨타크를, 파괴로봇이 -> 파괴로보가)
2. hand fixes per cell (emptied cells refilled, dropped clauses restored, traps, Al's name, ruby copula)
3. da0202 side file: literal "\\n" -> newline, Stone's tic removed where the source has no であります
4. missing space after sentence-final punctuation between two text cells shown on one line (scan_joins logic)

  python -I -X utf8 fix_terms.py
"""
import json
import re
import time
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
LOG = ROOT / "fix_log.jsonl"
BY = "Claude Opus 5.5 (claude-opus-5-5)"
VOWEL_FORM = [("으로", "로"), ("이었", "였"), ("이라", "라"), ("이야", "야"), ("이여", "여"), ("이다", "다"),
              ("을", "를"), ("은", "는"), ("과", "와"), ("이", "가"), ("아", "야")]
# (JP trigger in the unit, JP that must NOT be in the unit, old regex, new, final consonant goes away)
TERM_RULES = [("大十字", None, r"다이지유지|다이지주시|다이주지", "다이쥬지", False),
              ("アーカム", None, r"아캄 ?시티", "아컴 시티", False), ("アーカム", None, r"아캄", "아컴", False),
              ("シャンタク", None, r"샨탁", "샨타크", True),
              ("鬼械神", "機械神", r"기계신", "귀계신", False),
              ("破壊ロボ", None, r"파괴 ?로봇", "파괴로보", True), ("無敵ロボ", None, r"무적 ?로봇", "무적로보", True),
              ("ダンセイニ", None, r"단세이니", "던세이니", False),
              ("クロックワーク", None, r"클락워크 ?팬텀|클록워크팬텀", "클록워크 팬텀", False),
              ("トゥーソード", None, r"투소드", "투 소드", False)]
HAND = {  # sid: [(old, new, why)]  — old "" means the cell was empty
    "da1314:R00002": [("마테리얼과", "마테리얼", "조사를 뒤 칸 「と」로 돌려 빈 칸을 없앰")],
    "da1314:T00004": [("물질과", "물질", "조사를 뒤 칸 「と」로 돌림")],
    "da1314:T00005": [("", "과 ", "빈 칸 금지 — 원문 「と」 칸에 조사와 띄어쓰기")],
    "da1314:T00149": [("이런 곳 따위를", "이런 곳 따위", "조사를 뒤 칸 「を、」로 돌림")],
    "da1314:T00150": [("", "를, ", "빈 칸 금지 — 원문 「を、」")],
    "da1314:T00136": [("전신전령의 일격", "혼신의 일격", "한자음 함정 全霊→전령(傳令)")],
    "da1314:T00135": [("바르자이의 언월도", "바르자이 언월도", "확정 표기(교정본 「바르자이 언월도」)")],
    "da1315:T00119": [("「아즈라아아아드――――앗!」", "「아즈라드으으으――――읏!」", "확정 표기 아즈라드 + 원문처럼 끝소리를 늘임")],
    "da1316:T00002": [("잃어버렸을 ", "사라졌어야 ", "빈 칸 「の」를 채우려 はずの 구절을 다시 나눔(~할 터인 직역투도 함께)")],
    "da1316:T00003": [("터인 ", "했", "筈 칸")],
    "da1316:T00004": [("", "을 ", "빈 칸 금지 — 원문 「の」")],
    "da1316:T00007": [("두 번 다시 움직이지 못할 터였던, ", "두 번 다시 움직이지 않았어야 할, ", "같은 꼴(筈だった)을 맞춤")],
    "da1318:T00024": [("", "붕괴할 때마다 귀계신은 새로운 ", "빠진 번역 복원 — 원문 「崩壊のたびに鬼械神は新たな」")],
    "da1318:R00007": [("파츠", "파츠를", "루비 조사 짝(파츠를/부품을)")],
    "da1318:T00025": [("부품", "부품을", "루비 조사 짝")],
    "da1318:T00026": [("을 생산한다.", " 생산한다.", "조사를 두 칸으로 옮김")],
    "da1318:T00028": [("", "『제조』⇔『붕괴』의, 우스꽝스럽고 ", "빠진 번역 복원 — 원문 「『製造』⇔『崩壊』の、滑稽で」")],
    "da1318:T00066": [("네놈은데몬베인이아니다", "네놈은 데몬베인이 아니다", "권점 칸의 띄어쓰기를 되살림")],
    "da1318:T00067": [("\n\u3000", "! ", "원문 「！」 — 줄바꿈으로 바꿔 놓았음")],
    "da1318:T00068": [("너는나다", "네놈은 나다", "띄어쓰기 + 같은 お前를 한 줄 안에서 「네놈」으로")],
    "da1318:T00069": [("\n\u3000", "!\u3000얼굴 없는 천의 얼굴을 지닌 나의, 그 ", "빠진 번역 복원 — 원문 「！　千の無貌を持つ我の、その」")],
    "da1318:T00082": [("전신전령을 걸고", "온몸과 영혼을 걸고", "한자음 함정 全霊→전령(傳令)")],
    "da1318:R00025": [("검", "검이다", "루비 조사 짝 — 읽기 「검」+「다」가 「검다」로 읽힘")],
    "da1318:T00083": [("기사도", "기사도다", "루비 조사 짝")],
    "da1318:T00084": [("다!」", "!」", "서술격 조사를 두 칸으로 옮김")],
    "da1502a:T00001": [("또다시", "또다시 ", "다음 낱말과 띄어 씀")],
    "da1502a:T00002": [("비유", "비", "빈 칸 「え」를 채우려 비유담을 세 칸에 나눔")],
    "da1502a:T00003": [("", "유", "빈 칸 금지 — 원문 「え」")],
    "da1502a:T00078": [("그 무모에 뚫린,", "그 얼굴 없는 얼굴에 뚫린,", "한자음 함정 無貌→무모(無謀)")],
    "da0605:T00077": [("간다, 아르!", "간다, 알!", "확정 표기 알")],
}
ATTACH = re.compile(
    r"^(이라고|라고|이라는|라는|이란|란|이야|이여|이며|이자|이다|이었|였|이지|이나|이냐|이니|이랑|"
    r"에게|에서|에는|에도|으로|로|와|과|은|는|이|가|을|를|의|에|도|만|께|한테|처럼|보다|까지|부터|"
    r"마저|조차|님|들|하|한|할|함|했|해|된|되|될|됨|적|스러|답|다|요|야|여|지|고|며|면|서|게|기|"
    r"던|든|씩|째|짜리|끼리|뿐|밖|대로|만큼|쯤|씨|군|양|짱|쨩|사마|짓|거리|스럽|롭|화|성|감|식)")
SENT_END = re.compile(r"[.!?！？]$")

log = open(LOG, "a", encoding="utf-8")
listed = []


def write_log(pid, sid, field, before, after, why):
    log.write(json.dumps({"packet": pid, "id": sid, "field": field, "before": before, "after": after, "why": why,
                          "by": BY, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}, ensure_ascii=False) + "\n")


def replace_term(text, old_rx, new, loses_batchim):
    if not loses_batchim:
        return re.sub(old_rx, new, text)
    alt = "|".join(re.escape(c) for c, _ in VOWEL_FORM)
    conv = dict(VOWEL_FORM)
    return re.sub(f"(?:{old_rx})({alt})?", lambda m: new + (conv[m.group(1)] if m.group(1) else ""), text)


raw_cache = {}
for f in sorted((ROOT / "responses").glob("P*.json")):
    pid = f.stem
    pkt = json.loads((PKG / "packets" / f"{pid}.json").read_text(encoding="utf-8"))
    resp = json.loads(f.read_text(encoding="utf-8"))
    rec = {r["id"]: r for r in resp["records"]}
    n = 0
    for u in pkt["units"]:
        ujp = "".join(s["jp"] for s in u["segments"])
        for s in u["segments"]:
            r = rec.get(s["id"])
            if r is None or not isinstance(r.get("ko"), str) or r["ko"].startswith("[BROKEN"):
                continue
            before = r["ko"]
            k = before
            for trig, ban, rx, new, loses in TERM_RULES:
                if trig in ujp and not (ban and ban in ujp):
                    k = replace_term(k, rx, new, loses)
            for old, new, why in HAND.get(s["id"], []):
                if old == "" and k == "":
                    k = new
                elif old and k.count(old) == 1:
                    k = k.replace(old, new)
                elif old:
                    listed.append(f"{s['id']}: 손 고침 old {old!r}가 칸에 {k.count(old)}번 — 건너뜀")
            if k != before:
                why = "; ".join(w for o, nw, w in HAND.get(s["id"], [])) or "확정 표기 맞춤"
                write_log(pid, s["id"], "ko", before, k, why)
                r["ko"] = k
                n += 1
        # missing space after a sentence end between two text cells on one line
        texts = [s for s in u["segments"] if s["kind"] == "text"]
        fname = u["file"]
        if fname not in raw_cache:
            cand = list((PKG / "source_jp_A_cp932").glob(fname + "*"))
            raw_cache[fname] = cand[0].read_bytes() if cand else b""
        raw = raw_cache[fname]
        for a, b in zip(texts, texts[1:]):
            if a.get("source_line") != b.get("source_line"):
                continue
            gap = raw[a["source_byte_end"]:b["source_byte_start"]].decode("cp932", "replace")
            if "\n" in gap:
                continue
            ra, rb = rec.get(a["id"]), rec.get(b["id"])
            ka, kb = (ra or {}).get("ko") or "", (rb or {}).get("ko") or ""
            if not ka or not kb or ka[-1] in " \u3000" or kb[0] in " \u3000" or not re.match(r"[가-힣]", kb):
                continue
            if SENT_END.search(ka):
                write_log(pid, b["id"], "ko", kb, " " + kb, "문장 끝 뒤 띄어쓰기(두 칸이 한 줄에 이어 보임)")
                rb["ko"] = " " + kb
                n += 1
            elif re.search(r"[가-힣]$", ka) and not ATTACH.match(kb):
                listed.append(f"{a['id']}|{b['id']}: 붙어 보임 「{ka[-12:]}」‖「{kb[:12]}」 (gap {gap[:30]!r})")
    if n:
        f.write_text(json.dumps(resp, ensure_ascii=False, indent=2), encoding="utf-8")
        print(pid, "고친 칸", n)

side_p = ROOT / "responses" / "da0202_missing6.json"
side = json.loads(side_p.read_text(encoding="utf-8"))
for line in side["lines"]:
    before = line.get("ko") or ""
    k = before.replace("\\n", "\n")
    if line.get("src_lines", "").startswith("387"):
        k = k.replace("비교도 안 되지 말입니다!", "비교도 안 됩니다!")
    if k != before:
        write_log("da0202_missing6", line.get("src_lines"), "ko", before, k, "글자 \\n을 줄바꿈으로; 원문에 であります가 없는 줄의 말버릇 뺌")
        line["ko"] = k
side_p.write_text(json.dumps(side, ensure_ascii=False, indent=2), encoding="utf-8")
print("살펴볼 것:", len(listed))
for x in listed[:40]:
    print("  ", x)
