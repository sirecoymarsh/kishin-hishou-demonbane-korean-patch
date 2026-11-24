"""Per-lane stall watch (read-only, sends nothing): exit when a lane's agent transcript has not moved for LANE_STALL
while that lane still has packets left, or when every lane is done. Lane A = parent conversation."""
import json, os, re, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import check_tl  # noqa: E402
ROOT = check_tl.ROOT
BRAIN = Path(os.path.expanduser(r"~\.gemini\antigravity\brain"))
PARENT = "8c034aff-5e19-4d83-9bc1-5be38b5d1a60"
LANE_STALL, POLL = 720, 30

def lane_packets(name):
    return re.findall(r"(P\d{4}_[a-z0-9_]+) →", (ROOT / "lanes" / f"NEXT_{name}.txt").read_text(encoding="utf-8"))

def lane_dirs():
    found = {"A": BRAIN / PARENT}
    now = time.time()
    for b in BRAIN.iterdir():
        f = b / ".system_generated" / "logs" / "transcript.jsonl"
        try:
            if b.name != PARENT and f.exists() and now - f.stat().st_mtime < 6 * 3600:
                m = re.search(r"NEXT_([ABC])", f.open(encoding="utf-8").readline())
                if m and (m.group(1) not in found or f.stat().st_mtime > (found[m.group(1)] / ".system_generated" / "logs" / "transcript.jsonl").stat().st_mtime):
                    found[m.group(1)] = b
        except (OSError, ValueError):
            pass
    return found

t0 = time.time()
while True:
    dirs = lane_dirs()
    left = {n: [p for p in lane_packets(n) if not (ROOT / "responses" / f"{p}.json").exists()] for n in "ABC"}
    if not any(left.values()):
        print(time.strftime("%H:%M:%S"), "세 갈래 모두 응답을 다 씀"); break
    for n in "ABC":
        if not left[n]:
            continue
        d = dirs.get(n)
        if d is None:
            if time.time() - t0 > LANE_STALL:
                print(time.strftime("%H:%M:%S"), f"{n} 갈래 에이전트가 없음 — 남은 {len(left[n])}묶음"); sys.exit(0)
            continue
        age = time.time() - (d / ".system_generated" / "logs" / "transcript.jsonl").stat().st_mtime
        if age > LANE_STALL:
            print(time.strftime("%H:%M:%S"), f"{n} 갈래 멈춤({int(age // 60)}분 무진전, 대화 {d.name[:8]}) — 남은 {len(left[n])}묶음: {left[n][:3]}")
            sys.exit(0)
    time.sleep(POLL)
