"""Wait until the game in F:\\AI\\khd-argon-game closes, swap in the new script.pak / 0replay.pak (the files in use
are kept once as *.before_<tag>), check the hashes, and start the game again the same way as before
(_compat_launch.cmd through WMI, outside the Claude app).

  python -I -X utf8 install_on_exit.py <tag> [--relaunch-only]

The game runs one instance at a time: started while another copy's game is up, ours quits at once (21:09 chant2,
during the test copy's DUEL run, which starts a new game every ~2 min 8 s apart). So the relaunch waits for a full
minute with no KishinHisho_chs.exe from any folder.
"""
import hashlib
import shutil
import subprocess
import sys
import time
from pathlib import Path

import psutil

GAME = Path(r"F:\AI\khd-argon-game")
EXE = str(GAME / "KishinHisho_chs.exe").lower()
BUILD = Path(r"P:\AI\khd-argon-review\build")
FILES = {"script.pak": BUILD / "script.argon.pak", "0replay.pak": BUILD / "0replay.argon.pak"}
TAG = next((a for a in sys.argv[1:] if not a.startswith("--")), "update")
RELAUNCH_ONLY = "--relaunch-only" in sys.argv
LOG = BUILD / "install_on_exit.log"


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def running(any_copy=False):
    for p in psutil.process_iter(["name", "exe"]):
        try:
            if (p.info["exe"] or "").lower() == EXE or (any_copy and (p.info["name"] or "").lower() == "kishinhisho_chs.exe"):
                return True
        except psutil.Error:
            pass
    return False


def sha8(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()[:8].upper()


start, quiet = time.time(), 0
log(f"waiting for the game to close ({TAG}{', relaunch only' if RELAUNCH_ONLY else ''})")
while True:
    if running():
        quiet = 0
    else:
        quiet += 1
        if quiet >= 3:
            break
    if time.time() - start > 8 * 3600:
        log("gave up after 8 hours")
        sys.exit(1)
    time.sleep(2)
time.sleep(2)
for name, src in ({} if RELAUNCH_ONLY else FILES).items():
    dst = GAME / name
    bak = GAME / f"{name}.before_{TAG}"
    if not bak.exists():
        shutil.copy2(dst, bak)
    for attempt in range(10):
        try:
            shutil.copy2(src, dst)
            break
        except PermissionError:
            time.sleep(2)
    else:
        log(f"{name}: still locked, not installed")
        sys.exit(2)
    ok = sha8(dst) == sha8(src)
    log(f"{name}: {sha8(dst)} {'ok' if ok else 'MISMATCH'} (kept {bak.name})")
free_since, said = None, False
while True:
    if running(any_copy=True):
        free_since = None
        if not said:
            log("another copy's game is running - waiting for a minute with no game")
            said = True
    elif free_since is None:
        free_since = time.time()
    elif time.time() - free_since >= 60:
        break
    if time.time() - start > 8 * 3600:
        log("gave up after 8 hours")
        sys.exit(1)
    time.sleep(2)
ps = ("$si = New-CimInstance -Namespace root/cimv2 -ClassName Win32_ProcessStartup -ClientOnly -Property @{ CreateFlags = [uint32]8 }; "
      f"$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{{ CommandLine = 'cmd.exe /c \"{GAME}\\_compat_launch.cmd\"'; "
      f"CurrentDirectory = '{GAME}'; ProcessStartupInformation = $si }}; $r.ReturnValue")
r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True)
log(f"relaunch: WMI {r.stdout.strip()} {r.stderr.strip()[:200]}")
for _ in range(20):
    time.sleep(1)
    if running():
        log("game is running again")
        break
else:
    log("game did not show up within 20 s")
