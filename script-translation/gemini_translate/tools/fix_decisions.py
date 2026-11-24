"""User decisions (보고서 4절) carried into the Gemini translations; every change goes to fix_log.jsonl.
Add later decisions here the same way: (packet, sid, old, new) under the decision's note.

  python -I -X utf8 fix_decisions.py
"""
import json
import time
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
LOG = ROOT / "fix_log.jsonl"
BY = "Claude Opus 5.5 (claude-opus-5-5)"
DECISIONS = {
    "사용자 11-01 결정 12 「원적으로」: 怨敵=원적": [
        ("P0117_da1316", "da1316:T00077", "나의 사랑스러운 숙적이여」", "나의 사랑스러운 원적이여」"),
        ("P0118_da1317", "da1317:T00075", "나의 숙적을 향한", "나의 원적을 향한"),
        ("P0119_da1318", "da1318:T00065", "나의 숙적이여!", "나의 원적이여!"),
    ],
    # 르뤼에 이본 주문(원문 밑글자는 중국어 「我招喚鬼械神鬼歹老海的騎魚的武士」): 밑글자 = 중국어 발음의 한글 표기(8월 1호
    # 최종형, 「셰」가 글꼴에 없어 「시에」), 루비 = 발음되는 르뤼에 주문 그대로, 해석은 루비 밖 괄호
    "사용자 11-01 「중국어 글자의 독음을 쓰고 괄호치고 해석을 달아주면」: da9903 르뤼에 이본 주문": [
        ("P0131_da9903", "da9903:T00029", "나 귀계신 크툴루의 물고기를 탄 무사를 소환하노라", "워 자오환 구이시에션, 구이다이 라오하이 더 치위 더 우스"),
        ("P0131_da9903", "da9903:T00030", "」", "(나 귀계신 크툴루의 물고기를 탄 무사를 소환하노라)」"),
    ],
    # 같은 날 바꿈: 밑글자는 발음 표기 대신 원문 중국어 그대로(16자 모두 CP932·글꼴에 있고 한글 캐리어와 안 겹침), 루비·괄호 해석은 그대로
    "사용자 11-01 「한자를 읽지말고 원본대로」: da9903 주문 밑글자 = 원문 한자": [
        ("P0131_da9903", "da9903:T00029", "워 자오환 구이시에션, 구이다이 라오하이 더 치위 더 우스", "我招喚鬼械神鬼歹老海的騎魚的武士"),
    ],
}

log = open(LOG, "a", encoding="utf-8")
cache = {}
for why, edits in DECISIONS.items():
    for pid, sid, old, new in edits:
        if pid not in cache:
            cache[pid] = json.loads((ROOT / "responses" / f"{pid}.json").read_text(encoding="utf-8"))
        rec = next(r for r in cache[pid]["records"] if r["id"] == sid)
        before = rec["ko"]
        if new in before:          # already applied; 「」」→「(…)」」 would otherwise grow again on every run
            print(f"  {sid}: 이미 반영됨 — 건너뜀")
            continue
        if before.count(old) != 1:
            print(f"  {sid}: old {old!r}가 칸에 {before.count(old)}번 — 건너뜀(이미 반영했으면 정상)")
            continue
        rec["ko"] = before.replace(old, new)
        log.write(json.dumps({"packet": pid, "id": sid, "field": "ko", "before": before, "after": rec["ko"], "why": why,
                              "by": BY, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}, ensure_ascii=False) + "\n")
        print(f"  {sid}: {old!r} -> {new!r}")
for pid, resp in cache.items():
    (ROOT / "responses" / f"{pid}.json").write_text(json.dumps(resp, ensure_ascii=False, indent=2), encoding="utf-8")
