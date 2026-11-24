"""11-05 second batch for the merged copy F:\\AI\\khd-latest-game (user 「그냥 예스, 노로 만들자」) — run only while that
game is closed.

  script.pak       102cf34d -> final_yesno.pak (the save prompt back on the continue screen's buttons, as in the original)
  0continue_ko.pak e361d956 -> 20e6c27a: the original YES/NO pictures from cg.pak (save_buttons\\restore_yes_no.py)

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
    ("script.pak", "102cf34dfe2dcba3", HERE / "final_yesno.pak"),
    ("0continue_ko.pak", "e361d95699014c31", Path(r"P:\AI\khd-runtime-fix\work\save_buttons\out\0continue_ko.pak")),
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
