"""합본용 전용 묶음 만들기 — 코덱스 후보 묶음 그대로 + 빠진 장면 효과음 목록(.se)만 원래 장면 것으로 채운다.

HM(9303·9781·9782)·F2(9501) 전용 장면에는 .se 가 없었다(다른 전용 장면 13개는 모두 있음). 원래 장면의 .se 를
바이트 그대로 붙인다. 나머지 항목은 한 바이트도 바꾸지 않는다(읽어 되돌려 대조).

python -I -X utf8 build_packs.py
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
GAME = Path(r"P:\AI\khd-runtime-fix\game")
CAND = Path(r"P:\AI\Codex\projects\migrated-from-c\onedrive-chatgpt\데몬베인 시리즈 2\work\duel-settings-20261021\player-only-ai-20261026")
OUT = Path(r"P:\AI\khd-hero-ai\merged\out")
WORK = Path(r"P:\AI\khd-hero-ai\merged\pack-work")

_src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs: dict = {}
_argv, sys.argv = sys.argv, [sys.argv[0], str(GAME / "demonbane.dll")]
exec(compile(_src, "gupk_sounds.py", "exec"), gs)
sys.argv = _argv
TABLE = gs["read_pe_va"](str(GAME / "demonbane.dll"), gs["LOOKUP_TABLE_VA"], 6 * 256)


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def read_all(pack: Path) -> dict:
    out = {}
    for idx, name, key, unpacked, off, size in gs["entries"](str(pack), TABLE):
        out[name.replace("\\", "/").lower()] = gs["extract"](str(pack), TABLE, key, unpacked, off, size)
    return out


def installed_table() -> dict:
    eff = {}
    for p in sorted(GAME.glob("dmpack*.dat")):            # 게임처럼 이름 순, 뒤 묶음이 이긴다
        for idx, name, key, unpacked, off, size in gs["entries"](str(p), TABLE):
            eff[name.replace("\\", "/").lower()] = (str(p), key, unpacked, off, size)
    return eff


def scene_file(n: int, ext: str) -> str:
    return f"data/scene/scene{n:03d}/scene{n:03d}.{ext}"


def pack(files: dict, out: Path, work: Path):
    # khd_replay_20261015\tools\build_duel.pack 와 같은 길(gupk_build.py, 목록 파일)
    work.mkdir(parents=True, exist_ok=True)
    specs = []
    for name, data in files.items():
        p = work / name.replace("/", "__")
        p.write_bytes(data)
        specs.append(f"{name.replace('/', chr(92))}={p}")
    listing = work / "_specs.txt"
    listing.write_text("\n".join(specs) + "\n", encoding="utf-8")
    r = subprocess.run([sys.executable, "-I", str(TOOLS / "gupk_build.py"), str(GAME / "demonbane.dll"), str(out),
                        "@" + str(listing)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit(r.returncode)


# (후보 묶음, [(전용 장면, 원래 장면)])
PACKS = [
    (CAND / "AM/normal/r1/out/dmpack514_am_owned.dat", []),
    (CAND / "A2/normal/selected/out/dmpack514_a2_air.dat", []),
    (CAND / "EM/normal/r2/out/dmpack514_em_owned.dat", []),
    (CAND / "F2/normal/r3/out/dmpack514_f2_owned.dat", [(9501, 1771)]),
    (CAND / "HM/normal/r3/out/dmpack513_hm_owned.dat", [(9303, 1732), (9781, 720), (9782, 722)]),
]

eff = installed_table()
OUT.mkdir(parents=True, exist_ok=True)
report = []
for src, add in PACKS:
    files = read_all(src)
    for dest, orig in add:
        name = scene_file(dest, "se")
        assert name not in files, name
        assert scene_file(dest, "cmn") in files and scene_file(dest, "scn") in files, dest
        p, key, unpacked, off, size = eff[scene_file(orig, "se")]
        files[name] = gs["extract"](p, TABLE, key, unpacked, off, size)
    dst = OUT / src.name
    if add:
        pack(files, dst, WORK / src.stem)
        back = read_all(dst)
        assert back == files, f"{dst.name}: 되읽기가 다르다"
        before = read_all(src)
        assert all(back[k] == v for k, v in before.items()), f"{dst.name}: 원래 항목이 바뀌었다"
    else:
        shutil.copy2(src, dst)
    report.append(dict(pack=dst.name, sha256=sha(dst.read_bytes()), from_candidate=str(src),
                       candidate_sha256=sha(src.read_bytes()), members=len(files),
                       added_se=[dict(scene=d, from_scene=o, from_pack=Path(eff[scene_file(o, 'se')][0]).name,
                                      sha256=sha(files[scene_file(d, 'se')])) for d, o in add]))
    print(f"{dst.name}: {len(files)} 항목, se 추가 {[d for d, o in add]}, {report[-1]['sha256'][:16]}")
(OUT.parent / "packs.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
