"""4K 영상 시험 — 모니터 전체 방식(화면 밖)·소리 끔으로 감상실의 영상 6을 틀어, 영상 창(DirectShow 자식 창)이
4:3 그림 자리에 맞게 놓였는지 창 좌표로 확인한다(영상 화면 자체는 PrintWindow 로 안 찍힌다). 정상 종료.

  python -I -X utf8 run_uhd_movie.py
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import sys
import time

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

u = ctypes.windll.user32
POS = [1645, 741]


def children(h):
    out = []
    PROC = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def each(c, _):
        name = ctypes.create_unicode_buffer(128)
        u.GetClassNameW(c, name, 128)
        r = W.RECT()
        u.GetWindowRect(c, ctypes.byref(r))
        pts = (W.POINT * 2)(W.POINT(r.left, r.top), W.POINT(r.right, r.bottom))
        u.MapWindowPoints(None, h, pts, 2)
        out.append((name.value, pts[0].x, pts[0].y, pts[1].x - pts[0].x, pts[1].y - pts[0].y, bool(u.IsWindowVisible(c))))
        return True

    u.EnumChildWindows(h, PROC(each), 0)
    return out


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
        UA.click(h, 400, 500); time.sleep(4)
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 374); time.sleep(4); S.shot(h, "M01_theater")
        UA.click(h, 616, 339); time.sleep(4)
        S.log(f"image rectangle {UA.GEOM}")
        for k in range(3):
            for c in children(h):
                S.log(f"child {c[0]!r} at ({c[1]},{c[2]}) {c[3]}x{c[4]} visible {c[5]}")
            S.shot(h, f"M02_movie_{k}")
            time.sleep(3)
        UA.click(h, 400, 300); time.sleep(4); S.shot(h, "M03_after_skip")
        for c in children(h):
            S.log(f"after: child {c[0]!r} at ({c[1]},{c[2]}) {c[3]}x{c[4]} visible {c[5]}")
        UA.click(h, 722, 554); time.sleep(4)
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
