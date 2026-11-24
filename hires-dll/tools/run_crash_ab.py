"""튕김 A/B — 10-16 00:11 시험 끝에서 난 튕김(system.dll+0x1b49a 접근 위반)이 4K 탓인지 가린다.

같은 순서를 4K 켬(A)·끔(B)으로 돌린다: 제목 → 감상실 → STORY CHART → 1장 → 카드 → START → 글 5번 →
휠 위 1·3 → 우클릭 3번(메뉴가 열린 채) → 창 닫기(WM_CLOSE) → 확인창 첫 단추(BM_CLICK).
확인창은 누르기 전에 단추 글자를 적는다. 판마다 시작 뒤 생긴 Application Error(이벤트 로그)와 덤프 파일로 튕김을 센다.
소리 끔·화면 밖(4K 켬은 uhd_offscreen, 끔은 레지스트리 XPos=20000 으로 띄움). 끝나면 ini·XPos/YPos 를 되돌린다.

  python -I -X utf8 run_crash_ab.py [AB|A|B|BA=AB] [판 수=1] [--trace] [--orig]   (--trace: A 판에 uhd_trace=1, 00:11 판과 똑같이)
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import datetime as dt
import subprocess
import sys
import time

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import run_story_test4 as R4  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402
from run_uhd_screens import rclick, wheel  # noqa: E402
from run_uhd_loadprobe import class_of, kids, rect_of, text_of  # noqa: E402

u = ctypes.windll.user32
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
ORDER = (ARGS[0] if ARGS else "AB").upper()
ROUNDS = int(ARGS[1]) if len(ARGS) > 1 else 1
TRACE = 1 if "--trace" in sys.argv else 0
ORIG = "--orig" in sys.argv     # 00:11 판 그대로: 제목에서 불러오기(모달 창이 남음)·환경설정 클릭과 우클릭을 먼저


def crashes_since(t0: dt.datetime) -> list[str]:
    """Application Error(1000) 중 KishinHisho_chs.exe 것 — 시간과 오류 모듈·위치."""
    since = t0.strftime("%Y-%m-%dT%H:%M:%S")
    ps = ("$t=[datetime]'" + since + "'; Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000; StartTime=$t} "
          "-ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'KishinHisho' } | ForEach-Object { "
          "$m=$_.Message -replace '\\r?\\n',' | '; '{0:HH:mm:ss} {1}' -f $_.TimeCreated, $m.Substring(0,[Math]::Min(330,$m.Length)) }")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True)
    return [l for l in r.stdout.decode("cp949", "replace").splitlines() if l.strip()]


def dumps_since(t0: float) -> list[str]:
    return sorted(p.name for p in S.GAME.glob("*.crash.dmp") if p.stat().st_mtime >= t0 - 1)


def report_dialogs(tag: str):
    pids = {p for _h, p in S.game_windows()}
    for h, p in S.game_windows("#32770"):
        if p not in pids or not u.IsWindowVisible(h):
            continue
        S.log(f"{tag}: dialog {text_of(h)!r} at {rect_of(h)}")
        for c in kids(h):
            if u.IsWindowVisible(c):
                S.log(f"{tag}:    {class_of(c)!r} id {u.GetDlgCtrlID(c)} {text_of(c)!r}")


def offscreen_if_needed(h):
    """4K 끔이면 창을 화면 밖으로(엔진이 레지스트리 자리를 안 따랐을 때만)."""
    r = W.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    if r.left < 10000:
        u.SetWindowPos(h, 1, 20000, 0, 0, 0, 0x0001 | 0x0010)        # HWND_BOTTOM, NOSIZE|NOACTIVATE
        S.log(f"window was at ({r.left},{r.top}) — moved off screen")


def one_round(tag: str, uhd: int, raw: bytes, enc: str, pos: list[int]) -> bool:
    S.set_ini(raw, enc, agent=0, advance=0, skip=0, mute=1, subtitles=0,
              uhd=uhd, uhd_mode="full", uhd_offscreen=1, uhd_trace=TRACE if uhd else 0, uhd_probe=0, uhd_text=1, uhd_battle=1)
    if not uhd:
        realreg.set_dword("XPos", 20000)
        realreg.set_dword("YPos", 0)
    t0, w0 = time.time(), dt.datetime.now()
    S.log(f"==== {tag}: 4K {'on' if uhd else 'off'}")
    try:
        h = T3.launch()
        if not uhd:
            offscreen_if_needed(h)                                    # the engine keeps the window on screen
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4); S.shot(h, f"{tag}_00_title")
        if ORIG:
            UA.click(h, 405, 380); time.sleep(4); S.shot(h, f"{tag}_01_load")
            rclick(h); time.sleep(3); S.shot(h, f"{tag}_02_rclick")
            UA.click(h, 405, 452); time.sleep(4); S.shot(h, f"{tag}_03_config")
            rclick(h); time.sleep(3); S.shot(h, f"{tag}_04_rclick")
            report_dialogs(f"{tag} before the story steps")
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 317); time.sleep(5)
        UA.click(h, 218, 140); time.sleep(5)
        UA.click(h, *R4.card(3, 0)); time.sleep(5)
        UA.click(h, *T3.START); time.sleep(9)
        for _ in range(5):
            UA.click(h, 400, 300); time.sleep(2.5)
        S.shot(h, f"{tag}_05_text")
        wheel(h, 1); time.sleep(2); S.shot(h, f"{tag}_06_wheel1")
        wheel(h, 3); time.sleep(2); S.shot(h, f"{tag}_07_wheel3")
        for k in range(3):
            rclick(h); time.sleep(2.5); S.shot(h, f"{tag}_{8 + k:02d}_rclick{k + 1}")
        S.u.PostMessageW(h, 0x0010, 0, 0)                            # WM_CLOSE, as quit_game does
        time.sleep(1.5)
        report_dialogs(tag)
        S.dialog_yes(timeout=6)
        for _ in range(40):
            if not S.running():
                break
            time.sleep(0.5)
        else:
            S.log(f"{tag}: still running after the close request — closing it")
            UA.quit_game(h)
    finally:
        time.sleep(6)                                                 # WER writes the event a few seconds later
        if not uhd and not S.running():
            realreg.set_dword("XPos", pos[0])
            realreg.set_dword("YPos", pos[1])
    events, dumps = crashes_since(w0), dumps_since(t0)
    crashed = bool(events or dumps)
    S.log(f"{tag}: {'CRASH' if crashed else 'no crash'}; events {events}; dumps {dumps}")
    return crashed


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다 — 시험하지 않는다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    pos = [reg.get("XPos", 1645), reg.get("YPos", 741)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    results = []
    try:
        for r in range(ROUNDS):
            for side in ORDER:
                tag = f"{side}{r + 1}"
                results.append((tag, one_round(tag, 1 if side == "A" else 0, raw, enc, pos)))
                if S.running():
                    S.log("game still running — stopping the A/B here")
                    return
                time.sleep(3)
    finally:
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():
            realreg.set_dword("XPos", pos[0])
            realreg.set_dword("YPos", pos[1])
            S.log(f"window position restored to {pos}")
        S.log("results: " + ", ".join(f"{t} {'CRASH' if c else 'ok'}" for t, c in results))


if __name__ == "__main__":
    main()
