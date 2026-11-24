"""Hand fixes after the mechanical scan against the corrected text (11-01): Japanese kana left in Korean (ッ, ぁ), emphasis
「」 in narration turned into '', full-width ！？ (the corrected text writes !?; Weightley's full-width Latin chants keep
theirs), and ruby readings that only repeat the Korean base. ッ follows the corrected text: a grunt syllable when it
stretches a vowel (앗·엇·옷), otherwise dropped or !. Every change goes to fix_log.jsonl.

  python -I -X utf8 fix_kana.py
"""
import json
import time
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
LOG = ROOT / "fix_log.jsonl"
BY = "Claude Opus 5.5 (claude-opus-5-5)"
KANA = "일본어 가나(ッ·ぁ)가 남음 — 교정본처럼 늘임소리나 부호로"
QUOTE = "지문의 강조 「」를 작은따옴표로 바꿈 — 원문 괄호대로"
WIDE = "전각 ！？ — 교정본처럼 반각"
FIX = {  # packet: {sid: [(old, new, why)]}
    "P0118_da1317": {
        "da1317:T00055": [("꼴사납다ッ!", "꼴사납다앗!", KANA)],
        "da1317:T00056": [("극치다ッッッッ!", "극치다아아아앗!", KANA)],
        "da1317:T00073": [("――――ッ!」", "――――엇!」", KANA)],
        "da1317:T00115": [("ッッッッ!」", "!!」", KANA)],
        "da1317:T00186": [("거냐ッ!?", "거냐!?", KANA), ("쿠자쿠ッ!」", "쿠자쿠!」", KANA)],
        "da1317:T00009": [("？", "?", WIDE)],
    },
    "P0119_da1318": {
        "da1318:T00041": [("데몬베인ッッ!", "데몬베인!!", KANA)],
        "da1318:T00043": [("어어어ッッッ!」", "어어어엇!」", KANA)],
        "da1318:T00022": [("？」", "?」", WIDE)],
    },
    "P0120_da1319": {"da1319:T00031": [("「……ぁ…………」", "「……아…………」", KANA)]},
    "P0127_da1505": {
        "da1505:T00013": [("아범ッ」", "아범!」", KANA)],
        "da1505:T00032": [("계집애……ッ!」", "계집애……!」", KANA)],
        "da1505:T00039": [("계집애야아ッ!」", "계집애야아앗!」", KANA)],
    },
    "P0128_da9901": {
        "da9901:T00073": [("――――ッ!?」", "――――앗!?」", KANA)],
        "da9901:T00074": [("――――ッ!」", "――――옷!」", KANA)],
        "da9901:T00080": [("했느냐ッ」", "했느냐!」", KANA)],
        "da9901:T00110": [("말이냐ッ」", "말이냐!」", KANA)],
    },
    "P0131_da9903": {"da9903:T00038": [("ッ!」", "!」", KANA)]},
    "P0113_da1313": {
        "da1313:T00050": [("'톱니'바퀴", "「톱니」바퀴", QUOTE), ("'이빨'이", "「이빨」이", QUOTE)],
        "da1313:T00123": [("'지금'이야말로", "「지금」이야말로", QUOTE)],
    },
    "P0063_da0601": {"da0601:T00197": [("！」", "!」", WIDE)], "da0601:T00219": [("！」", "!」", WIDE)]},
    "P0064_da0602": {"da0602:T00011": [("！」", "!」", WIDE)], "da0602:T00016": [("！」", "!」", WIDE)]},
    "P0068_da0606": {"da0606:T00050": [("？", "?", WIDE), ("！", "!", WIDE)]},
}
# readings that only repeat the base: kit rule — ko "", ruby_action omit_redundant, and an issue
OMIT = {
    "P0127_da1505": {"da1505:R00001": "ヤマ(사건을 이르는 은어)의 한국어 읽기가 밑글자 「사건」과 같아 두 번 보이므로 읽기 줄 생략"},
    "P0128_da9901": {
        "da9901:R00008": "サンドマン 다섯 칸 읽기를 앞 세 칸 「샌드맨」에 담고, 밑글자 「사양」을 되풀이하던 남은 두 칸은 생략",
        "da9901:R00009": "サンドマン 다섯 칸 읽기를 앞 세 칸 「샌드맨」에 담고, 밑글자 「사양」을 되풀이하던 남은 두 칸은 생략",
    },
}

log = open(LOG, "a", encoding="utf-8")


def write_log(pid, sid, field, before, after, why):
    log.write(json.dumps({"packet": pid, "id": sid, "field": field, "before": before, "after": after, "why": why,
                          "by": BY, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}, ensure_ascii=False) + "\n")


for pid in sorted(set(FIX) | set(OMIT)):
    f = ROOT / "responses" / f"{pid}.json"
    resp = json.loads(f.read_text(encoding="utf-8"))
    rec = {r["id"]: r for r in resp["records"]}
    n = 0
    for sid, edits in FIX.get(pid, {}).items():
        k = rec[sid]["ko"]
        before = k
        for old, new, why in edits:
            if k.count(old) != 1:
                print(f"  {sid}: old {old!r}가 칸에 {k.count(old)}번 — 건너뜀")
                continue
            k = k.replace(old, new)
        if k != before:
            write_log(pid, sid, "ko", before, k, "; ".join(sorted({w for _, _, w in edits})))
            rec[sid]["ko"] = k
            n += 1
    for sid, note in OMIT.get(pid, {}).items():
        r = rec[sid]
        if r.get("ko") != "" or r.get("ruby_action") != "omit_redundant":
            write_log(pid, sid, "ko", r.get("ko"), "", "루비 읽기가 밑글자와 같음 — 읽기 줄 생략(omit_redundant)")
            r["ko"] = ""
            r["ruby_action"] = "omit_redundant"
            n += 1
        if not any(q.get("id") == sid for q in resp.get("issues", [])):
            q = {"id": sid, "kind": "ruby_decision", "note": note}
            resp.setdefault("issues", []).append(q)
            write_log(pid, sid, "issue", None, q, "omit_redundant에는 issue가 있어야 한다")
    if n:
        f.write_text(json.dumps(resp, ensure_ascii=False, indent=2), encoding="utf-8")
    print(pid, "고친 칸", n)
