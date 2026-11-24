"""네로 각인 666 DUEL 묶음(dmpack520_kokuin666.dat) 설치·되돌리기(11-02).

  python -I -X utf8 install_kokuin666.py <묶음> <sha256> <게임 폴더>...   설치(이 도구로 넣은 옛 판이면 바꿈)
  python -I -X utf8 install_kokuin666.py --restore <게임 폴더>...         이 묶음 빼기(설치 전엔 없던 파일)

- 그 폴더에서 게임이 돌고 있으면 손대지 않는다.
- 같은 이름 파일이 이 도구 기록에 없는 해시면 멈춘다(다른 작업이 넣었다는 뜻).
- 기록: P:\\AI\\khd-hero-ai\\backup\\<폴더 이름_해시>\\kokuin666.json(판마다 해시·시각), 바꾼 옛 판은 같은 폴더에 해시 이름으로.
- 502·504·505·506·508·510·512 묶음을 다시 빌드하면 이 묶음도 build_kokuin666.py 로 다시 만들어 넣을 것(같은 정의를 덮는다).
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

NAME = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--name=")), "dmpack520_kokuin666.dat")   # 11-03: 517 등 다른 묶음도
RECORD = "kokuin666.json" if NAME == "dmpack520_kokuin666.dat" else Path(NAME).stem + ".json"
BACKUP = Path(r"P:\AI\khd-hero-ai\backup")


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def running_in(game: Path) -> list[str]:
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='KishinHisho_chs.exe'\" | ForEach-Object { $_.ExecutablePath }"],
                       capture_output=True, text=True, creationflags=0x08000000, timeout=20)
    paths = [x.strip() for x in r.stdout.splitlines() if x.strip()]
    return [p for p in paths if Path(p).parent.resolve() == game.resolve()]


def backup_dir(game: Path) -> Path:
    tag = hashlib.sha256(str(game.resolve()).lower().encode()).hexdigest()[:8]
    return BACKUP / f"{game.name}_{tag}"


def load(game: Path) -> dict:
    p = backup_dir(game) / RECORD
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else {"game": str(game), "installs": []}


def save(game: Path, rec: dict):
    d = backup_dir(game)
    d.mkdir(parents=True, exist_ok=True)
    (d / RECORD).write_text(json.dumps(rec, ensure_ascii=False, indent=1), encoding="utf-8")


def known_builds() -> set:
    """이 도구가 어느 폴더에든 넣은 판 — 11-04: 합본·v12 사본은 아르곤 사본을 통째로 베껴 그 판을 가져 제 기록이 없다"""
    out = set()
    for p in BACKUP.glob(f"*/{RECORD}"):
        out |= {i["sha"] for i in json.loads(p.read_text(encoding="utf-8"))["installs"] if i.get("sha")}
    return out


def install(src: Path, want: str, game: Path):
    dst, rec = game / NAME, load(game)
    ours = {i["sha"] for i in rec["installs"]} | known_builds()
    now = sha(dst) if dst.exists() else None
    if now == want:
        print(f"{game}: 이미 이 판({want[:16]}) — 건너뜀")
        return
    if now is not None:
        if now not in ours:
            sys.exit(f"{game}: {NAME} 이 이 도구가 넣은 판이 아니다({now[:16]}) — 멈춤")
        shutil.copy2(dst, backup_dir(game) / f"{NAME}.{now[:16]}")
    tmp = dst.with_name(NAME + ".kokuin-stage")
    shutil.copy2(src, tmp)
    os.replace(tmp, dst)
    assert sha(dst) == want, f"{game}: 넣은 뒤 해시가 다르다"
    rec["installs"].append(dict(at=time.strftime("%Y-%m-%dT%H:%M:%S"), sha=want, replaced=now))
    save(game, rec)
    print(f"{game}: {NAME} {want[:16]} 설치" + (f"(옛 판 {now[:16]} 은 백업)" if now else "(새 파일)"))


def restore(game: Path):
    dst, rec = game / NAME, load(game)
    if not dst.exists():
        print(f"{game}: {NAME} 없음 — 건너뜀")
        return
    now = sha(dst)
    if now not in {i["sha"] for i in rec["installs"]}:
        sys.exit(f"{game}: {NAME} 이 이 도구가 넣은 판이 아니다({now[:16]}) — 멈춤")
    dst.unlink()
    rec["installs"].append(dict(at=time.strftime("%Y-%m-%dT%H:%M:%S"), sha=None, removed=now))
    save(game, rec)
    print(f"{game}: {NAME} 뺌(설치 전 상태)")


args = sys.argv[1:]
undo = "--restore" in args
rest = [a for a in args if not a.startswith("--")]
if not undo:
    src, want, rest = Path(rest[0]), rest[1].lower(), rest[2:]
    if sha(src) != want:
        sys.exit(f"{src}: 해시가 {want[:16]} 이 아니다 — 멈춤")
games = [Path(a) for a in rest]
for g in games:
    if not (g / "KishinHisho_chs.exe").exists():
        sys.exit(f"{g}: 게임 폴더가 아니다")
    if running_in(g):
        sys.exit(f"{g}: 이 폴더의 게임이 켜져 있다 — 손대지 않음")
for g in games:
    restore(g) if undo else install(src, want, g)
