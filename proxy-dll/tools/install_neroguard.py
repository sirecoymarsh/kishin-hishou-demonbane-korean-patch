"""네로 등·치마 막기 풀기(균형 모드) 묶음 516 + 균형 모드 장면 표 설치·되돌리기(11-02).

  python -I -X utf8 install_neroguard.py <묶음> <묶음 sha256> <표> <게임 폴더>...   설치(이 도구로 넣은 옛 판이면 바꿈)
  python -I -X utf8 install_neroguard.py --restore <게임 폴더>...                      설치 전으로(묶음 빼기·표 되돌리기)

- 그 폴더에서 게임이 돌고 있으면 손대지 않는다. 처음 설치 때 원래 표(autobattle\\duel_balance_scenes.tsv)를 백업한다.
- 기록: P:\\AI\\khd-hero-ai\\backup\\<폴더 이름_해시>\\neroguard.json, 원래 표 duel_balance_scenes.<해시16>.tsv
- 520(각인 666)은 이 묶음 위에 다시 빌드해야 한다(tools\\build_kokuin666.py — 같은 네로 정의를 덮으므로).
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

NAME = "dmpack516_neroguard.dat"
BACKUP = Path(r"P:\AI\khd-hero-ai\backup")


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def running_in(game: Path) -> bool:
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='KishinHisho_chs.exe'\" | ForEach-Object { $_.ExecutablePath }"],
                       capture_output=True, text=True, creationflags=0x08000000, timeout=20)
    return any(Path(x.strip()).parent.resolve() == game.resolve() for x in r.stdout.splitlines() if x.strip())


def bdir(game: Path) -> Path:
    return BACKUP / f"{game.name}_{hashlib.sha256(str(game.resolve()).lower().encode()).hexdigest()[:8]}"


def put(src: Path, dst: Path):
    tmp = dst.with_name(dst.name + ".neroguard-stage")
    shutil.copy2(src, tmp)
    os.replace(tmp, dst)


def install(pack: Path, want: str, table: Path, game: Path):
    d = bdir(game)
    d.mkdir(parents=True, exist_ok=True)
    recp = d / "neroguard.json"
    rec = json.loads(recp.read_text(encoding="utf-8")) if recp.exists() else {"game": str(game), "installs": []}
    tsv = game / "autobattle" / "duel_balance_scenes.tsv"
    if "original_table" not in rec:
        h = sha(tsv)
        shutil.copy2(tsv, d / f"duel_balance_scenes.{h[:16]}.tsv")
        rec["original_table"] = h
    ours_packs = {i["pack"] for i in rec["installs"]}
    ours_tables = {i["table"] for i in rec["installs"]} | {rec["original_table"]}
    dst = game / NAME
    now = sha(dst) if dst.exists() else None
    if now is not None and now not in ours_packs:
        sys.exit(f"{game}: {NAME} 이 이 도구가 넣은 판이 아니다({now[:16]}) — 멈춤")
    if sha(tsv) not in ours_tables:
        sys.exit(f"{game}: 표가 이 도구가 아는 판이 아니다({sha(tsv)[:16]}) — 다른 작업이 바꿨다, 멈춤")
    put(pack, dst)
    put(table, tsv)
    assert sha(dst) == want
    rec["installs"].append(dict(at=time.strftime("%Y-%m-%dT%H:%M:%S"), pack=want, table=sha(tsv), replaced=now))
    recp.write_text(json.dumps(rec, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{game}: {NAME} {want[:16]} + 표 {sha(tsv)[:16]} 설치")


def restore(game: Path):
    d = bdir(game)
    rec = json.loads((d / "neroguard.json").read_text(encoding="utf-8"))
    dst = game / NAME
    if dst.exists():
        if sha(dst) not in {i["pack"] for i in rec["installs"]}:
            sys.exit(f"{game}: {NAME} 이 이 도구가 넣은 판이 아니다 — 멈춤")
        dst.unlink()
    put(d / f"duel_balance_scenes.{rec['original_table'][:16]}.tsv", game / "autobattle" / "duel_balance_scenes.tsv")
    rec["installs"].append(dict(at=time.strftime("%Y-%m-%dT%H:%M:%S"), pack=None, table=rec["original_table"]))
    (d / "neroguard.json").write_text(json.dumps(rec, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{game}: {NAME} 뺌, 표 원래대로")


args = sys.argv[1:]
undo = "--restore" in args
rest = [a for a in args if not a.startswith("--")]
if not undo:
    pack, want, table, rest = Path(rest[0]), rest[1].lower(), Path(rest[2]), rest[3:]
    if sha(pack) != want:
        sys.exit(f"{pack}: 해시가 {want[:16]} 이 아니다 — 멈춤")
for g in map(Path, rest):
    if not (g / "KishinHisho_chs.exe").exists():
        sys.exit(f"{g}: 게임 폴더가 아니다")
    if running_in(g):
        sys.exit(f"{g}: 이 폴더의 게임이 켜져 있다 — 손대지 않음")
for g in map(Path, rest):
    restore(g) if undo else install(pack, want, table, g)
