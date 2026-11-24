"""4K 전투 정밀 시험 — FINISHERS 칸 하나를 모니터 전체 방식(화면 밖)·소리 끔으로 돌리며 1초마다 화면을 찍고,
찍을 때마다 khd4k 기록에서 마지막 프레임 덤프 번호를 같이 적는다(어떤 그리기가 어떤 화면을 만들었는지 맞춰 보려고).

  python -I -X utf8 run_uhd_battle2.py [칸=4] [초=45] [--stageA]
"""
from __future__ import annotations

import re
import sys
import time

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_finisher_test as F  # noqa: E402
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
PICK = int(ARGS[0]) if ARGS else 4
SECONDS = float(ARGS[1]) if len(ARGS) > 1 else 45
STAGE_A = "--stageA" in sys.argv
POS = [1645, 741]


def last_frame() -> str:
    try:
        tail = UA.TRACE.read_bytes()[-200000:].decode("utf-8", "replace")
    except OSError:
        return "?"
    m = re.findall(r"---- battle frame (\d+)", tail)
    return m[-1] if m else "-"


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    POS[:] = [reg.get("XPos", 1645), reg.get("YPos", 741)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    if UA.TRACE.exists():
        UA.TRACE.unlink()
    S.set_ini(raw, enc, agent=1, advance=0, skip=0, mute=1, subtitles=0,
              uhd=1, uhd_mode="full", uhd_offscreen=1, uhd_trace=1, uhd_probe=0, uhd_text=1,
              uhd_battle=0 if STAGE_A else 1)
    try:
        h = T3.launch()
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4)
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 431); time.sleep(6)
        UA.click(h, *F.bar_center(PICK))
        t0 = time.time()
        k = 0
        while time.time() - t0 < SECONDS and S.running():
            time.sleep(1.0)
            k += 1
            S.shot(h, f"C{k:02d}")
            S.log(f"shot C{k:02d} after frame dump {last_frame()}")
        time.sleep(3)
        UA.quit_game(h)
    finally:
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():
            realreg.set_dword("XPos", POS[0])
            realreg.set_dword("YPos", POS[1])
            S.log(f"window position restored to {POS}")


if __name__ == "__main__":
    main()
