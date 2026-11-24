"""Mechanical fixes on Gemini responses (kit format khd-gemini-response-v1); every change goes to fix_log.jsonl.

- source_sha256 back to the packet's value
- ruby readings marked omit_redundant: ko "" (the kit rule) and an issue {id, kind, note}
- ruby readings left null: hiragana-only reading -> omit_redundant + issue; anything else is listed for hand fixing
- issues normalised to {id, kind, note} (Gemini wrote category/reason)
- model_id from external evidence (the conversation's Model Selection), not the agent's self-report

  python -I -X utf8 fix_structure.py
"""
import json
import re
import time
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
MODEL = "Gemini 3.8 Flash (High)"  # transcript 8c034aff step 40: Model Selection -> Gemini 3.8 Flash (High)
LOG = ROOT / "fix_log.jsonl"
HIRA = re.compile(r"^[\u3041-\u309f・ー]+$")

log = open(LOG, "a", encoding="utf-8")
manual = []
for f in sorted((ROOT / "responses").glob("P*.json")):
    pid = f.stem
    pkt = json.loads((PKG / "packets" / f"{pid}.json").read_text(encoding="utf-8"))
    seg = {s["id"]: s for u in pkt["units"] for s in u["segments"]}
    known = set(seg) | {u["id"] for u in pkt["units"]}
    r = json.loads(f.read_text(encoding="utf-8-sig"))
    changes = []

    def ch(sid, field, before, after, why):
        changes.append({"packet": pid, "id": sid, "field": field, "before": before, "after": after, "why": why,
                        "by": "Claude Opus 5.5 (claude-opus-5-5)", "at": time.strftime("%Y-%m-%dT%H:%M:%S")})

    if r.get("model_id") != MODEL:
        ch(pid, "model_id", r.get("model_id"), MODEL, "자기 신고 대신 대화 기록의 모델 설정")
        r["model_id"] = MODEL
    issues = []
    for q in r.get("issues", []):
        if not isinstance(q, dict):
            continue
        nq = {"id": q.get("id"), "kind": q.get("kind") or q.get("category") or "note",
              "note": q.get("note") or q.get("reason") or q.get("detail") or ""}
        if nq != q:
            ch(q.get("id"), "issue", q, nq, "issue 칸 이름을 양식(id·kind·note)으로")
        if nq["id"] in known:
            issues.append(nq)
        else:
            ch(q.get("id"), "issue", nq, None, "없는 ID를 가리키는 issue 삭제")
    have = {q["id"] for q in issues}
    for rec in r["records"]:
        s = seg.get(rec["id"])
        if s is None:
            continue
        if rec.get("source_sha256") != s["source_sha256"]:
            ch(rec["id"], "source_sha256", rec.get("source_sha256"), s["source_sha256"], "원문 해시는 바꾸지 않는다")
            rec["source_sha256"] = s["source_sha256"]
        if s["kind"] == "ruby_reading":
            if rec.get("ko") is None and rec.get("ruby_action") != "omit_redundant":
                if HIRA.match(s["jp"]):
                    ch(rec["id"], "ruby_action", None, "omit_redundant", f"히라가나 독음 {s['jp']} — 생략")
                    rec["ruby_action"] = "omit_redundant"
                else:
                    manual.append((rec["id"], s["jp"]))
            if rec.get("ruby_action") == "omit_redundant":
                if rec.get("ko") != "":
                    ch(rec["id"], "ko", rec.get("ko"), "", "omit_redundant 읽기 칸은 빈 값")
                    rec["ko"] = ""
                if rec["id"] not in have:
                    nq = {"id": rec["id"], "kind": "ruby_decision", "note": f"일본어 독음 {s['jp']}이 한국어 본문과 겹쳐 읽기 줄을 생략"}
                    ch(rec["id"], "issue", None, nq, "omit_redundant에는 issue가 있어야 한다")
                    issues.append(nq)
                    have.add(rec["id"])
    r["issues"] = issues
    if changes:
        f.write_text(json.dumps(r, ensure_ascii=False, indent=2), encoding="utf-8")
        for c in changes:
            log.write(json.dumps(c, ensure_ascii=False) + "\n")
    kinds = {}
    for c in changes:
        kinds[c["field"]] = kinds.get(c["field"], 0) + 1
    if changes:
        print(pid, kinds)
print("손으로 고칠 루비 칸:", manual)
