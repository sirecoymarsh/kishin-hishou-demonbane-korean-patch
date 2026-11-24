"""주인공 5기체 AI 합본 설치·되돌리기(11-01).

  python -I -X utf8 install_hero_ai.py <게임 폴더>...            설치(백업 뒤)
  python -I -X utf8 install_hero_ai.py --restore <게임 폴더>...  설치 전으로

- 그 폴더에서 게임이 돌고 있으면 손대지 않는다.
- 지금 파일이 10-26 설치판(코덱스 HM-RM R5)이 아니면 멈춘다(다른 작업이 바꿨다는 뜻) — 이미 이 합본이면 건너뛴다.
- 백업: P:\\AI\\khd-hero-ai\\backup\\<폴더 이름 + 해시>\\ (원래 dinput8.dll·dmpack513, 514 묶음은 원래 없음을 기록).
- 되돌리기는 이 합본의 해시와 같은 파일만 지우거나 바꾼다.
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

NEW = Path(r"P:\AI\khd-hero-ai\merged\out")
BACKUP = Path(r"P:\AI\khd-hero-ai\backup")
BEFORE = {   # 10-26 14:04 코덱스 설치판(boss-match-ai-20261026\install-hm-rm-r5-20261026)
    "dinput8.dll": "8b8eb19522d08f4a88773e49e411c125411a6eb7bdf9411ee2096c614a30d188",
    "dmpack513_hm_owned.dat": "aa312fbcc77ebc52017b48c6035351fa8eaa214cfeba541b5d92d773f9037ea0",
}
ADDED = ["dmpack514_am_owned.dat", "dmpack514_a2_air.dat", "dmpack514_em_owned.dat", "dmpack514_f2_owned.dat"]
FILES = list(BEFORE) + ADDED


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def running_in(game: Path) -> list[str]:
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='KishinHisho_chs.exe'\" | ForEach-Object { $_.ExecutablePath }"],
                       capture_output=True, text=True, creationflags=0x08000000, timeout=20)
    paths = [x.strip() for x in r.stdout.splitlines() if x.strip()]
    return [p for p in paths if Path(p).parent.resolve() == game.resolve()]


def put(src: Path, dst: Path):
    tmp = dst.with_name(dst.name + ".hero-ai-stage")
    shutil.copy2(src, tmp)
    os.replace(tmp, dst)


def backup_dir(game: Path) -> Path:
    tag = hashlib.sha256(str(game.resolve()).lower().encode()).hexdigest()[:8]
    return BACKUP / f"{game.name}_{tag}"


def install(game: Path):
    new = {n: sha(NEW / n) for n in FILES}
    now = {n: (sha(game / n) if (game / n).exists() else None) for n in FILES}
    if now == new:
        print(f"{game}: 이미 이 합본 — 건너뜀")
        return
    for n, h in BEFORE.items():
        if now[n] != h:
            sys.exit(f"{game}: {n} 이 10-26 설치판이 아니다({now[n]}) — 멈춤")
    for n in ADDED:
        if now[n] is not None:
            sys.exit(f"{game}: {n} 이 이미 있다({now[n][:16]}) — 멈춤")
    bdir = backup_dir(game)
    if bdir.exists():
        sys.exit(f"{bdir} 이 이미 있다 — 먼저 확인할 것")
    bdir.mkdir(parents=True)
    for n in BEFORE:
        shutil.copy2(game / n, bdir / n)
        assert sha(bdir / n) == BEFORE[n]
    for n in FILES:
        put(NEW / n, game / n)
    after = {n: sha(game / n) for n in FILES}
    assert after == new, f"{game}: 넣은 뒤 해시가 다르다"
    rec = dict(at=time.strftime("%Y-%m-%dT%H:%M:%S"), game=str(game), before=now, after=after, backup=str(bdir))
    (bdir / "installation.json").write_text(json.dumps(rec, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{game}: 설치 — dinput8 {after['dinput8.dll'][:16]}, 513 {after['dmpack513_hm_owned.dat'][:16]}, 514 묶음 4개 추가 (백업 {bdir})")


def restore(game: Path):
    bdir = backup_dir(game)
    rec = json.loads((bdir / "installation.json").read_text(encoding="utf-8"))
    for n in FILES:
        p = game / n
        if p.exists() and sha(p) != rec["after"][n]:
            sys.exit(f"{game}: {n} 이 설치 뒤 다시 바뀌었다({sha(p)[:16]}) — 멈춤")
    for n in BEFORE:
        put(bdir / n, game / n)
        assert sha(game / n) == BEFORE[n]
    for n in ADDED:
        if (game / n).exists():
            (game / n).unlink()
    print(f"{game}: 설치 전으로 되돌림")


args = sys.argv[1:]
undo = "--restore" in args
games = [Path(a) for a in args if not a.startswith("--")]
for g in games:
    if running_in(g):
        sys.exit(f"{g}: 이 폴더의 게임이 켜져 있다 — 손대지 않음")
for g in games:
    (restore if undo else install)(g)
