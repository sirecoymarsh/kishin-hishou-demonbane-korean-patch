"""4K 필살기 비교 촬영 — FINISHERS 칸 하나를 화면 밖·소리 끔으로 틀고 그림 자리만 빠르게(약 0.3초마다) 찍는다.
원해상도(uhd_battle=1)와 800×600 판(uhd_battle=0, 4K 모듈은 켠 채)을 같은 시각끼리 견주려는 것.
찍을 때마다 khd4k 기록의 마지막 프레임 덤프 번호를 이름에 붙인다. 정상 종료.

  python -I -X utf8 run_uhd_finisher_cmp.py [칸=10(그랜드 크로스)] [초=40] [--stageA] [--cap=처음-끝/간격] [--agent0] [--plain] [--envfix=0|1|2] [--noshots] [--d3d8to9]
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
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
from PIL import Image  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
PICK = int(ARGS[0]) if ARGS else 10
SECONDS = float(ARGS[1]) if len(ARGS) > 1 else 40
STAGE_A = "--stageA" in sys.argv
CAP = next((x.split("=", 1)[1] for x in sys.argv if x.startswith("--cap=")), "")   # 예: --cap=1380-1800/6
AGENT = 0 if "--agent0" in sys.argv else 1
WITH_D3D8TO9 = "--d3d8to9" in sys.argv   # 사본에 d3d8to9 를 잠깐 넣고 돌린 뒤 다시 뺀다
ENVFIX = next((x.split("=", 1)[1] for x in sys.argv if x.startswith("--envfix=")), "0")   # 시험용 환경 매핑 실험
NOSHOTS = "--noshots" in sys.argv   # 바깥 캡처(PrintWindow) 안 함 — d3d8to9(D3D9) 창은 이것 때문에 로딩이 멈춘다(10-16 추정)
PLAIN = "--plain" in sys.argv      # 4K 모듈 없이(uhd=0) — 원래 게임 그대로, 창은 레지스트리 XPos 20000 + 바로 화면 밖으로
TAG = "P" if PLAIN else ("A" if STAGE_A else "N")
u, g = ctypes.windll.user32, ctypes.windll.gdi32


def last_frame() -> str:
    try:
        tail = UA.TRACE.read_bytes()[-100000:].decode("utf-8", "replace")
    except OSError:
        return "-"
    m = re.findall(r"---- battle frame (\d+)", tail)
    return m[-1] if m else "-"


def fast_shot(h, name: str):
    """그림 자리만 1440×1080 JPEG 로(PNG 4K 저장은 2초 넘게 걸려 순간을 놓친다)."""
    rc = W.RECT()
    u.GetClientRect(h, ctypes.byref(rc))
    w, ht = rc.right, rc.bottom
    sdc = u.GetDC(None)
    mdc = g.CreateCompatibleDC(sdc)
    bmp = g.CreateCompatibleBitmap(sdc, w, ht)
    old = g.SelectObject(mdc, bmp)
    u.PrintWindow(h, mdc, 3)
    hdr = (ctypes.c_uint32 * 10)(40, w, (-ht) & 0xFFFFFFFF, 1 | (32 << 16), 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * ht * 4)
    g.GetDIBits(mdc, bmp, 0, ht, buf, hdr, 0)
    g.SelectObject(mdc, old)
    g.DeleteObject(bmp)
    g.DeleteDC(mdc)
    u.ReleaseDC(None, sdc)
    im = Image.frombuffer("RGBA", (w, ht), buf, "raw", "BGRA", 0, 1).convert("RGB")
    gx, gy, gw, gh = UA.GEOM
    im = im.crop((gx, gy, gx + gw, gy + gh)).resize((1440, 1080), Image.BILINEAR)
    im.save(S.SHOTS / f"{name}.jpg", quality=90)


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    pos = [reg.get("XPos", 0), reg.get("YPos", 0)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    if UA.TRACE.exists():
        UA.TRACE.unlink()
    S.set_ini(raw, enc, agent=AGENT, background=1, advance=0, skip=0, mute=1, subtitles=0,
              uhd=0 if PLAIN else 1, uhd_mode="full", uhd_offscreen=1, uhd_trace=1, uhd_probe=0, uhd_text=1,
              uhd_battle=0 if STAGE_A else 1, uhd_capture=CAP, uhd_envfix=ENVFIX)
    if PLAIN:
        realreg.set_dword("XPos", 20000)
        realreg.set_dword("YPos", 0)
    d3d8 = S.GAME / "d3d8.dll"
    if WITH_D3D8TO9:
        if d3d8.exists():
            sys.exit("d3d8.dll 이 이미 있다 — 손대지 않는다")
        import shutil
        shutil.copy2(S.GAME / "d3d8.dll.d3d8to9_off", d3d8)
        S.log("d3d8to9 in place for this run")
    try:
        h = T3.launch()
        if PLAIN:
            r = W.RECT()
            u.GetWindowRect(h, ctypes.byref(r))
            if r.left < 10000:
                u.SetWindowPos(h, 1, 20000, 0, 0, 0, 0x0001 | 0x0010)      # HWND_BOTTOM, NOSIZE|NOACTIVATE
                S.log(f"window was at ({r.left},{r.top}) - moved off screen")
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
            time.sleep(0.15)
            k += 1
            if not NOSHOTS:
                fast_shot(h, f"{TAG}{k:03d}_{time.time() - t0:05.1f}s_f{last_frame()}")
        S.log(f"{k} shots in {time.time() - t0:.0f}s ({'plain game' if PLAIN else ('800x600 scaled' if STAGE_A else 'native')})")
        time.sleep(3)
        UA.quit_game(h)
    finally:
        if WITH_D3D8TO9 and d3d8.exists() and not S.running():
            d3d8.unlink()
            S.log("d3d8.dll removed again")
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():
            realreg.set_dword("XPos", pos[0])
            realreg.set_dword("YPos", pos[1])
            S.log(f"window position restored to {pos}")


if __name__ == "__main__":
    main()
