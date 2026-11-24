"""4K 글자 화면 시험 — 모니터 전체 방식(화면 밖)·소리 끔으로 STORY CHART 1장 장면에서 본문·백로그(휠)·우클릭 메뉴를
차례로 찍는다(불러오기·환경설정은 윈도 대화상자라 run_uhd_loadprobe.py 로 따로 본다). 화면마다 PNG 를 남기고 정상 종료.

  python -I -X utf8 run_uhd_screens.py
"""
from __future__ import annotations

import sys
import time

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import run_story_test4 as R4  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

POS = [1645, 741]
WM_RBUTTONDOWN, WM_RBUTTONUP, WM_MOUSEWHEEL = 0x0204, 0x0205, 0x020A


def rclick(h, x=400, y=300):
    gx, gy, gw, gh = UA.GEOM
    cx, cy = gx + x * gw // 800, gy + y * gh // 600
    S.hover(h, cx, cy, 0.3)
    S.u.PostMessageW(h, WM_RBUTTONDOWN, 2, S.lp(cx, cy))
    time.sleep(0.08)
    S.u.PostMessageW(h, WM_RBUTTONUP, 0, S.lp(cx, cy))


def wheel(h, notches=1):
    for _ in range(abs(notches)):
        delta = 120 if notches > 0 else -120
        S.u.PostMessageW(h, WM_MOUSEWHEEL, (delta & 0xFFFF) << 16, 0)
        time.sleep(0.4)


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    POS[:] = [reg.get("XPos", 1645), reg.get("YPos", 741)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    if UA.TRACE.exists():
        UA.TRACE.unlink()
    S.set_ini(raw, enc, agent=0, advance=0, skip=0, mute=1, subtitles=0,
              uhd=1, uhd_mode="full", uhd_offscreen=1, uhd_trace=1, uhd_probe=0, uhd_text=1)
    try:
        h = T3.launch()
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4); S.shot(h, "S00_title")
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 317); time.sleep(5)
        UA.click(h, 218, 140); time.sleep(5)
        UA.click(h, *R4.card(3, 0)); time.sleep(5)
        UA.click(h, *T3.START); time.sleep(9)
        for _ in range(5):
            UA.click(h, 400, 300); time.sleep(2.5)
        S.shot(h, "S05_text")
        wheel(h, 1); time.sleep(2); S.shot(h, "S06_backlog")
        wheel(h, 3); time.sleep(2); S.shot(h, "S07_backlog_more")
        rclick(h); time.sleep(2.5); S.shot(h, "S08_after_rclick")
        rclick(h); time.sleep(2.5); S.shot(h, "S09_after_rclick2")
        rclick(h); time.sleep(2.5); S.shot(h, "S10_after_rclick3")
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
