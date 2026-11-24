"""합본 대리 DLL 판 바꾸기(11-01) — 묶음은 그대로, dinput8.dll 만.

  python -I -X utf8 update_dll.py <새 dll> <지금 있어야 할 sha256> <게임 폴더>...
  python -I -X utf8 update_dll.py --restore <게임 폴더>...      (바로 앞 판으로)

- 그 폴더에서 게임이 돌고 있으면, 또는 지금 dll 이 기대한 판이 아니면 멈춘다.
- 앞 판은 P:\\AI\\khd-hero-ai\\backup\\<폴더 이름+해시>\\dinput8.<앞 판 해시 16자>.dll 로 남긴다.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

BACKUP = Path(r"P:\AI\khd-hero-ai\backup")


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def running_in(game: Path) -> bool:
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='KishinHisho_chs.exe'\" | ForEach-Object { $_.ExecutablePath }"],
                       capture_output=True, text=True, creationflags=0x08000000, timeout=20)
    return any(Path(x.strip()).parent.resolve() == game.resolve() for x in r.stdout.splitlines() if x.strip())


def bdir(game: Path) -> Path:
    tag = hashlib.sha256(str(game.resolve()).lower().encode()).hexdigest()[:8]
    return BACKUP / f"{game.name}_{tag}"


def put(src: Path, dst: Path):
    tmp = dst.with_name(dst.name + ".hero-ai-stage")
    shutil.copy2(src, tmp)
    os.replace(tmp, dst)


args = sys.argv[1:]
if args and args[0] == "--restore":
    for g in map(Path, args[1:]):
        if running_in(g):
            sys.exit(f"{g}: 게임이 켜져 있다 — 손대지 않음")
        rec = json.loads((bdir(g) / "dll_updates.json").read_text(encoding="utf-8"))[-1]
        if sha(g / "dinput8.dll") != rec["new"]:
            sys.exit(f"{g}: dinput8.dll 이 마지막 교체 뒤 바뀌었다 — 멈춤")
        put(Path(rec["backup"]), g / "dinput8.dll")
        assert sha(g / "dinput8.dll") == rec["old"]
        print(f"{g}: dinput8 {rec['old'][:16]} 로 되돌림")
    sys.exit(0)

new, want, games = Path(args[0]), args[1].lower(), [Path(a) for a in args[2:]]
nh = sha(new)
for g in games:
    if running_in(g):
        sys.exit(f"{g}: 게임이 켜져 있다 — 손대지 않음")
    cur = sha(g / "dinput8.dll")
    if cur == nh:
        print(f"{g}: 이미 {nh[:16]} — 건너뜀")
        continue
    if cur != want:
        sys.exit(f"{g}: dinput8.dll 이 {cur[:16]} — 기대한 {want[:16]} 이 아니어서 멈춤")
    d = bdir(g)
    d.mkdir(parents=True, exist_ok=True)
    keep = d / f"dinput8.{cur[:16]}.dll"
    if not keep.exists():
        shutil.copy2(g / "dinput8.dll", keep)
    assert sha(keep) == cur
    put(new, g / "dinput8.dll")
    assert sha(g / "dinput8.dll") == nh
    log = d / "dll_updates.json"
    recs = json.loads(log.read_text(encoding="utf-8")) if log.exists() else []
    recs.append(dict(at=time.strftime("%Y-%m-%dT%H:%M:%S"), old=cur, new=nh, backup=str(keep), source=str(new)))
    log.write_text(json.dumps(recs, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{g}: dinput8 {cur[:16]} -> {nh[:16]} (앞 판 {keep})")
