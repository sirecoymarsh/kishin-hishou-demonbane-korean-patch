"""d3d8to9 로딩 멈춤 조사 — 사본에 d3d8to9(d3d8.dll)를 잠깐 넣고 FINISHERS 칸 하나(또는 새 게임 첫 전투)를 화면 밖·소리 끔으로
띄워, 로딩 중 스레드 위치(threads32.py)와 khd4k 기록을 남긴다. 끝나면 정상 종료를 시도하고, 안 꺼지면(멈춘 시험 프로세스)
강제 종료한다. d3d8.dll·ini·창 위치는 시작 전으로 되돌린다.

  python -I -X utf8 run_d3d8to9_probe.py [칸=10 | newgame] [기다릴 초=30] [--keep-d3d8]
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_finisher_test as F  # noqa: E402
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
WHAT = ARGS[0] if ARGS else "10"
WAIT = float(ARGS[1]) if len(ARGS) > 1 else 30
KEEP = "--keep-d3d8" in sys.argv
D3D8 = S.GAME / "d3d8.dll"
D3D8_OFF = S.GAME / "d3d8.dll.d3d8to9_off"
OUT = Path(r"P:\AI\khd-4k\tmp")


def sample(pid: int, tag: str):
    r = subprocess.run([sys.executable, "-I", "-X", "utf8", str(Path(__file__).with_name("threads32.py")), str(pid)],
                       capture_output=True)
    text = r.stdout.decode("utf-8", "replace") + r.stderr.decode("utf-8", "replace")
    (OUT / f"threads_{tag}.txt").write_text(text, encoding="utf-8")
    S.log(f"threads saved: threads_{tag}.txt ({len(text.splitlines())} lines)")


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    if D3D8.exists():
        sys.exit("d3d8.dll 이 이미 있다 — 손대지 않는다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    pos = [reg.get("XPos", 0), reg.get("YPos", 0)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    if UA.TRACE.exists():
        UA.TRACE.unlink()
    shutil.copy2(D3D8_OFF, D3D8)
    S.log("d3d8to9 in place")
    S.set_ini(raw, enc, agent=1, background=1, advance=1 if WHAT == "newgame" else 0, skip=1 if WHAT == "newgame" else 0,
              mute=1, subtitles=0, uhd=1, uhd_mode="full", uhd_offscreen=1, uhd_trace=1, uhd_probe=0, uhd_text=1,
              uhd_battle=1, uhd_capture="", uhd_envfix=0)
    try:
        h = T3.launch()
        pid = S.game_windows()[0][1]
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4)
        if WHAT == "newgame":
            UA.click(h, 405, 344); time.sleep(3)
            S.dialog_yes(timeout=3)
        else:
            UA.click(h, 405, 416); time.sleep(4)
            UA.click(h, 400, 431); time.sleep(6)
            UA.click(h, *F.bar_center(int(WHAT)))
        t0 = time.time()
        while time.time() - t0 < WAIT:
            time.sleep(5)
            S.log(f"{time.time() - t0:.0f}s")
        sample(pid, f"{WHAT}_a")
        time.sleep(5)
        sample(pid, f"{WHAT}_b")
        S.u.PostMessageW(h, 0x0010, 0, 0)
        time.sleep(2)
        S.dialog_yes(timeout=5)
        for _ in range(40):
            if not S.running():
                S.log("game exited normally")
                break
            time.sleep(0.5)
        else:
            sample(pid, f"{WHAT}_close")
            S.log("game did not exit - the stuck test process is ended")
            subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
            time.sleep(3)
    finally:
        if not KEEP and D3D8.exists() and not S.running():
            D3D8.unlink()
            S.log("d3d8.dll removed again")
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():
            realreg.set_dword("XPos", pos[0])
            realreg.set_dword("YPos", pos[1])
            S.log(f"window position restored to {pos}")
        shutil.copy2(UA.TRACE, OUT / f"trace_d3d8to9_{WHAT}.txt") if UA.TRACE.exists() else None


if __name__ == "__main__":
    main()
