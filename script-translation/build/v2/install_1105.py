"""11-05 batch for the merged copy F:\\AI\\khd-latest-game — run only while that game is closed.

  script.pak      1733be47 -> final.pak   (engine-measured line wrap, staged lines kept on screen, save prompt buttons,
                                          GAME OVER + enemy line before the continue prompt)
  0continue_ko.pak 1014959c -> + 저장하기/하지 않기 buttons (khd-runtime-fix\\work\\save_buttons)
  version.dll     99a22fe2 -> line gap 10 px: dialog boxes hold 4 lines (khd-backlog-jump, [text] line_gap)

Each target must still be the file this batch was built on; the replaced files go to backup\\ (here, not the game
folder). --restore puts the backups back.
"""
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = Path(__file__).resolve().parent
GAME = Path(r"F:\AI\khd-latest-game")
FILES = [  # (game file, expected current sha256 prefix, new file)
    ("script.pak", "1733be477cb20cb2", HERE / "final.pak"),
    ("0continue_ko.pak", "1014959c94bee1cf", Path(r"P:\AI\khd-runtime-fix\work\save_buttons\out\0continue_ko.pak")),
    ("version.dll", "99a22fe206e895a5", Path(r"P:\AI\khd-backlog-jump\out\version.dll")),
]
BACKUP = HERE / "backup"


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def running() -> bool:
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='KishinHisho_chs.exe'\" | ForEach-Object { $_.ExecutablePath }"],
                       capture_output=True, text=True, encoding="cp949", errors="replace", creationflags=0x08000000, timeout=30)
    return any(Path(x.strip()).parent.resolve() == GAME.resolve() for x in r.stdout.splitlines() if x.strip())


def main():
    if running():
        sys.exit("game is running from this copy - not touched")
    BACKUP.mkdir(exist_ok=True)
    if "--restore" in sys.argv:
        for name, want, _ in FILES:
            b = BACKUP / f"{name}.{want[:8]}"
            if b.exists():
                shutil.copy2(b, GAME / name)
                print(name, "restored", sha(GAME / name)[:8])
        return
    plan = []
    for name, want, new in FILES:
        cur = sha(GAME / name)
        if cur == sha(new):
            print(name, "already in", cur[:8])
            continue
        if not cur.startswith(want):
            sys.exit(f"{name} is {cur[:8]}, not {want[:8]} this batch was built on - nothing installed")
        plan.append((name, want, new))
    for name, want, new in plan:
        shutil.copy2(GAME / name, BACKUP / f"{name}.{want[:8]}")
        tmp = GAME / (name + ".incoming")
        shutil.copy2(new, tmp)
        os.replace(tmp, GAME / name)
        got = sha(GAME / name)
        assert got == sha(new), name
        print(name, "ok", got[:8])


if __name__ == "__main__":
    main()
