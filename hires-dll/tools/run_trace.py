"""khd4k.dll 추적판 실행 — 소리 끔·뒤 창·창 모드에서 STORY CHART 로 장면 하나를 열어 글 몇 줄을 넘기고 정상 종료.
결과는 game\\autobattle\\khd4k_trace.txt(실행마다 지우고 새로 씀) + 찍은 화면.

  python -I -X utf8 run_trace.py
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import run_story_test4 as R4  # noqa: E402

TRACE = S.GAME / "autobattle" / "khd4k_trace.txt"


def windowed_or_quit(h) -> bool:
    """테두리 없는 800×600 창이 (0,0) 에 있으면 전체 화면 — 바로 정상 종료한다."""
    l, t, r, b = R4.win_rect(h)
    if (r - l, b - t) == (800, 600) and (l, t) == (0, 0):
        S.log("the game came up FULL SCREEN — closing it at once")
        S.u.PostMessageW(h, 0x0010, 0, 0)
        time.sleep(1.5)
        S.dialog_yes(timeout=5)
        return False
    S.log(f"windowed: window {R4.win_rect(h)}")
    return True


def quit_game(h):
    S.u.PostMessageW(h, 0x0010, 0, 0)          # WM_CLOSE
    time.sleep(1.5)
    S.dialog_yes(timeout=6)
    for _ in range(40):
        if not S.running():
            S.log("game exited normally")
            return
        time.sleep(0.5)
    S.log("game still running after the close request")


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    if R4.reg_read()["FullScreen"][0]:
        sys.exit("사본이 전체 화면 설정이라 띄우지 않는다")
    raw, enc = S.read_ini()
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    if TRACE.exists():
        TRACE.unlink()
    S.set_ini(raw, enc, agent=0, advance=0, skip=0, mute=1, subtitles=0, uhd=int(sys.argv[1]) if len(sys.argv) > 1 else 1)
    try:
        h = T3.launch()
        S.to_back(h)
        time.sleep(3)
        if not windowed_or_quit(h):
            return
        time.sleep(8)
        S.to_back(h)
        R4.gclick(h, 400, 500); time.sleep(4)
        R4.gclick(h, 405, 416); time.sleep(4)
        R4.gclick(h, 400, 317); time.sleep(5); S.shot(h, "K01_chart")
        R4.gclick(h, 218, 140); time.sleep(5); S.shot(h, "K02_c1")
        R4.gclick(h, *R4.card(3, 0)); time.sleep(5); S.shot(h, "K03_detail")
        R4.gclick(h, *T3.START); time.sleep(9); S.shot(h, "K04_scene")
        for k in range(6):
            R4.gclick(h, 400, 300); time.sleep(2.5)
            if k % 2 == 1:
                S.shot(h, f"K05_text_{k}")
        quit_game(h)
    finally:
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")


if __name__ == "__main__":
    main()
