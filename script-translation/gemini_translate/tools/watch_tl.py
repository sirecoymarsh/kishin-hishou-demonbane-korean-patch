"""Watch the translation run (read/check only, sends no messages): check each new/changed response, write 되돌림 notes,
print only changes, and exit when everything passes, when nothing moves for STALL_SEC, or after MAX_SEC.

  python -I -X utf8 watch_tl.py
"""
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import check_tl  # noqa: E402

ROOT = check_tl.ROOT
BRAIN = Path(os.path.expanduser(r"~\.gemini\antigravity\brain"))
STALL_SEC, MAX_SEC, POLL = 480, 4 * 3600, 30
TARGETS = check_tl.all_targets()
SIDE = "da0202_missing6"


def activity():
    t = 0
    for d in (ROOT / "responses", ROOT / "되돌림"):
        for f in d.glob("*"):
            t = max(t, f.stat().st_mtime)
    now = time.time()
    for b in BRAIN.iterdir():
        try:
            if b.is_dir() and now - b.stat().st_mtime < 6 * 3600:
                f = b / ".system_generated" / "logs" / "transcript.jsonl"
                if f.exists():
                    t = max(t, f.stat().st_mtime)
        except OSError:
            pass
    return t


def main():
    seen, result = {}, {}
    t0, last_act = time.time(), activity()
    print(time.strftime("%H:%M:%S"), f"감시 시작 — 대상 {len(TARGETS)}묶음 + {SIDE}", flush=True)
    while True:
        for pid in TARGETS + [SIDE]:
            f = ROOT / "responses" / f"{pid}.json"
            if not f.exists():
                continue
            m = f.stat().st_mtime
            if seen.get(pid) != m and time.time() - m > 40:
                r = check_tl.check_side() if pid == SIDE else check_tl.check(pid)
                check_tl.note(r)
                seen[pid] = m
                was = result.get(pid)
                result[pid] = len(r["hard"])
                state = "통과" if not r["hard"] else f"되돌림 {len(r['hard'])}"
                if was != result[pid]:
                    ok = sum(1 for v in result.values() if v == 0)
                    print(time.strftime("%H:%M:%S"), pid, state, f"살핌 {len(r['soft'])}", f"(통과 {ok}/{len(TARGETS) + 1})", flush=True)
        if all(result.get(p) == 0 for p in TARGETS + [SIDE]):
            print(time.strftime("%H:%M:%S"), "전부 통과", flush=True)
            return
        act = activity()
        if act > last_act:
            last_act = act
        if time.time() - last_act > STALL_SEC:
            print(time.strftime("%H:%M:%S"), f"{STALL_SEC // 60}분 동안 움직임 없음 — 멈춤",
                  "| 없음:", [p for p in TARGETS + [SIDE] if p not in result],
                  "| 되돌림:", [p for p, v in result.items() if v], flush=True)
            return
        if time.time() - t0 > MAX_SEC:
            print(time.strftime("%H:%M:%S"), "4시간 지남", flush=True)
            return
        time.sleep(POLL)


if __name__ == "__main__":
    main()
