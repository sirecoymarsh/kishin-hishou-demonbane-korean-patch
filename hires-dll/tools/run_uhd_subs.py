"""전투 자막 창 4K 시험(10-16) — 화면 밖·소리 끔·subtitles=1 로 FINISHERS 칸 하나를 돌리며 자막 창(KhdSubtitles.exe)의
화면 좌표·크기를 게임 그림 자리(창 속성 khd4k.pos·khd4k.size)와 함께 적는다.

  python -I -X utf8 run_uhd_subs.py full|window|off [칸=4] [--expect-ini=<사용자 ini SHA-256 앞 8자리>]
    full    uhd=1 모니터 전체 방식(4096×2160 이면 그림 (608,0) 2880×2160, 배율 3.6)
    window  uhd=1 창 방식 2배(그림 (0,0) 1600×1200, 배율 2)
    off     uhd=0(khd4k 없음 = 예전 그대로) — 레지스트리 XPos=20000·YPos=0 으로 두고 띄우고, 엔진이 창을 화면 안 (3290,0) 으로
            당기므로 창이 생기는 즉시 화면 밖으로 옮겨 6초 동안 지킨다.
진짜 레지스트리 FullScreen 이 0 이 아니면(못 읽으면) 띄우지 않는다(띄우기 직전에 다시 읽는다) — 엔진의 전체 화면 전환을 막는
건 khd4k 뿐이라, uhd=0 에서 FullScreen=1 이면 주 모니터가 800×600 으로 바뀐다(10-16 00:38 다른 시험에서 실제로 그랬다).
다른 게임(사용자 플레이)이 켜져 있으면 시작하지 않는다.
끝나면 autobattle.ini 는 시험 전 바이트 그대로, 레지스트리 FullScreen·XPos·YPos 는 시험 전 값으로(realreg.py, 게임이 꺼진 뒤).
기록: <SHOTS>\\subs_<mode>.tsv — 자막 창이 바뀔 때마다 화면 좌표와 「그림 기준 좌표 ÷ 배율」(800×600 단위),
겹친 그림 <SHOTS>\\subs_<mode>_NN.png(게임 그림 + 자막 창을 PrintWindow 로 떠서 겹침).
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import subprocess
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import battle_watch as BW  # noqa: E402
import run_finisher_test as F  # noqa: E402
import run_smoke as S  # noqa: E402
import var_dump as V  # noqa: E402
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

MODE = sys.argv[1] if len(sys.argv) > 1 else "full"
PICK = int(sys.argv[2]) if len(sys.argv) > 2 else 4
assert MODE in ("full", "window", "off"), MODE
REG_KEEP = ("FullScreen", "XPos", "YPos")
u, g = S.u, S.g
u.GetPropA.restype = ctypes.c_void_p
u.GetPropA.argtypes = [W.HWND, ctypes.c_char_p]
u.GetWindowRect.argtypes = [W.HWND, ctypes.POINTER(W.RECT)]
u.ClientToScreen.argtypes = [W.HWND, ctypes.POINTER(W.POINT)]
u.GetWindowLongW.argtypes = [W.HWND, ctypes.c_int]
u.IsWindowVisible.argtypes = [W.HWND]
OUT = S.SHOTS / f"subs_{MODE}.tsv"
LAUNCHED = [0]                                             # the game process this test started


def log(m):
    S.log(m)
    with open(OUT, "a", encoding="utf-8") as fh:
        fh.write(f"{time.strftime('%H:%M:%S')}\t{m}\n")


def screen_size():
    return u.GetSystemMetrics(0), u.GetSystemMetrics(1)


def off_screen(h) -> bool:
    """창이 모니터 위에 있으면 화면 밖(주 모니터 오른쪽 x=20000)으로 — 사용자 화면에 안 보이게."""
    x, y, _w, _h = wrect(h)
    if x < 10000:
        u.SetWindowPos(h, None, 20000, 0, 0, 0, 0x0001 | 0x0004 | 0x0010)     # NOSIZE NOZORDER NOACTIVATE
        return True
    return False


def launch(keep_off: bool):
    """WMI 로 앱 밖에서 띄운다. 띄우기 전에 게임이 하나도 없었으니 새로 생긴 게임 창이 이 시험의 것이다 — 게임이 스스로를 한 번
    더 띄워 WMI 가 준 pid 와 진짜 게임 pid 가 다르다(10-16 확인). 창이 보이는 즉시(10ms 간격, WMI 응답을 기다리지 않고)
    화면 안이면 밖으로 옮긴다. keep_off: 4K 모듈이 없으면 엔진이 x=20000 을 화면 안 (3290,0)으로 당기므로 6초 동안 지킨다."""
    before = {p for _h, p in S.game_windows()}
    exe = S.GAME / "KishinHisho_chs.exe"
    wmi = subprocess.Popen(["powershell", "-NoProfile", "-Command",
                            "$si = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ShowWindow = [uint16]4}; "
                            "$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = '\""
                            + str(exe) + "\"'; CurrentDirectory = '" + str(S.GAME) + "'; ProcessStartupInformation = $si }; $r.ProcessId"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    t0, found, moved = time.time(), None, 0
    while time.time() - t0 < 40:
        ws = [(h, p) for h, p in S.game_windows() if p not in before]
        if ws:
            found = ws[0]
            if off_screen(found[0]):
                moved += 1
            break
        time.sleep(0.01)
    out, err = wmi.communicate(timeout=60)
    log(f"WMI create: {out.strip()!r} {err.strip()[:200]!r}")
    if not found:
        log(f"no new game window; game windows {S.game_windows()}")
        sys.exit("no window")
    h, pid = found
    LAUNCHED[0] = pid
    if keep_off:
        t1 = time.time()
        while time.time() - t1 < 6:
            if off_screen(h):
                moved += 1
            time.sleep(0.02)
    log(f"game window {h:#x} of pid {pid} found {time.time() - t0:.2f}s after the launch; moved off screen {moved} time(s)")
    return pid, h


def wrect(h):
    r = W.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def picture(h):
    """그림 자리(화면 좌표)와 배율: khd4k 창 속성이 있으면 그것, 없으면 클라이언트 800×600."""
    o = W.POINT()
    u.ClientToScreen(h, ctypes.byref(o))
    pos, size = u.GetPropA(h, b"khd4k.pos") or 0, u.GetPropA(h, b"khd4k.size") or 0
    pw, ph = size & 0xFFFF, (size >> 16) & 0xFFFF
    if pw and ph:
        return (o.x + (pos & 0xFFFF), o.y + ((pos >> 16) & 0xFFFF), pw, ph), ph / 600, True
    c = W.RECT()
    u.GetClientRect(h, ctypes.byref(c))
    return (o.x, o.y, c.right, c.bottom), c.bottom / 600, False


def proc_pid(name):
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        f"(Get-Process {name} -ErrorAction SilentlyContinue | Sort-Object StartTime -Descending | Select-Object -First 1).Id"],
                       capture_output=True, text=True)
    s = r.stdout.strip()
    return int(s) if s else 0


def sub_windows(spid):
    """자막 창들(겹 창 WS_EX_LAYERED 이고 보이는 것)."""
    out = []

    def cb(h, _l):
        p = W.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == spid and u.IsWindowVisible(h) and (u.GetWindowLongW(h, -20) & 0x80000):
            out.append(h)
        return True
    u.EnumWindows(S.WNDENUMPROC(cb), 0)
    return out


def grab(h, flags):
    """PrintWindow 로 창(또는 클라이언트)을 32비트로 뜬다 — 알파까지."""
    if flags & 1:
        r = W.RECT()
        u.GetClientRect(h, ctypes.byref(r))
        w, ht = r.right, r.bottom
    else:
        _x, _y, w, ht = wrect(h)
    sdc = u.GetDC(None)
    mdc = g.CreateCompatibleDC(sdc)
    hdr = (ctypes.c_uint32 * 10)(40, w, (-ht) & 0xFFFFFFFF, 1 | (32 << 16), 0, 0, 0, 0, 0, 0)
    bits = ctypes.c_void_p()
    g.CreateDIBSection.restype = W.HBITMAP
    g.CreateDIBSection.argtypes = [W.HDC, ctypes.c_void_p, W.UINT, ctypes.POINTER(ctypes.c_void_p), W.HANDLE, W.DWORD]
    bmp = g.CreateDIBSection(mdc, hdr, 0, ctypes.byref(bits), None, 0)
    old = g.SelectObject(mdc, bmp)
    ctypes.memset(bits, 0, w * ht * 4)
    ok = u.PrintWindow(h, mdc, flags)
    buf = ctypes.string_at(bits, w * ht * 4)
    g.SelectObject(mdc, old)
    g.DeleteObject(bmp)
    g.DeleteDC(mdc)
    u.ReleaseDC(None, sdc)
    a = np.frombuffer(buf, np.uint8).reshape(ht, w, 4)[:, :, [2, 1, 0, 3]].copy()
    return ok, a


def composite(h, subs, pic, tag):
    """게임 그림 + 보이는 자막 창들을 겹쳐 그림 자리만 잘라 저장(반 크기도)."""
    ok, base = grab(h, 3)                                  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    o = W.POINT()
    u.ClientToScreen(h, ctypes.byref(o))
    img = Image.fromarray(base[:, :, :3].copy(), "RGB")
    notes = []
    for sh in subs:
        x, y, w, ht = wrect(sh)
        if x < -1000:
            continue
        sok, a = grab(sh, 2)                               # PW_RENDERFULLCONTENT
        alpha = a[:, :, 3]
        notes.append(f"{sh:#x} printwindow {sok} alpha {int(alpha.min())}..{int(alpha.max())}")
        if alpha.min() == alpha.max():
            # PrintWindow does not see what UpdateLayeredWindow put there (10-16: the form's grey background, alpha 255):
            # mark where the window is instead
            from PIL import ImageDraw
            d = ImageDraw.Draw(img)
            lw = max(2, ht // 60)
            d.rectangle((x - o.x, y - o.y, x - o.x + w - 1, y - o.y + ht - 1), outline=(255, 0, 255), width=lw)
            continue
        # PrintWindow gives the layered window's premultiplied pixels: back to straight alpha, then paste with it
        al = alpha.astype(np.float32)[:, :, None]
        rgb = np.where(al > 0, np.clip(a[:, :, :3].astype(np.float32) * 255.0 / np.maximum(al, 1), 0, 255), 0)
        a = np.concatenate([rgb.astype(np.uint8), alpha[:, :, None]], axis=2)
        rgba = Image.fromarray(a, "RGBA")
        lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
        lay.paste(rgba, (x - o.x, y - o.y))
        img = Image.alpha_composite(img.convert("RGBA"), lay).convert("RGB")
    px, py, pw, ph = pic
    crop = img.crop((px - o.x, py - o.y, px - o.x + pw, py - o.y + ph))
    p = S.SHOTS / f"subs_{MODE}_{tag}.png"
    crop.save(p)
    crop.resize((pw // 2, ph // 2), Image.LANCZOS).save(S.SHOTS / f"subs_{MODE}_{tag}_half.png")
    log(f"composite {p.name} (game printwindow {ok}); " + "; ".join(notes))


def describe(subs, pic, k):
    px, py, _pw, _ph = pic
    out = []
    for sh in subs:
        x, y, w, ht = wrect(sh)
        if x < -1000:
            continue
        out.append(f"{sh:#x} screen ({x},{y}) {w}x{ht} = picture/K ({(x - px) / k:.1f},{(y - py) / k:.1f}) {w / k:.1f}x{ht / k:.1f}")
    return out


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    # another session's test may be between two of its rounds (its settings in the ini, no game running): then the
    # ini is not the user's and must not become the "before" this test restores
    want = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--expect-ini=")), "")
    if want and S.sha(S.INI) != want.upper():
        sys.exit(f"autobattle.ini 가 기대한 사용자 설정({want})이 아니다({S.sha(S.INI)}) — 다른 시험 중일 수 있어 하지 않는다")
    saved = realreg.query()
    before = {n: saved.get(n) for n in REG_KEEP}
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    log(f"mode {MODE}, finisher {PICK}; registry before {before}; screen {screen_size()}")
    scr0 = screen_size()
    if before.get("FullScreen") != 0:
        sys.exit(f"진짜 레지스트리 FullScreen 이 0 이 아니다({before.get('FullScreen')}) — 띄우지 않는다")
    vals = dict(agent=1, advance=0, skip=0, mute=1, subtitles=1, float=0, uhd_trace=0, uhd_probe=0, uhd_text=1, uhd_battle=1)
    if MODE == "off":
        vals.update(uhd=0)
    else:
        vals.update(uhd=1, uhd_mode="full" if MODE == "full" else "window", uhd_scale=2, uhd_offscreen=1)
    h = None
    try:
        if MODE == "off":
            realreg.set_dword("XPos", 20000)
            realreg.set_dword("YPos", 0)
        now = realreg.query()                              # right before the launch, once more
        log(f"registry for the launch {({n: now.get(n) for n in REG_KEEP})}")
        if now.get("FullScreen") != 0 or S.running():
            sys.exit("FullScreen 이 0 이 아니거나 게임이 켜져 있다 — 띄우지 않는다")
        S.set_ini(raw, enc, **vals)
        pid, h = launch(MODE == "off")
        log(f"game pid {pid} window {h:#x} at {wrect(h)}")
        x, y, w, ht = wrect(h)
        if x < 10000:                                      # never on the user's screens
            u.SetWindowPos(h, None, 20000, 0, 0, 0, 0x0001 | 0x0004 | 0x0010)
            log(f"window was on a monitor — moved to {wrect(h)}")
        time.sleep(1.0)
        if screen_size() != scr0:
            log(f"DISPLAY MODE CHANGED {scr0} -> {screen_size()} — closing")
            UA.quit_game(h)
            return
        time.sleep(2)
        pic, k, uhd = picture(h)
        o = W.POINT()
        u.ClientToScreen(h, ctypes.byref(o))
        UA.GEOM[:] = [pic[0] - o.x, pic[1] - o.y, pic[2], pic[3]]
        log(f"window {wrect(h)}, client origin ({o.x},{o.y}), picture {pic} K {k:.3f} (khd4k properties: {uhd})")
        spid = 0
        for _ in range(10):
            spid = proc_pid("KhdSubtitles")
            if spid:
                break
            time.sleep(0.5)
        log(f"subtitle process {spid}")
        time.sleep(6)
        UA.click(h, 400, 500); time.sleep(4)
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 431); time.sleep(5)
        S.shot(h, f"subs_{MODE}_list")
        UA.click(h, *F.bar_center(PICK))
        ph_ = V.k.OpenProcess(0x0010 | 0x0400, False, pid)
        t0 = time.time()
        seen, last, shots, moved = False, None, 0, False
        while time.time() - t0 < 200:
            base = V.module_base(ph_, "demonbane.dll")
            snap = BW.snapshot(ph_, base) if base else {"scene": "", "chars": []}
            if snap["chars"]:
                seen = True
            elif seen:
                log("battle closed")
                break
            pic, k, uhd = picture(h)
            subs = sub_windows(spid)
            desc = describe(subs, pic, k)
            key = tuple(desc)
            if key != last:
                last = key
                log(f"picture {pic} K {k:.3f} window {wrect(h)} | " + (" || ".join(desc) if desc else "no rows shown"))
                if desc and shots < 3:
                    time.sleep(0.3)
                    composite(h, subs, pic, f"{shots:02d}")
                    shots += 1
                if desc and not moved:
                    # the rows follow the window: move it 300 right and 200 down for a moment
                    x, y, _w, _h = wrect(h)
                    u.SetWindowPos(h, None, x + 300, y + 200, 0, 0, 0x0001 | 0x0004 | 0x0010)
                    time.sleep(0.4)
                    pic2, k2, _u = picture(h)
                    log(f"moved window by (300,200): picture {pic2} | " + " || ".join(describe(sub_windows(spid), pic2, k2)))
                    u.SetWindowPos(h, None, x, y, 0, 0, 0x0001 | 0x0004 | 0x0010)
                    time.sleep(0.4)
                    pic3, k3, _u = picture(h)
                    log(f"moved back: picture {pic3} | " + " || ".join(describe(sub_windows(spid), pic3, k3)))
                    moved = True
                    last = None
            time.sleep(0.1)
        else:
            log("battle did not close within the limit")
        time.sleep(4)
        S.shot(h, f"subs_{MODE}_after")
        log("rows after the battle: " + (" || ".join(describe(sub_windows(spid), *picture(h)[:2])) or "none"))
        UA.quit_game(h)
    finally:
        S.INI.write_bytes(raw)
        keys = ("agent", "mute", "subtitles", "skip", "advance", "uhd", "uhd_mode", "uhd_offscreen", "uhd_trace")
        now = {ln.split("=", 1)[0].strip(): ln.split("=", 1)[1].strip()
               for ln in S.INI.read_bytes().decode(enc).splitlines() if "=" in ln and not ln.lstrip().startswith(";")}
        log(f"autobattle.ini restored: {S.sha(S.INI)} (" + ", ".join(f"{n}={now.get(n)}" for n in keys) + ")")
        if h is None and LAUNCHED[0]:                      # started but not driven: close it normally all the same
            ws = [x for x, p in S.game_windows() if p == LAUNCHED[0]]
            if ws:
                UA.quit_game(ws[0])
        for _ in range(20):
            if not S.running():
                break
            time.sleep(0.5)
        if not S.running():
            for n, v in before.items():
                if v is not None:
                    realreg.set_dword(n, v)
            log(f"registry restored {({n: realreg.query().get(n) for n in REG_KEEP})}")
        else:
            log("game still running — registry NOT restored; restore FullScreen/XPos/YPos after it closes")


if __name__ == "__main__":
    main()
