"""기신비상 배포 패치 두 꾸러미 가늠(읽기만): 합본 파일을 「게임 패치」「고화질 애셋」「사용자 원본(안 실음)」「뺌」으로 가르고,
파일마다 압축 크기(lzma preset 1, 24MB 넘으면 가운데 24MB 표본)를 더한다. dmpack.dat·dmpack001.dat 은 바뀐 메뉴 그림 11장만,
mpeg.pak 은 바뀐 영상 4편만 덧씌우기로 실는다고 보고 그 크기로 센다. 결과 patch_plan.json."""
import json
import lzma
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
G = Path(r"F:\AI\khd-latest-game")
inv = json.loads(Path(r"Y:\AI\claude-work\khd-session-57a04242\patch_inventory.json").read_text(encoding="utf-8"))
OUT = Path(r"Y:\AI\claude-work\khd-session-57a04242\patch_plan.json")
OFFICIAL_120 = {"dmpack001.dat", "dmpack002.dat", "0sound.pak", "0voice.pak", "system.pak"}   # 2006 날짜 = 공식 업데이트
HD = {"dmpack600_retex.dat"}
DROP = re.compile(r"(_log|\.log$|trace|\.raw$|\.old|status\.txt$|crash|^dgvoodoo\.conf$|^_compat_)", re.I)
MENU_MB = 1.3 + 4.1                       # dmpack.dat 3장 + dmpack001.dat 8장 (항목 대조)
MPEG_MB = 279_855_120 / 2**20            # 바뀐 영상 4편


def ratio(p: Path) -> float:
    size = p.stat().st_size
    with open(p, "rb") as f:
        if size > 24 << 20:
            f.seek(size // 2 - (12 << 20))
            data = f.read(24 << 20)
        else:
            data = f.read()
    return len(lzma.compress(data, preset=1)) / max(1, len(data)) if data else 1.0


parts = {"game": [], "hd": [], "user_base": [], "drop": []}
for r in inv["rows"]:
    rel, size, st = r["rel"], r["size"] / 2**20, r["status"]
    name = rel.split("/")[-1]
    if st == "exclude" or DROP.search(name):
        parts["drop"].append((rel, size, None))
    elif st == "original" or rel in OFFICIAL_120:
        parts["user_base"].append((rel, size, None))
    elif rel in HD:
        parts["hd"].append((rel, size, ratio(G / rel)))
    elif rel in ("dmpack.dat", "mpeg.pak"):
        parts["user_base"].append((rel + " (원본은 사용자 것 그대로)", size, None))
    else:
        parts["game"].append((rel, size, ratio(G / rel)))
parts["game"].append(("0mpeg.pak (새로: 바뀐 영상 4편)", MPEG_MB, 0.86))
parts["game"].append(("dmpack 메뉴 그림 덧씌우기 (새로: 11장)", MENU_MB, 0.80))
summary = {}
for k, rows in parts.items():
    raw = sum(s for _r, s, _c in rows)
    comp = sum(s * c for _r, s, c in rows if c is not None)
    summary[k] = dict(files=len(rows), raw_mb=round(raw, 1), compressed_mb=round(comp, 1) if k in ("game", "hd") else None)
OUT.write_text(json.dumps(dict(summary=summary, parts={k: [(r, round(s, 2), c and round(c, 3)) for r, s, c in v]
                                                       for k, v in parts.items()}), ensure_ascii=False, indent=0), encoding="utf-8")
print(json.dumps(summary, ensure_ascii=False))


def group(rel):
    if rel.startswith("autobattle/"):
        return "autobattle (자동 전투·자막·4K 모듈·자동 진행)"
    if re.match(r"dmpack5\d\d", rel) or rel == "dmpack003.dat":
        return "전투·DUEL 묶음 dmpack003·500~536"
    if rel.startswith("0mpeg"):
        return "한국어 영상 4편 (0mpeg.pak)"
    if rel == "0replay.pak":
        return "다시 보기 0replay.pak"
    if rel.endswith((".dll", ".exe", ".ini")) or rel.startswith("hook"):
        return "실행·엔진·대리 DLL·LE·설정"
    if rel.endswith(".otf"):
        return "한국어 글꼴"
    return "대본·메뉴·덧씌우기(script·0cg·0logo·0system·0finishers 등)"


from collections import defaultdict  # noqa: E402
g = defaultdict(lambda: [0, 0.0, 0.0])
for rel, s, c in parts["game"]:
    k = group(rel)
    g[k][0] += 1
    g[k][1] += s
    g[k][2] += s * c
print("\n게임 패치 묶음별:")
for k, (n, s, c) in sorted(g.items(), key=lambda x: -x[1][2]):
    print(f"  {k:46} {n:4}개  풀면 {s:7.1f} MB  압축 {c:6.1f} MB")
print("\n사용자 원본(안 실음):", [(r, round(s, 1)) for r, s, _c in parts["user_base"]])
print("고화질:", [(r, round(s, 1), round(c, 2)) for r, s, c in parts["hd"]])
