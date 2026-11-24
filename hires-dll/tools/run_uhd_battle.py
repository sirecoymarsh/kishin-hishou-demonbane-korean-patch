"""4K 전투 시험 — 모니터 전체 방식(화면 밖)·소리 끔·에이전트 켬으로 FINISHERS 칸 하나를 돌려 전투 화면을 찍는다.
전투 모듈(demonbane.dll, D3D8)이 키운 창에서 어떻게 그려지는지 보려는 것.

  python -I -X utf8 run_uhd_battle.py [칸=4] [--window]
"""
from __future__ import annotations

import sys
import time

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_finisher_test as F  # noqa: E402
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
PICK = int(ARGS[0]) if ARGS else 4
FULL = "--window" not in sys.argv

POS = [1645, 741]


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
              uhd=1, uhd_mode="full" if FULL else "window", uhd_scale=2, uhd_offscreen=1, uhd_trace=1, uhd_probe=0, uhd_text=1)
    try:
        h = T3.launch()
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4)
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 431); time.sleep(5); S.shot(h, "B01_finishers")
        UA.click(h, *F.bar_center(PICK))
        for k in range(6):
            time.sleep(6)
            S.shot(h, f"B02_battle_{k}")
            if not S.running():
                break
        closed = F.watch_battle(h, f"B03_f{PICK:02d}", limit=200)
        S.log("battle closed" if closed else "battle did not close")
        time.sleep(4)
        S.shot(h, "B04_after")
        UA.quit_game(h)
    finally:
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():                                  # the game saves its (off-screen) window position at exit
            realreg.set_dword("XPos", POS[0])
            realreg.set_dword("YPos", POS[1])
            S.log(f"window position restored to {POS}")


if __name__ == "__main__":
    main()
