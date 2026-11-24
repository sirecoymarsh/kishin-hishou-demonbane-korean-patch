"""Wait until the merged copy's game (F:\\AI\\khd-latest-game) has been closed for 30 s, run install_1105.py, then start
that game again once no copy of the game runs (one instance at a time: test copies first). Never closes a game.

  python -I -X utf8 install_waiter_1105.py
"""
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
HERE = Path(__file__).resolve().parent
GAME = Path(r"F:\AI\khd-latest-game")
EXE = "kishinhisho_chs.exe"
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi")


def game_paths():
    arr = (wintypes.DWORD * 4096)()
    got = wintypes.DWORD()
    psapi.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(got))
    out = []
    for pid in arr[: got.value // 4]:
        h = k32.OpenProcess(0x1000, False, pid)            # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024)
        n = wintypes.DWORD(1024)
        if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) and buf.value.lower().endswith(EXE):
            out.append(buf.value)
        k32.CloseHandle(h)
    return out


def ours(paths):
    return any(Path(p).parent.resolve() == GAME.resolve() for p in paths)


def runners():
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | ForEach-Object { $_.CommandLine }"],
                       capture_output=True, text=True, encoding="cp949", errors="replace", creationflags=0x08000000)
    return any(("when_closed" in c or "_after_" in c or "run_duel" in c or "run_v12_test" in c) and "install_waiter" not in c
               for c in r.stdout.splitlines())


def log(*a):
    print(time.strftime("%H:%M:%S"), *a)


def main():
    t0, quiet = time.time(), 0
    while quiet < 30:
        quiet = 0 if ours(game_paths()) else quiet + 10
        if time.time() - t0 > 12 * 3600:
            log("game not closed in 12 h - nothing installed")
            return
        time.sleep(10)
    log(f"merged copy's game closed (waited {int(time.time() - t0)} s)")
    r = subprocess.run([sys.executable, "-I", "-X", "utf8", str(HERE / "install_1105.py")], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    print(r.stdout.strip(), r.stderr.strip()[-500:])
    log("install exit", r.returncode)
    if r.returncode:
        return
    # other sessions' DUEL tests run in the test copies one game after another (python test_*_when_closed / *_after_*):
    # start ours only after a full minute with no game and no such runner, so a test round is never cut off
    t1, quiet = time.time(), 0
    while quiet < 60:
        busy = bool(game_paths()) or runners()
        quiet = 0 if busy else quiet + 20
        if time.time() - t1 > 4 * 3600:
            log("tests kept running 4 h - game not started; start it by hand")
            return
        time.sleep(20)
    ps = ("$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = "
          f"'\"{GAME}\\KishinHisho_chs.exe\"'; CurrentDirectory = '{GAME}' }}; 'launched ' + $r.ReturnValue")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True,
                       encoding="cp949", errors="replace", creationflags=0x08000000)
    log(r.stdout.strip())
    time.sleep(15)
    lines = (GAME / "khdjump_log.txt").read_text(encoding="utf-8", errors="replace").splitlines()
    print("\n".join(lines[-4:]))


if __name__ == "__main__":
    main()
