"""읽기 전용: 전투 팩(GUPK)의 그림 항목(dds 말고 tga·bmp·png·jpg)을 크기와 함께 적는다. 64×64 같은 작은 그림 후보 찾기용.

  python -I -X utf8 list_pack_images.py <demonbane.dll> <출력 폴더> <가로> <세로> <팩...>

해당 크기의 그림은 출력 폴더에 풀어 둔다(이름의 / 는 __ 로).
"""
from __future__ import annotations

import io
import os
import sys

from PIL import Image

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
TOOLS = r"P:\AI\khd-runtime-fix\tools"
src = open(os.path.join(TOOLS, "gupk_sounds.py"), encoding="utf-8").read().split("\nargs = sys.argv[1:]")[0]
gs: dict = {}
exec(compile(src, "gupk_sounds.py", "exec"), gs)


def main():
    dll, out, w, h, packs = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5:]
    os.makedirs(out, exist_ok=True)
    table = gs["read_pe_va"](dll, gs["LOOKUP_TABLE_VA"], 6 * 256)
    counts = {}
    for pack in packs:
        for idx, name, key, unpacked, off, size in gs["entries"](pack, table):
            ext = os.path.splitext(name.lower())[1]
            if off == 0xFFFFFFFF or ext not in (".tga", ".bmp", ".png", ".jpg", ".jpeg"):
                continue
            counts[ext] = counts.get(ext, 0) + 1
            if unpacked > 4_000_000:
                continue
            data = gs["extract"](pack, table, key, unpacked, off, size)
            try:
                im = Image.open(io.BytesIO(data))
                size_ = im.size
                mode = im.mode
            except Exception:
                continue
            if size_ == (w, h):
                im = im.convert("RGBA")
                ex = im.getextrema()
                print(f"{os.path.basename(pack)}: {name}  {size_} {mode}  rgb max {ex[0][1]},{ex[1][1]},{ex[2][1]}  alpha {ex[3]}")
                open(os.path.join(out, name.replace("/", "__")), "wb").write(data)
    print("image entries by type:", counts)


if __name__ == "__main__":
    main()
