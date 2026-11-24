"""새 설치 시험용 바탕: 일본판 1.00(P:\\AI\\khd-retl\\JP_PURE) + 공식 1.20 업데이트 파일(P:\\AI\\khd-runtime-fix\\jp120 의 2006년 날짜 파일).
통째 복사(하드링크 금지 — 설치가 원본으로 샌다). 10-30 배포의 원본 필수 파일 해시와 대조.
  python -I -X utf8 make_testbase.py <대상 폴더>"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

dst = Path(sys.argv[1])
if dst.exists():
    raise SystemExit(f"{dst} exists")
PURE = Path(r"P:\AI\khd-retl\JP_PURE")
J120 = Path(r"P:\AI\khd-runtime-fix\jp120")
FROM_PURE = ["advdata.pak", "cg.pak", "dmpack.dat", "mpeg.pak", "sound.pak", "voice.pak"]
FROM_120 = ["0advdata.pak", "0cg.pak", "0sound.pak", "0voice.pak", "dmpack001.dat", "dmpack002.dat", "demonbane.dll",
            "script.pak", "system.dll", "system.pak"]
m1030 = json.loads(Path(r"P:\AI\Codex\projects\migrated-from-c\onedrive-chatgpt\데몬베인 시리즈 2\delivery"
                        r"\KishinHisho_KO_DUEL_20261030\manifest.json").read_text(encoding="utf-8-sig"))
req = {r["path"]: r["sha256"] for r in m1030["required"]}


def sha(p):
    d = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            d.update(b)
    return d.hexdigest()


dst.mkdir(parents=True)
(dst / "SAVE").mkdir()
for names, src in ((FROM_PURE, PURE), (FROM_120, J120)):
    for n in names:
        shutil.copyfile(src / n, dst / n)
        h = sha(dst / n)
        note = "" if n not in req else (" = 1.20 (10-30 required)" if h == req[n] else " != 10-30 required " + req[n][:8])
        print(f"{n:16} {h[:8]}{note}")
print("mpeg.pak is 1.00:", sha(dst / "mpeg.pak").startswith("83a06b06"), "| dmpack.dat is 1.00:", sha(dst / "dmpack.dat").startswith("fdd3d1a8"))
