"""4K 전투 뒤 클릭 시험 — 사용자 10-16 「피니셔를 보고 돌아오니까 게임에 마우스 클릭이 안 먹혀」.
모니터 전체 방식(화면 밖)·소리 끔·사용자처럼 agent=1·background=1 로 FINISHERS 칸 하나를 보고 돌아온 뒤,
창 처리 함수가 어느 모듈 것인지 적고 EXIT 를 눌러 화면이 바뀌는지(감상실로 돌아가는지) 본다. 정상 종료.

  python -I -X utf8 run_uhd_postbattle.py [칸=4]
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import sys
import time

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_finisher_test as F  # noqa: E402
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

from PIL import ImageChops, ImageStat  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
PICK = int(ARGS[0]) if ARGS else 4
u = ctypes.windll.user32
k32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi


def proc_owner(h) -> str:
    """창 처리 함수 주소가 든 모듈 이름(게임 프로세스 안)."""
    proc = u.GetWindowLongA(h, -4) & 0xFFFFFFFF
    pid = W.DWORD()
    u.GetWindowThreadProcessId(h, ctypes.byref(pid))
    hp = k32.OpenProcess(0x0410, False, pid.value)
    mods = (ctypes.c_void_p * 512)()
    need = W.DWORD()
    psapi.EnumProcessModulesEx(hp, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
    owner = "?"
    for m in mods[: need.value // ctypes.sizeof(ctypes.c_void_p)]:
        info = (ctypes.c_uint32 * 3)()
        psapi.GetModuleInformation(hp, ctypes.c_void_p(m), info, 12)
        if info[0] <= proc < info[0] + info[1]:
            name = ctypes.create_unicode_buffer(260)
            psapi.GetModuleBaseNameW(hp, ctypes.c_void_p(m), name, 260)
            owner = name.value
            break
    k32.CloseHandle(hp)
    return f"{proc:#x} in {owner}"


def differs(a, b) -> float:
    d = ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
    return sum(ImageStat.Stat(d).mean) / 3


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    pos = [reg.get("XPos", 1645), reg.get("YPos", 741)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    S.set_ini(raw, enc, agent=1, background=1, advance=0, skip=0, mute=1, subtitles=0,
              uhd=1, uhd_mode="full", uhd_offscreen=1, uhd_trace=0, uhd_probe=0, uhd_text=1, uhd_battle=1)
    try:
        h = T3.launch()
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        S.log(f"window procedure at start: {proc_owner(h)}")
        UA.click(h, 400, 500); time.sleep(4)
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 431); time.sleep(6); S.shot(h, "P01_finishers")
        UA.click(h, *F.bar_center(PICK)); time.sleep(8)
        S.log(f"window procedure in the fight: {proc_owner(h)}")
        closed = F.watch_battle(h, f"P02_f{PICK:02d}", limit=200)
        S.log("battle closed" if closed else "battle did not close")
        time.sleep(5)
        S.log(f"window procedure after the fight: {proc_owner(h)}")
        before = S.shot(h, "P03_back_on_finishers")
        UA.click(h, 722, 554); time.sleep(5)                       # EXIT
        after = S.shot(h, "P04_after_exit_click")
        d = differs(before, after)
        S.log(f"EXIT click {'WORKED' if d > 8 else 'did NOT change the screen'} (mean difference {d:.1f})")
        UA.quit_game(h)
    finally:
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():
            realreg.set_dword("XPos", pos[0])
            realreg.set_dword("YPos", pos[1])
            S.log(f"window position restored to {pos}")


if __name__ == "__main__":
    main()
