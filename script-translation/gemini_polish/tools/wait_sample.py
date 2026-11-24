"""Wait (read-only) until both sample outputs exist and are stable, or Gemini ends its turn, or 40 min pass."""
import json, os, time
OUT = r"P:\AI\khd-argon-review\gemini_polish\out"
import sys
WANT = [a + ".json" for a in sys.argv[1:]] or ["P0002_da0101.json", "P0031_da0301.json"]
TR = os.path.expanduser(r"~\.gemini\antigravity\brain\8c034aff-5e19-4d83-9bc1-5be38b5d1a60\.system_generated\logs\transcript.jsonl")
t0 = time.time(); start_lines = len(open(TR, encoding="utf-8").read().splitlines())
last_sig = None; stable_since = None
while time.time() - t0 < 2400:
    have = [w for w in WANT if os.path.exists(os.path.join(OUT, w))]
    sig = tuple((w, os.path.getmtime(os.path.join(OUT, w))) for w in have)
    if sig != last_sig:
        last_sig, stable_since = sig, time.time()
    lines = open(TR, encoding="utf-8").read().splitlines()
    ended = False
    if len(lines) > start_lines + 2:
        o = json.loads(lines[-1])
        ended = o.get("type") == "PLANNER_RESPONSE" and not o.get("tool_calls") and o.get("status") == "DONE"
    if len(have) == len(WANT) and time.time() - stable_since > 40:
        print("둘 다 씀", time.strftime("%H:%M:%S")); break
    if ended and time.time() - stable_since > 40:
        print("제미나이가 차례를 끝냄", time.strftime("%H:%M:%S"), "결과:", have, "| 마지막 말:", (o.get("content") or "")[:300]); break
    time.sleep(20)
else:
    print("40분 지남", have)
print("대화 기록 줄", len(lines), "(시작 때", start_lines, ")")
