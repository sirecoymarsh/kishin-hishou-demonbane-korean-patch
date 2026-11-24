"""Emphasis-dot rubies (傍点: the JP reading is only ・ and spaces). Gemini wrote six such readings as words, so the game
would print the base phrase twice (above and in the line); they go back to dots, one per syllable of the Korean base
(the corrected text mostly does the same). Three bases lost their word spaces (Gemini squeezed them to fit the dot
count) and two endings broke («있으니까다», imperative + 라고). Every change goes to fix_log.jsonl.

  python -I -X utf8 fix_dots.py
"""
import json
import time
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
LOG = ROOT / "fix_log.jsonl"
BY = "Claude Opus 5.5 (claude-opus-5-5)"
DOTS = "강조점 루비(원문 읽기가 점뿐)를 글자로 써서 밑글자가 두 번 보임 — 점으로(밑글자 음절 수)"
HAND = {  # packet: {sid: (old, new, why)}
    "P0121_da1320": {
        "da1320:R00009": ("우리", "・・", DOTS),
        "da1320:R00016": ("미래영겁, 계속 패배해라", "・・・・\u3000・・・・・・", DOTS),
        "da1320:T00084": ("계속 패배해라", "계속 패배하라", "명령형 인용 「패배해라라고」 → 「패배하라고」"),
        "da1320:T00085": ("라고」", "고」", "명령형 인용 「패배해라라고」 → 「패배하라고」"),
        "da1320:R00020": ("차례를 마친", "・・・・・", DOTS),
        "da1320:R00021": ("배우는", "・・・", DOTS),
        "da1320:R00022": ("무대에서", "・・・・", DOTS),
        "da1320:R00023": ("내려와야겠지", "・・・・・・", DOTS),
    },
    "P0119_da1318": {
        "da1318:T00021": ("무너지고있다", "무너지고 있다", "강조점 밑글자의 띄어쓰기를 되살림"),
        "da1318:T00038": ("가장새롭고오래된신", "가장 새롭고 오래된 신", "강조점 밑글자의 띄어쓰기를 되살림"),
    },
    "P0118_da1317": {
        "da1317:T00088": ("대체어떻게", "대체 어떻게", "강조점 밑글자의 띄어쓰기를 되살림"),
    },
    "P0113_da1313": {
        "da1313:T00204": ("믿을 수 있으니까다", "믿을 수 있는 거다", "어미가 깨짐(信じられるんだよ)"),
    },
}

log = open(LOG, "a", encoding="utf-8")
for pid, fixes in HAND.items():
    f = ROOT / "responses" / f"{pid}.json"
    resp = json.loads(f.read_text(encoding="utf-8"))
    rec = {r["id"]: r for r in resp["records"]}
    n = 0
    for sid, (old, new, why) in fixes.items():
        before = rec[sid]["ko"]
        if before.count(old) != 1:
            print(f"  {sid}: old {old!r}가 칸에 {before.count(old)}번 — 건너뜀")
            continue
        after = before.replace(old, new)
        log.write(json.dumps({"packet": pid, "id": sid, "field": "ko", "before": before, "after": after, "why": why,
                              "by": BY, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}, ensure_ascii=False) + "\n")
        rec[sid]["ko"] = after
        n += 1
    if n:
        f.write_text(json.dumps(resp, ensure_ascii=False, indent=2), encoding="utf-8")
    print(pid, "고친 칸", n)
