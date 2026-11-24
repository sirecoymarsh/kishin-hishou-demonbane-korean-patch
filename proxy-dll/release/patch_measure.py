"""배포 크기 가늠(읽기만): ①제자리 수정한 dmpack.dat·dmpack001.dat 이 원본과 다른 양(64KB 블록) ②큰 파일 압축 비율(가운데 32MB 표본,
lzma preset 1) ③autobattle 안 기록·시험 찌꺼기."""
import json
import lzma
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
G = Path(r"F:\AI\khd-latest-game")
B = 1 << 16


def diff_blocks(a, b):
    n = d = 0
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            x, y = fa.read(B), fb.read(B)
            if not x and not y:
                break
            n += 1
            d += x != y
    return n, d


for cur, orig in (("dmpack.dat", "dmpack.dat.orig_FDD3D1A8"), ("dmpack001.dat", "dmpack001.dat.orig_0B479A4D"),
                  ("demonbane.dll", "demonbane.dll.orig_3F389D2D")):
    n, d = diff_blocks(G / cur, G / orig)
    print(f"{cur}: {d}/{n} blocks of 64 KB differ from {orig} -> about {d * B / 2**20:.1f} MB of changed data")

S = 32 << 20
for name in ("dmpack505_duelauto.dat", "dmpack522_hard_v11.dat", "0replay.pak", "dmpack530_duel_sight.dat",
             "dmpack533_nero_event.dat", "dmpack600_retex.dat", "mpeg.pak", "KHDKorean-CP932Han-Ext.otf", "dmpack001.dat"):
    p = G / name
    size = p.stat().st_size
    with open(p, "rb") as f:
        f.seek(max(0, size // 2 - S // 2))
        data = f.read(S)
    c = len(lzma.compress(data, preset=1))
    print(f"{name:30} {size / 2**20:7.1f} MB  sample {len(data) / 2**20:5.1f} MB -> {c / len(data):5.2f}  (whole file ~{size * c / len(data) / 2**20:6.1f} MB)")

inv = json.loads(Path(r"Y:\AI\claude-work\khd-session-57a04242\patch_inventory.json").read_text(encoding="utf-8"))
junk = [r for r in inv["rows"] if r["status"] != "exclude" and r["rel"].startswith("autobattle/")
        and re.search(r"(_log|\.log|trace|\.raw$|\.old|status\.txt$|/cap/|crash)", r["rel"], re.I)]
print(f"autobattle logs/test leftovers still counted: {len(junk)} files {sum(r['size'] for r in junk) / 2**20:.1f} MB",
      [r["rel"] for r in sorted(junk, key=lambda r: -r["size"])[:6]])
