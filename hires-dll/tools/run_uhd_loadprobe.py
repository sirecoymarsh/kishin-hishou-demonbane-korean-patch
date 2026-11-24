"""4K 불러오기 확인 — 제목 화면에서 「불러오기」를 누른 뒤 게임 프로세스의 최상위 창과 대화상자 안 컨트롤을 적는다
(4K 화면에서 불러오기가 엔진 화면인지 윈도 대화상자인지 보려는 것). 대화상자는 취소(IDCANCEL)로 닫고 정상 종료.

  python -I -X utf8 run_uhd_loadprobe.py [메뉴 y=380]
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
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")] if __name__ == "__main__" else []   # also imported
MENU_Y = int(ARGS[0]) if ARGS else 380
POS = [1645, 741]
PROC = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)


def text_of(h):
    buf = ctypes.create_unicode_buffer(256)
    u.GetWindowTextW(h, buf, 256)
    return buf.value


def class_of(h):
    buf = ctypes.create_unicode_buffer(128)
    u.GetClassNameW(h, buf, 128)
    return buf.value


def rect_of(h):
    r = W.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def game_tops(pids):
    out = []

    def each(h, _):
        pid = W.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value in pids:
            out.append(h)
        return True

    u.EnumWindows(PROC(each), 0)
    return out


def kids(h):
    out = []

    def each(c, _):
        out.append(c)
        return True

    u.EnumChildWindows(h, PROC(each), 0)
    return out


def report(tag, pids):
    dialogs = []
    for h in game_tops(pids):
        vis = bool(u.IsWindowVisible(h))
        if not vis:
            continue
        S.log(f"{tag}: top {class_of(h)!r} {text_of(h)!r} at {rect_of(h)}")
        if class_of(h) == "#32770":
            dialogs.append(h)
            for c in kids(h):
                S.log(f"{tag}:    {class_of(c)!r} id {u.GetDlgCtrlID(c)} {text_of(c)!r} at {rect_of(c)}")
    return dialogs


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    POS[:] = [reg.get("XPos", 1645), reg.get("YPos", 741)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    S.set_ini(raw, enc, agent=0, advance=0, skip=0, mute=1, subtitles=0,
              uhd=1, uhd_mode="full", uhd_offscreen=1, uhd_trace=0, uhd_probe=0, uhd_text=1)
    try:
        h = T3.launch()
        pids = {p for _h, p in S.game_windows()}
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4)
        report("title", pids)
        UA.click(h, 405, MENU_Y); time.sleep(4)
        S.shot(h, "L01_after_menu_click")
        dialogs = report("after click", pids)
        for d in dialogs:
            u.PostMessageW(d, 0x0111, 2, 0)              # WM_COMMAND IDCANCEL
            S.log("dialog cancelled")
        time.sleep(3)
        report("after cancel", pids)
        S.shot(h, "L02_after_cancel")
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
