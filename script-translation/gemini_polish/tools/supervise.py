"""Supervise the Gemini polish run: check every new/changed out file, write 되돌림 notes, report only changes.

  python -I -X utf8 supervise.py              # check only (no messages sent)
  python -I -X utf8 supervise.py --wake       # also wake the conversation when it stalls (needs the user's OK)

Wake rules (--wake): turn ended (last transcript line is a PLANNER_RESPONSE without tool calls) -> wake after 40 s;
no file activity for STALL_SEC -> wake; cooldown COOLDOWN_SEC; MAX_WAKES in a row without progress -> stop waking.
Quota (SYSTEM_MESSAGE "encountered an error" ... "Resets in 1h2m3s") -> wait until reset + 2 min, then wake once.
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import check_out  # noqa: E402

ROOT = Path(r"P:\AI\khd-argon-review\gemini_polish")
CONV = "8c034aff-5e19-4d83-9bc1-5be38b5d1a60"
BRAIN = Path(os.path.expanduser(r"~\.gemini\antigravity\brain"))
AGENTAPI = Path(os.path.expanduser(r"~\.claude\skills\antigravity-drive\agentapi.py"))
STATE = ROOT / "tools" / "supervise_state.json"
STALL_SEC, COOLDOWN_SEC, MAX_WAKES, POLL = 600, 240, 3, 60
WAKE_MSG = ("이어서 진행하라. 각 갈래는 lanes\\NEXT_*.txt 목록에서 out\\ 에 파일이 없는 묶음부터 하고, "
            "되돌림\\ 에 파일이 있는 묶음은 그 지적대로 다시 써라. 멈춘 서브에이전트가 있으면 같은 지시로 다시 띄워라. "
            "명령은 실행하지 말고 파일 읽기·쓰기만 한다.")


def all_pids():
    return sorted(p.stem for p in (ROOT / "in").glob("*.json"))


def activity():
    """Newest mtime among out/, 되돌림/ and every recent brain folder (subagents get their own)."""
    t = 0
    for d in (ROOT / "out", ROOT / "되돌림"):
        for f in d.glob("*"):
            t = max(t, f.stat().st_mtime)
    now = time.time()
    for b in BRAIN.iterdir():
        if b.is_dir() and now - b.stat().st_mtime < 6 * 3600:
            for f in (b / ".system_generated" / "logs").glob("transcript.jsonl"):
                t = max(t, f.stat().st_mtime)
    return t


def transcript_tail():
    p = BRAIN / CONV / ".system_generated" / "logs" / "transcript.jsonl"
    try:
        return [json.loads(x) for x in p.read_text(encoding="utf-8").splitlines()[-40:]]
    except Exception:  # noqa: BLE001
        return []


def quota_reset(lines):
    for o in reversed(lines):
        if o.get("type") == "SYSTEM_MESSAGE" and "encountered an error" in (o.get("content") or ""):
            m = re.search(r"Resets in (?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?", o["content"])
            if m:
                h, mi, s = (int(x or 0) for x in m.groups())
                created = time.mktime(time.strptime(o["created_at"][:19], "%Y-%m-%dT%H:%M:%S")) - time.timezone
                return created + h * 3600 + mi * 60 + s
    return None


def wake(msg):
    r = subprocess.run([sys.executable, "-I", "-X", "utf8", str(AGENTAPI), "send", CONV, msg], capture_output=True, text=True, encoding="utf-8")
    print(time.strftime("%H:%M:%S"), "깨움:", (r.stdout or r.stderr).strip()[:80], flush=True)


def main():
    do_wake = "--wake" in sys.argv
    state = json.loads(STATE.read_text(encoding="utf-8")) if STATE.exists() else {"seen": {}, "result": {}}
    wakes, last_wake, last_act = 0, 0, activity()
    while True:
        changed = []
        for f in sorted((ROOT / "out").glob("*.json")):
            m = f.stat().st_mtime
            if state["seen"].get(f.stem) != m and time.time() - m > 40:   # let the writer finish
                r = check_out.check(f.stem)
                check_out.note(r)
                state["seen"][f.stem] = m
                state["result"][f.stem] = {"edits": r["edits"], "cells": r["cells"], "keep": round(r["keep"], 3),
                                           "hard": len(r["hard"]), "soft": len(r["soft"])}
                changed.append((f.stem, r))
        if changed:
            STATE.write_text(json.dumps(state, ensure_ascii=False, indent=1), encoding="utf-8")
            done = sum(1 for v in state["result"].values() if v["hard"] == 0)
            for pid, r in changed:
                print(time.strftime("%H:%M:%S"), pid, f"편집 {r['edits']} 칸 {r['cells']} 보존 {r['keep']:.3f}",
                      "통과" if not r["hard"] else f"되돌림 {len(r['hard'])}", flush=True)
            print(f"   통과 {done}/{len(all_pids())}", flush=True)
        if all(state["result"].get(p, {}).get("hard", 1) == 0 for p in all_pids()):
            print("전부 통과", flush=True)
            return
        act = activity()
        if act > last_act:
            last_act, wakes = act, 0
        if do_wake:
            lines = transcript_tail()
            reset = quota_reset(lines)
            now = time.time()
            if reset and now < reset + 120:
                pass  # quota: wait
            elif wakes >= MAX_WAKES:
                pass  # gave up waking; human needed
            elif now - last_wake > COOLDOWN_SEC:
                ended = bool(lines) and lines[-1].get("type") == "PLANNER_RESPONSE" and not lines[-1].get("tool_calls")
                if (ended and now - last_act > 40) or now - last_act > STALL_SEC:
                    wake(WAKE_MSG)
                    last_wake, wakes = now, wakes + 1
                    if wakes >= MAX_WAKES:
                        print("세 번 깨워도 진전 없음 — 깨우기 멈춤", flush=True)
        time.sleep(POLL)


if __name__ == "__main__":
    main()
