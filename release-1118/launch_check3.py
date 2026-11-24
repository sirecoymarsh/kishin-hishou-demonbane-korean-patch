"""11-18 밤 사용자 「본패치만 / 본패치+4K / 본패치+4K+책 한번씩 띄워줄래? 확인해보게」.
기신비상은 한 번에 하나만 뜨므로 check3\ 의 세 벌을 차례로 띄운다: 하나를 띄우고, 사용자가 그 게임을 닫으면 다음 것.
사용자가 두 번 누르는 것과 같게 그 폴더의 Play.cmd 를 앱 밖(WMI)에서 콘솔 없이(DETACHED_PROCESS) 돌린다.
화면에 보이게 띄우고(레지스트리의 사용자 창 자리), 창 제목 끝에 몇 번째 판인지 붙인다. 아무것도 누르지 않는다.
  python -I -X utf8 launch_check3.py [시작 번호 1~3]
"""
import ctypes
import ctypes.wintypes as W
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-4k\tools")
import realreg  # noqa: E402

ROOT = Path(r"Y:\AI\claude-work\khd-release-1118\check3")
SETS = [("1 본패치만", "확인 1: 본패치만"), ("2 본패치+4K", "확인 2: 본패치 + 4K"), ("3 본패치+4K+책", "확인 3: 본패치 + 4K + 책")]
EXE = "KishinHisho_chs.exe"
LOG = ROOT / "launch_log.txt"
u = ctypes.WinDLL("user32", use_last_error=True)
k = ctypes.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.restype = W.HANDLE
WNDENUMPROC = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)


def log(s):
    line = time.strftime("%H:%M:%S ") + s
    print(line, flush=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def exe_path(pid):
    h = k.OpenProcess(0x1000, False, pid)
    if not h:
        return ""
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = W.DWORD(1024)
        return buf.value if k.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) else ""
    finally:
        k.CloseHandle(h)


def game_pids():
    """켜져 있는 모든 기신비상(KishinHisho_chs.exe) — {pid: 경로}"""
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "Get-Process KishinHisho_chs -ErrorAction SilentlyContinue | ForEach-Object { $_.Id }"],
                         capture_output=True, text=True).stdout
    return {int(x): exe_path(int(x)) for x in out.split() if x.strip().isdigit()}


def main_window(pid):
    found = []

    def cb(h, _l):
        p = W.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h) and not u.GetWindow(h, 4):   # GW_OWNER 없음 = 맨 위 창
            found.append(h)
        return True
    u.EnumWindows(WNDENUMPROC(cb), 0)
    return found[0] if found else None


def launch(game: Path):
    env = dict(os.environ, KHD_CMD=f'"C:\\Windows\\System32\\cmd.exe" /d /c ""{game}\\Play.cmd""', KHD_DIR=str(game))
    ps = ("$si = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ShowWindow=[uint16]0; CreateFlags=[uint32]8}; "
          "$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine=$env:KHD_CMD; "
          "CurrentDirectory=$env:KHD_DIR; ProcessStartupInformation=$si }; \"$($r.ReturnValue) $($r.ProcessId)\"")
    return subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True, env=env).stdout.strip()


start = int(sys.argv[1]) if len(sys.argv) > 1 else 1
reg = realreg.query()
log(f"registry FullScreen {reg.get('FullScreen')} XPos/YPos {reg.get('XPos')},{reg.get('YPos')}")
if reg.get("FullScreen", 0) != 0:
    sys.exit("레지스트리 FullScreen 이 0 이 아니다 — 모니터 해상도가 바뀔 수 있어 띄우지 않는다")
for i, (folder, tag) in enumerate(SETS, 1):
    if i < start:
        continue
    game = ROOT / folder
    t0 = time.time()
    while True:                                   # 다른 기신비상이 켜져 있으면 꺼질 때까지 (한 번에 하나만 뜸)
        other = game_pids()
        if not other:
            break
        if time.time() - t0 > 3 * 3600:
            sys.exit(f"세 시간 동안 다른 기신비상이 안 꺼짐 {other} — 그만")
        time.sleep(2)
    if i > start:
        time.sleep(3)
    log(f"[{i}] 띄움 {game} → {launch(game)}")
    pid, h = None, None
    for _ in range(120):
        for p, path in game_pids().items():
            if path.lower() == str(game / EXE).lower():
                pid = p
        if pid:
            h = main_window(pid)
            if h:
                break
        time.sleep(0.5)
    if not pid:
        log(f"[{i}] 게임 프로세스를 못 찾음 — 그만")
        sys.exit(1)
    if h:
        time.sleep(2)
        h = main_window(pid) or h
        b = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(h, b, 256)
        u.SetWindowTextW(h, f"{b.value} — {tag}")
        r = W.RECT()
        u.GetWindowRect(h, ctypes.byref(r))
        log(f"[{i}] pid {pid} 창 {h:#x} 「{b.value}」 자리 ({r.left},{r.top})-({r.right},{r.bottom})")
    else:
        log(f"[{i}] pid {pid} 창을 아직 못 찾음")
    hp = k.OpenProcess(0x100000, False, pid)          # SYNCHRONIZE
    k.WaitForSingleObject(hp, 0xFFFFFFFF)
    k.CloseHandle(hp)
    log(f"[{i}] 닫힘 ({(time.time() - t0) / 60:.1f}분)")
reg = realreg.query()
log(f"끝. registry XPos/YPos {reg.get('XPos')},{reg.get('YPos')}")
