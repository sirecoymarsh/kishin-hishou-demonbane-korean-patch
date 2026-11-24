"""4K A 단계 시험 — 창 방식·배율 2(1600×1200)·화면 밖·소리 끔으로 띄워 STORY CHART → 1장 04 → START → 글 몇 줄 → 정상 종료.
엔진은 800×600 으로 그리고 khd4k 가 키워 그린다. 클릭은 키운 좌표로 보낸다(khd4k 가 800×600 으로 되돌림).

  python -I -X utf8 run_uhd_a.py [배율=2]
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import realreg  # noqa: E402
import run_story_test4 as R4  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")] if __name__ == "__main__" else []   # also imported by other tests
SCALE = int(ARGS[0]) if ARGS else 2
PROBE = "--probe" in sys.argv
FULL = "--full" in sys.argv
TEXT = 0 if "--notext" in sys.argv else 1
GEOM = [0, 0, 800 * SCALE, 600 * SCALE]          # image x, y, w, h in the client (set after launch)
TRACE = S.GAME / "autobattle" / "khd4k_trace.txt"


def click(h, x, y):
    gx, gy, gw, gh = GEOM
    S.click(h, gx + x * gw // 800, gy + y * gh // 600)


def layout(h):
    import ctypes.wintypes as W
    import ctypes
    r = W.RECT()
    S.u.GetClientRect(h, ctypes.byref(r))
    cw, ch = r.right, r.bottom
    if cw * 3 >= ch * 4:
        ih, iw = ch, ch * 4 // 3
    else:
        iw, ih = cw, cw * 3 // 4
    GEOM[:] = [(cw - iw) // 2, (ch - ih) // 2, iw, ih]
    S.log(f"client {cw}x{ch}, image at ({GEOM[0]},{GEOM[1]}) {iw}x{ih}")


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

POS = [1645, 741]


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    POS[:] = [reg.get("XPos", 1645), reg.get("YPos", 741)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    if TRACE.exists():
        TRACE.unlink()
    S.set_ini(raw, enc, agent=0, advance=0, skip=0, mute=1, subtitles=0,
              uhd=1, uhd_mode="full" if FULL else "window", uhd_scale=SCALE, uhd_offscreen=1, uhd_trace=1,
              uhd_probe=1 if PROBE else 0, uhd_text=TEXT)
    try:
        h = T3.launch()
        time.sleep(3)
        l, t, r, b = R4.win_rect(h)
        S.log(f"window {l},{t} {r - l}x{b - t}")
        layout(h)
        time.sleep(8)
        click(h, 400, 500); time.sleep(4); S.shot(h, "A00_title_menu")
        click(h, 405, 416); time.sleep(4)
        click(h, 400, 317); time.sleep(5); S.shot(h, "A01_chart")
        click(h, 218, 140); time.sleep(5); S.shot(h, "A02_c1")
        click(h, *R4.card(3, 0)); time.sleep(5); S.shot(h, "A03_detail")
        click(h, *T3.START); time.sleep(9); S.shot(h, "A04_scene")
        for k in range(4):
            click(h, 400, 300); time.sleep(2.5)
        S.shot(h, "A05_text")
        quit_game(h)
    finally:
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():                                  # the game saves its (off-screen) window position at exit
            realreg.set_dword("XPos", POS[0])
            realreg.set_dword("YPos", POS[1])
            S.log(f"window position restored to {POS}")


if __name__ == "__main__":
    main()
