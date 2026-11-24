"""읽기 전용: 전투 팩(GUPK)에서 덤프한 텍스처와 같은 그림을 찾는다 — 어느 팩의 어느 항목인지, 원본 팩과 같은지.

  python -I -X utf8 find_envtex.py <덤프 dds> <demonbane.dll> <팩...>

DDS 항목(.dds)을 모두 풀어 첫 단계 블록이 덤프와 같은지(또는 크기·형식이 같은지) 본다.
푸는 방식은 khd-runtime-fix\\tools\\gupk_sounds.py 그대로(그 파일의 함수만 읽어 씀).
"""
from __future__ import annotations

import os
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
TOOLS = r"P:\AI\khd-runtime-fix\tools"
src = open(os.path.join(TOOLS, "gupk_sounds.py"), encoding="utf-8").read().split("\nargs = sys.argv[1:]")[0]
gs: dict = {}
exec(compile(src, "gupk_sounds.py", "exec"), gs)


def dds_info(data: bytes):
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    h, w = struct.unpack_from("<II", data, 12)
    fourcc = data[84:88]
    mips = struct.unpack_from("<I", data, 28)[0]
    return w, h, fourcc, mips


def main():
    dump, dll, packs = sys.argv[1], sys.argv[2], sys.argv[3:]
    ref = open(dump, "rb").read()
    rw, rh, rfcc, _ = dds_info(ref)
    rblocks = ref[128:]
    table = gs["read_pe_va"](dll, gs["LOOKUP_TABLE_VA"], 6 * 256)
    for pack in packs:
        n_dds = 0
        for idx, name, key, unpacked, off, size in gs["entries"](pack, table):
            if off == 0xFFFFFFFF or not name.lower().endswith(".dds"):
                continue
            n_dds += 1
            if unpacked > 2_000_000:
                continue
            data = gs["extract"](pack, table, key, unpacked, off, size)
            info = dds_info(data)
            if not info:
                continue
            w, h, fcc, mips = info
            if (w, h, fcc) != (rw, rh, rfcc):
                continue
            same = data[128:128 + len(rblocks)] == rblocks
            print(f"{os.path.basename(pack)}: {name}  {w}x{h} {fcc.decode(errors='replace')} mips {mips}  {'SAME as the dump' if same else 'different'}")
        print(f"{os.path.basename(pack)}: {n_dds} dds entries scanned")


if __name__ == "__main__":
    main()
